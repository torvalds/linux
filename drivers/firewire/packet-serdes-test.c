// SPDX-License-Identifier: GPL-2.0-or-later
//
// packet-serdes-test.c - An application of Kunit to check serialization/deserialization of packets
//			  defined by IEEE 1394.
//
// Copyright (c) 2024 Takashi Sakamoto

#include <kunit/test.h>

#include <linux/firewire-constants.h>

#include "packet-header-definitions.h"

static void serialize_async_header_common(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					  unsigned int dst_id, unsigned int tlabel,
					  unsigned int retry, unsigned int tcode,
					  unsigned int priority, unsigned int src_id)
{
	async_header_set_destination(header, dst_id);
	async_header_set_tlabel(header, tlabel);
	async_header_set_retry(header, retry);
	async_header_set_tcode(header, tcode);
	async_header_set_priority(header, priority);
	async_header_set_source(header, src_id);
}

static void serialize_async_header_request(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					   unsigned int dst_id, unsigned int tlabel,
					   unsigned int retry, unsigned int tcode,
					   unsigned int priority, unsigned int src_id, u64 offset)
{
	serialize_async_header_common(header, dst_id, tlabel, retry, tcode, priority, src_id);
	async_header_set_offset(header, offset);
}

static void serialize_async_header_quadlet_request(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						   unsigned int dst_id, unsigned int tlabel,
						   unsigned int retry, unsigned int tcode,
						   unsigned int priority, unsigned int src_id,
						   u64 offset)
{
	serialize_async_header_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
				       offset);
}

static void serialize_async_header_block_request(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						 unsigned int dst_id, unsigned int tlabel,
						 unsigned int retry, unsigned int tcode,
						 unsigned int priority, unsigned int src_id,
						 u64 offset, unsigned int data_length,
						 unsigned int extended_tcode)
{
	serialize_async_header_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
				       offset);
	async_header_set_data_length(header, data_length);
	async_header_set_extended_tcode(header, extended_tcode);
}

static void serialize_async_header_response(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					    unsigned int dst_id, unsigned int tlabel,
					    unsigned int retry, unsigned int tcode,
					    unsigned int priority, unsigned int src_id,
					    unsigned int rcode)
{
	serialize_async_header_common(header, dst_id, tlabel, retry, tcode, priority, src_id);
	async_header_set_rcode(header, rcode);
}

static void serialize_async_header_quadlet_response(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						    unsigned int dst_id, unsigned int tlabel,
						    unsigned int retry, unsigned int tcode,
						    unsigned int priority, unsigned int src_id,
						    unsigned int rcode)
{
	serialize_async_header_response(header, dst_id, tlabel, retry, tcode, priority, src_id,
					rcode);
}

static void serialize_async_header_block_response(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						  unsigned int dst_id, unsigned int tlabel,
						  unsigned int retry, unsigned int tcode,
						  unsigned int priority, unsigned int src_id,
						  unsigned int rcode, unsigned int data_length,
						  unsigned int extended_tcode)
{
	serialize_async_header_response(header, dst_id, tlabel, retry, tcode, priority, src_id,
					rcode);
	async_header_set_data_length(header, data_length);
	async_header_set_extended_tcode(header, extended_tcode);
}

static void deserialize_async_header_common(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
					    unsigned int *dst_id, unsigned int *tlabel,
					    unsigned int *retry, unsigned int *tcode,
					    unsigned int *priority, unsigned int *src_id)
{
	*dst_id = async_header_get_destination(header);
	*tlabel = async_header_get_tlabel(header);
	*retry = async_header_get_retry(header);
	*tcode = async_header_get_tcode(header);
	*priority = async_header_get_priority(header);
	*src_id = async_header_get_source(header);
}

static void deserialize_async_header_request(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
					     unsigned int *dst_id, unsigned int *tlabel,
					     unsigned int *retry, unsigned int *tcode,
					     unsigned int *priority, unsigned int *src_id,
					     u64 *offset)
{
	deserialize_async_header_common(header, dst_id, tlabel, retry, tcode, priority, src_id);
	*offset = async_header_get_offset(header);
}

static void deserialize_async_header_quadlet_request(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
						     unsigned int *dst_id, unsigned int *tlabel,
						     unsigned int *retry, unsigned int *tcode,
						     unsigned int *priority, unsigned int *src_id,
						     u64 *offset)
{
	deserialize_async_header_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
					 offset);
}

