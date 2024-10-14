// SPDX-License-Identifier: GPL-2.0-or-later
//
// ohci-serdes-test.c - An application of Kunit to check serialization/deserialization of data in
//			buffers and registers defined in 1394 OHCI specification.
//
// Copyright (c) 2024 Takashi Sakamoto

#include <kunit/test.h>

#include "ohci.h"


static void test_self_id_count_register_deserialization(struct kunit *test)
{
	const u32 expected = 0x803d0594;

	bool is_error = ohci1394_self_id_count_is_error(expected);
	u8 generation = ohci1394_self_id_count_get_generation(expected);
	u32 size = ohci1394_self_id_count_get_size(expected);

	KUNIT_EXPECT_TRUE(test, is_error);
	KUNIT_EXPECT_EQ(test, 0x3d, generation);
	KUNIT_EXPECT_EQ(test, 0x165, size);
}

static void test_self_id_receive_buffer_deserialization(struct kunit *test)
{
	const u32 buffer[] = {
		0x0006f38b,
		0x807fcc56,
		0x7f8033a9,
		0x8145cc5e,
		0x7eba33a1,
	};

	u8 generation = ohci1394_self_id_receive_q0_get_generation(buffer[0]);
	u16 timestamp = ohci1394_self_id_receive_q0_get_timestamp(buffer[0]);

	KUNIT_EXPECT_EQ(test, 0x6, generation);
	KUNIT_EXPECT_EQ(test, 0xf38b, timestamp);
}

static void test_at_data_serdes(struct kunit *test)
{
	static const __le32 expected[] = {
		cpu_to_le32(0x00020e80),
		cpu_to_le32(0xffc2ffff),
		cpu_to_le32(0xe0000000),
	};
	__le32 quadlets[] = {0, 0, 0};
	bool has_src_bus_id = ohci1394_at_data_get_src_bus_id(expected);
	unsigned int speed = ohci1394_at_data_get_speed(expected);
	unsigned int tlabel = ohci1394_at_data_get_tlabel(expected);
	unsigned int retry = ohci1394_at_data_get_retry(expected);
	unsigned int tcode = ohci1394_at_data_get_tcode(expected);
	unsigned int destination_id = ohci1394_at_data_get_destination_id(expected);
	u64 destination_offset = ohci1394_at_data_get_destination_offset(expected);

	KUNIT_EXPECT_FALSE(test, has_src_bus_id);
	KUNIT_EXPECT_EQ(test, 0x02, speed);
	KUNIT_EXPECT_EQ(test, 0x03, tlabel);
	KUNIT_EXPECT_EQ(test, 0x02, retry);
	KUNIT_EXPECT_EQ(test, 0x08, tcode);

	ohci1394_at_data_set_src_bus_id(quadlets, has_src_bus_id);
	ohci1394_at_data_set_speed(quadlets, speed);
	ohci1394_at_data_set_tlabel(quadlets, tlabel);
	ohci1394_at_data_set_retry(quadlets, retry);
	ohci1394_at_data_set_tcode(quadlets, tcode);
	ohci1394_at_data_set_destination_id(quadlets, destination_id);
	ohci1394_at_data_set_destination_offset(quadlets, destination_offset);

	KUNIT_EXPECT_MEMEQ(test, quadlets, expected, sizeof(expected));
}

static void test_it_data_serdes(struct kunit *test)
{
	static const __le32 expected[] = {
		cpu_to_le32(0x000349a7),
		cpu_to_le32(0x02300000),
	};
	__le32 quadlets[] = {0, 0};
	unsigned int scode = ohci1394_it_data_get_speed(expected);
	unsigned int tag = ohci1394_it_data_get_tag(expected);
	unsigned int channel = ohci1394_it_data_get_channel(expected);
	unsigned int tcode = ohci1394_it_data_get_tcode(expected);
	unsigned int sync = ohci1394_it_data_get_sync(expected);
	unsigned int data_length = ohci1394_it_data_get_data_length(expected);

	KUNIT_EXPECT_EQ(test, 0x03, scode);
	KUNIT_EXPECT_EQ(test, 0x01, tag);
	KUNIT_EXPECT_EQ(test, 0x09, channel);
	KUNIT_EXPECT_EQ(test, 0x0a, tcode);
	KUNIT_EXPECT_EQ(test, 0x7, sync);
	KUNIT_EXPECT_EQ(test, 0x0230, data_length);

	ohci1394_it_data_set_speed(quadlets, scode);
	ohci1394_it_data_set_tag(quadlets, tag);
	ohci1394_it_data_set_channel(quadlets, channel);
	ohci1394_it_data_set_tcode(quadlets, tcode);
	ohci1394_it_data_set_sync(quadlets, sync);
	ohci1394_it_data_set_data_length(quadlets, data_length);

	KUNIT_EXPECT_MEMEQ(test, quadlets, expected, sizeof(expected));
}

static struct kunit_case ohci_serdes_test_cases[] = {
	KUNIT_CASE(test_self_id_count_register_deserialization),
	KUNIT_CASE(test_self_id_receive_buffer_deserialization),
	KUNIT_CASE(test_at_data_serdes),
	KUNIT_CASE(test_it_data_serdes),
	{}
};

static struct kunit_suite ohci_serdes_test_suite = {
	.name = "firewire-ohci-serdes",
	.test_cases = ohci_serdes_test_cases,
};
kunit_test_suite(ohci_serdes_test_suite);

MODULE_DESCRIPTION("FireWire buffers and registers serialization/deserialization unit test suite");
MODULE_LICENSE("GPL");
