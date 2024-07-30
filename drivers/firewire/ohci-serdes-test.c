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

static struct kunit_case ohci_serdes_test_cases[] = {
	KUNIT_CASE(test_self_id_count_register_deserialization),
	KUNIT_CASE(test_self_id_receive_buffer_deserialization),
	{}
};

static struct kunit_suite ohci_serdes_test_suite = {
	.name = "firewire-ohci-serdes",
	.test_cases = ohci_serdes_test_cases,
};
kunit_test_suite(ohci_serdes_test_suite);

MODULE_DESCRIPTION("FireWire buffers and registers serialization/deserialization unit test suite");
MODULE_LICENSE("GPL");