static void deserialize_async_header_block_request(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
						   unsigned int *dst_id, unsigned int *tlabel,
						   unsigned int *retry, unsigned int *tcode,
						   unsigned int *priority, unsigned int *src_id,
						   u64 *offset,
						   unsigned int *data_length,
						   unsigned int *extended_tcode)
{
	deserialize_async_header_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
					 offset);
	*data_length = async_header_get_data_length(header);
	*extended_tcode = async_header_get_extended_tcode(header);
}

static void deserialize_async_header_response(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
					      unsigned int *dst_id, unsigned int *tlabel,
					      unsigned int *retry, unsigned int *tcode,
					      unsigned int *priority, unsigned int *src_id,
					      unsigned int *rcode)
{
	deserialize_async_header_common(header, dst_id, tlabel, retry, tcode, priority, src_id);
	*rcode = async_header_get_rcode(header);
}

static void deserialize_async_header_quadlet_response(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
						      unsigned int *dst_id, unsigned int *tlabel,
						      unsigned int *retry, unsigned int *tcode,
						      unsigned int *priority, unsigned int *src_id,
						      unsigned int *rcode)
{
	deserialize_async_header_response(header, dst_id, tlabel, retry, tcode, priority, src_id, rcode);
}

static void deserialize_async_header_block_response(const u32 header[ASYNC_HEADER_QUADLET_COUNT],
						    unsigned int *dst_id, unsigned int *tlabel,
						    unsigned int *retry, unsigned int *tcode,
						    unsigned int *priority, unsigned int *src_id,
						    unsigned int *rcode, unsigned int *data_length,
						    unsigned int *extended_tcode)
{
	deserialize_async_header_response(header, dst_id, tlabel, retry, tcode, priority, src_id, rcode);
	*data_length = async_header_get_data_length(header);
	*extended_tcode = async_header_get_extended_tcode(header);
}

static void serialize_isoc_header(u32 *header, unsigned int data_length, unsigned int tag,
				  unsigned int channel, unsigned int tcode, unsigned int sy)
{
	isoc_header_set_data_length(header, data_length);
	isoc_header_set_tag(header, tag);
	isoc_header_set_channel(header, channel);
	isoc_header_set_tcode(header, tcode);
	isoc_header_set_sy(header, sy);
}

static void deserialize_isoc_header(u32 header, unsigned int *data_length, unsigned int *tag,
				    unsigned int *channel, unsigned int *tcode, unsigned int *sy)
{
	*data_length = isoc_header_get_data_length(header);
	*tag = isoc_header_get_tag(header);
	*channel = isoc_header_get_channel(header);
	*tcode = isoc_header_get_tcode(header);
	*sy = isoc_header_get_sy(header);
}

