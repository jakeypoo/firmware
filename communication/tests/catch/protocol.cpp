/**
 ******************************************************************************
  Copyright (c) 2013-2015 Particle Industries, Inc.  All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************
 */

#include "protocol.h"
#include "catch.hpp"
#include "fakeit.hpp"
using namespace fakeit;

using namespace particle::protocol;

class AbstractProtocol : public Protocol
{
public:
	AbstractProtocol(MessageChannel& channel) : Protocol(channel) {}

	virtual size_t build_hello(Message& message, bool was_ota_upgrade_successful)
	{
		return 0;
	}

	virtual void init(const char *id,
	          const SparkKeys &keys,
	          const SparkCallbacks &callbacks,
	          const SparkDescriptor &descriptor)
	{
		Protocol::init(callbacks, descriptor);
	}

};

SCENARIO("default product co-ordinates are set")
{
	MessageChannel* channel = nullptr;
	AbstractProtocol p(*channel);	// channel is not used
	product_details_t details;
	details.size = sizeof(details);
	p.get_product_details(details);

	REQUIRE(details.product_id==PRODUCT_ID);
	REQUIRE(details.product_version==PRODUCT_FIRMWARE_VERSION);
}

void event_handler(const char* event, const char* data)
{
}

SCENARIO("5 subscribe messages are registered")
{
	MessageChannel* channel = nullptr;
	AbstractProtocol p(*channel);	// channel is not used
	for (int i=0; i<5; i++) {
		INFO("adding event " << i);
		char buf[2];
		buf[1] = 0;
		buf[0] = 'A'+i;
		bool added = p.add_event_handler(buf, event_handler);
		REQUIRE(added);
	}

	bool added = p.add_event_handler("abcd", event_handler);
	REQUIRE(!added);

	p.remove_event_handlers(nullptr);

	added = p.add_event_handler("abcd", event_handler);
	REQUIRE(added);

}

struct ProtocolBuilder
{
	SparkKeys keys;
	SparkCallbacks callbacks;
	SparkDescriptor descriptor;
	char id[12];

	ProtocolBuilder()
	{
		memset(this, 0, sizeof(*this));
		callbacks.size = sizeof(callbacks);
	}

	void build(Protocol& p)
	{
		p.init(id, keys, callbacks, descriptor);
	}
};

uint32_t fake_millis()
{
	return 0;
}

void event_ack(bool confirmable)
{
	ProtocolBuilder builder;
	builder.callbacks.millis = &fake_millis;
	Mock<MessageChannel> channel;
	AbstractProtocol p(channel.get());
	builder.build(p);

	// build an event message - either non/con
	Message event;
	uint8_t event_buf[50];
	event.set_buffer(event_buf, sizeof(event_buf));
	size_t msglen = Messages::event(event_buf, 0x1234, "e", "", 60, EventType::PUBLIC, confirmable);
	event.set_length(msglen);
	event.decode_id();	// need this in the test since it's not done by our mock MessageChannel

	auto receive_event = [&event, msglen](Message& msg) {
		msg = event;
		return NO_ERROR;
	};
	When(Method(channel,receive)).Do(receive_event);

	// provide a response buffer when Response is called.
	Message response;
	uint8_t response_buf[50];
	response.set_buffer(response_buf, sizeof(response_buf));

	auto provide_response = [&response](Message& original, Message& msg, size_t required) {
		REQUIRE(required <= 50);
		msg = response;
		return NO_ERROR;
	};

	if (confirmable)
	{
		When(Method(channel,response)).Do(provide_response);

		auto validate_response = [](Message& msg){
			REQUIRE(msg.length()==4);
			uint8_t* buf = msg.buf();
			REQUIRE(CoAP::type(buf)==CoAPType::ACK);
			REQUIRE(msg.get_id()==0x1234);
			return NO_ERROR;
		};

		if (confirmable)
			When(Method(channel,send)).Do(validate_response);
	}

	bool success = p.event_loop();
	REQUIRE(success);

	if (confirmable)
		Verify(Method(channel,send));
}

SCENARIO("confirmable events are acknowledged")
{
	event_ack(true);
}

SCENARIO("non-confirmable events are not acknowledged")
{
	event_ack(false);
}