static void test_async_header_write_quadlet_request(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc05100,
		0xffc1ffff,
		0xf0000234,
		0x1f0000c0,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	u64 offset;
	u32 quadlet_data;

	deserialize_async_header_quadlet_request(expected, &dst_id, &tlabel, &retry, &tcode,
						 &priority, &src_id, &offset);
	quadlet_data = async_header_get_quadlet_data(expected);

	KUNIT_EXPECT_EQ(test, 0xffc0, dst_id);
	KUNIT_EXPECT_EQ(test, 0x14, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_WRITE_QUADLET_REQUEST, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc1, src_id);
	KUNIT_EXPECT_EQ(test, 0xfffff0000234, offset);
	KUNIT_EXPECT_EQ(test, 0x1f0000c0, quadlet_data);

	serialize_async_header_quadlet_request(header, dst_id, tlabel, retry, tcode, priority,
					       src_id, offset);
	async_header_set_quadlet_data(header, quadlet_data);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_write_block_request(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc06510,
		0xffc1ecc0,
		0x00000000,
		0x00180000,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	u64 offset;
	unsigned int data_length;
	unsigned int extended_tcode;

	deserialize_async_header_block_request(expected, &dst_id, &tlabel, &retry, &tcode,
					       &priority, &src_id, &offset, &data_length,
					       &extended_tcode);

	KUNIT_EXPECT_EQ(test, 0xffc0, dst_id);
	KUNIT_EXPECT_EQ(test, 0x19, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_WRITE_BLOCK_REQUEST, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc1, src_id);
	KUNIT_EXPECT_EQ(test, 0xecc000000000, offset);
	KUNIT_EXPECT_EQ(test, 0x0018, data_length);
	KUNIT_EXPECT_EQ(test, 0x0000, extended_tcode);

	serialize_async_header_block_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
					     offset, data_length, extended_tcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_write_response(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc15120,
		0xffc00000,
		0x00000000,
		0x00000000,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	unsigned int rcode;

	deserialize_async_header_quadlet_response(expected, &dst_id, &tlabel, &retry, &tcode,
						  &priority, &src_id, &rcode);

	KUNIT_EXPECT_EQ(test, 0xffc1, dst_id);
	KUNIT_EXPECT_EQ(test, 0x14, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_WRITE_RESPONSE, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc0, src_id);
	KUNIT_EXPECT_EQ(test, RCODE_COMPLETE, rcode);

	serialize_async_header_quadlet_response(header, dst_id, tlabel, retry, tcode, priority,
						src_id, rcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected) - sizeof(expected[0]));
}

static void test_async_header_read_quadlet_request(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc0f140,
		0xffc1ffff,
		0xf0000984,
		0x00000000,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	u64 offset;

	deserialize_async_header_quadlet_request(expected, &dst_id, &tlabel, &retry, &tcode,
						 &priority, &src_id, &offset);

	KUNIT_EXPECT_EQ(test, 0xffc0, dst_id);
	KUNIT_EXPECT_EQ(test, 0x3c, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_READ_QUADLET_REQUEST, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc1, src_id);
	KUNIT_EXPECT_EQ(test, 0xfffff0000984, offset);

	serialize_async_header_quadlet_request(header, dst_id, tlabel, retry, tcode, priority,
					       src_id, offset);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_read_quadlet_response(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc1f160,
		0xffc00000,
		0x00000000,
		0x00000180,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	unsigned int rcode;
	u32 quadlet_data;

	deserialize_async_header_quadlet_response(expected, &dst_id, &tlabel, &retry, &tcode,
						  &priority, &src_id, &rcode);
	quadlet_data = async_header_get_quadlet_data(expected);

	KUNIT_EXPECT_EQ(test, 0xffc1, dst_id);
	KUNIT_EXPECT_EQ(test, 0x3c, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_READ_QUADLET_RESPONSE, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc0, src_id);
	KUNIT_EXPECT_EQ(test, RCODE_COMPLETE, rcode);
	KUNIT_EXPECT_EQ(test, 0x00000180, quadlet_data);

	serialize_async_header_quadlet_response(header, dst_id, tlabel, retry, tcode, priority,
						src_id, rcode);
	async_header_set_quadlet_data(header, quadlet_data);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_read_block_request(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc0e150,
		0xffc1ffff,
		0xf0000400,
		0x00200000,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	u64 offset;
	unsigned int data_length;
	unsigned int extended_tcode;

	deserialize_async_header_block_request(expected, &dst_id, &tlabel, &retry, &tcode,
					       &priority, &src_id, &offset, &data_length,
					       &extended_tcode);

	KUNIT_EXPECT_EQ(test, 0xffc0, dst_id);
	KUNIT_EXPECT_EQ(test, 0x38, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_READ_BLOCK_REQUEST, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc1, src_id);
	KUNIT_EXPECT_EQ(test, 0xfffff0000400, offset);
	KUNIT_EXPECT_EQ(test, 0x0020, data_length);
	KUNIT_EXPECT_EQ(test, 0x0000, extended_tcode);

	serialize_async_header_block_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
					     offset, data_length, extended_tcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_read_block_response(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc1e170,
		0xffc00000,
		0x00000000,
		0x00200000,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	unsigned int rcode;
	unsigned int data_length;
	unsigned int extended_tcode;

	deserialize_async_header_block_response(expected, &dst_id, &tlabel, &retry, &tcode,
						&priority, &src_id, &rcode, &data_length,
						&extended_tcode);

	KUNIT_EXPECT_EQ(test, 0xffc1, dst_id);
	KUNIT_EXPECT_EQ(test, 0x38, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_READ_BLOCK_RESPONSE, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc0, src_id);
	KUNIT_EXPECT_EQ(test, RCODE_COMPLETE, rcode);
	KUNIT_EXPECT_EQ(test, 0x0020, data_length);
	KUNIT_EXPECT_EQ(test, 0x0000, extended_tcode);

	serialize_async_header_block_response(header, dst_id, tlabel, retry, tcode, priority,
					      src_id, rcode, data_length, extended_tcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_lock_request(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc02d90,
		0xffc1ffff,
		0xf0000984,
		0x00080002,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	u64 offset;
	unsigned int data_length;
	unsigned int extended_tcode;

	deserialize_async_header_block_request(expected, &dst_id, &tlabel, &retry, &tcode,
					       &priority, &src_id, &offset, &data_length,
					       &extended_tcode);

	KUNIT_EXPECT_EQ(test, 0xffc0, dst_id);
	KUNIT_EXPECT_EQ(test, 0x0b, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_LOCK_REQUEST, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc1, src_id);
	KUNIT_EXPECT_EQ(test, 0xfffff0000984, offset);
	KUNIT_EXPECT_EQ(test, 0x0008, data_length);
	KUNIT_EXPECT_EQ(test, EXTCODE_COMPARE_SWAP, extended_tcode);

	serialize_async_header_block_request(header, dst_id, tlabel, retry, tcode, priority, src_id,
					     offset, data_length, extended_tcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_async_header_lock_response(struct kunit *test)
{
	static const u32 expected[ASYNC_HEADER_QUADLET_COUNT] = {
		0xffc12db0,
		0xffc00000,
		0x00000000,
		0x00040002,
	};
	u32 header[ASYNC_HEADER_QUADLET_COUNT] = {0, 0, 0, 0};

	unsigned int dst_id;
	unsigned int tlabel;
	unsigned int retry;
	unsigned int tcode;
	unsigned int priority;
	unsigned int src_id;
	unsigned int rcode;
	unsigned int data_length;
	unsigned int extended_tcode;

	deserialize_async_header_block_response(expected, &dst_id, &tlabel, &retry, &tcode,
						&priority, &src_id, &rcode, &data_length,
						&extended_tcode);

	KUNIT_EXPECT_EQ(test, 0xffc1, dst_id);
	KUNIT_EXPECT_EQ(test, 0x0b, tlabel);
	KUNIT_EXPECT_EQ(test, 0x01, retry);
	KUNIT_EXPECT_EQ(test, TCODE_LOCK_RESPONSE, tcode);
	KUNIT_EXPECT_EQ(test, 0x00, priority);
	KUNIT_EXPECT_EQ(test, 0xffc0, src_id);
	KUNIT_EXPECT_EQ(test, RCODE_COMPLETE, rcode);
	KUNIT_EXPECT_EQ(test, 0x0004, data_length);
	KUNIT_EXPECT_EQ(test, EXTCODE_COMPARE_SWAP, extended_tcode);

	serialize_async_header_block_response(header, dst_id, tlabel, retry, tcode, priority,
					      src_id, rcode, data_length, extended_tcode);

	KUNIT_EXPECT_MEMEQ(test, header, expected, sizeof(expected));
}

static void test_isoc_header(struct kunit *test)
{
	const u32 expected = 0x00d08dec;
	u32 header = 0;

	unsigned int data_length;
	unsigned int tag;
	unsigned int channel;
	unsigned int tcode;
	unsigned int sy;

	deserialize_isoc_header(expected, &data_length, &tag, &channel, &tcode, &sy);

	KUNIT_EXPECT_EQ(test, 0xd0, data_length);
	KUNIT_EXPECT_EQ(test, 0x02, tag);
	KUNIT_EXPECT_EQ(test, 0x0d, channel);
	KUNIT_EXPECT_EQ(test, 0x0e, tcode);
	KUNIT_EXPECT_EQ(test, 0x0c, sy);

	serialize_isoc_header(&header, data_length, tag, channel, tcode, sy);

	KUNIT_EXPECT_EQ(test, header, expected);
}

static struct kunit_case packet_serdes_test_cases[] = {
	KUNIT_CASE(test_async_header_write_quadlet_request),
	KUNIT_CASE(test_async_header_write_block_request),
	KUNIT_CASE(test_async_header_write_response),
	KUNIT_CASE(test_async_header_read_quadlet_request),
	KUNIT_CASE(test_async_header_read_quadlet_response),
	KUNIT_CASE(test_async_header_read_block_request),
	KUNIT_CASE(test_async_header_read_block_response),
	KUNIT_CASE(test_async_header_lock_request),
	KUNIT_CASE(test_async_header_lock_response),
	KUNIT_CASE(test_isoc_header),
	{}
};

static struct kunit_suite packet_serdes_test_suite = {
	.name = "firewire-packet-serdes",
	.test_cases = packet_serdes_test_cases,
};
kunit_test_suite(packet_serdes_test_suite);

MODULE_LICENSE("GPL");
