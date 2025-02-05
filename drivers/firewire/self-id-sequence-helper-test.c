// SPDX-License-Identifier: GPL-2.0-or-later
//
// self-id-sequence-helper-test.c - An application of Kunit to test helpers of self ID sequence.
//
// Copyright (c) 2024 Takashi Sakamoto

#include <kunit/test.h>

#include "phy-packet-definitions.h"

static void test_self_id_sequence_enumerator_valid(struct kunit *test)
{
	static const u32 valid_sequences[] = {
		0x00000000,
		0x00000001, 0x00800000,
		0x00000001, 0x00800001, 0x00900000,
		0x00000000,
	};
	struct self_id_sequence_enumerator enumerator;
	const u32 *entry;
	unsigned int quadlet_count;

	enumerator.cursor = valid_sequences;
	enumerator.quadlet_count = ARRAY_SIZE(valid_sequences);

	entry = self_id_sequence_enumerator_next(&enumerator, &quadlet_count);
	KUNIT_EXPECT_PTR_EQ(test, entry, &valid_sequences[0]);
	KUNIT_EXPECT_EQ(test, quadlet_count, 1);
	KUNIT_EXPECT_EQ(test, enumerator.quadlet_count, 6);

	entry = self_id_sequence_enumerator_next(&enumerator, &quadlet_count);
	KUNIT_EXPECT_PTR_EQ(test, entry, &valid_sequences[1]);
	KUNIT_EXPECT_EQ(test, quadlet_count, 2);
	KUNIT_EXPECT_EQ(test, enumerator.quadlet_count, 4);

	entry = self_id_sequence_enumerator_next(&enumerator, &quadlet_count);
	KUNIT_EXPECT_PTR_EQ(test, entry, &valid_sequences[3]);
	KUNIT_EXPECT_EQ(test, quadlet_count, 3);
	KUNIT_EXPECT_EQ(test, enumerator.quadlet_count, 1);

	entry = self_id_sequence_enumerator_next(&enumerator, &quadlet_count);
	KUNIT_EXPECT_PTR_EQ(test, entry, &valid_sequences[6]);
	KUNIT_EXPECT_EQ(test, quadlet_count, 1);
	KUNIT_EXPECT_EQ(test, enumerator.quadlet_count, 0);

	entry = self_id_sequence_enumerator_next(&enumerator, &quadlet_count);
	KUNIT_EXPECT_EQ(test, PTR_ERR(entry), -ENODATA);
}

static void test_self_id_sequence_enumerator_invalid(struct kunit *test)
{
	static const u32 invalid_sequences[] = {
		0x00000001,
	};
	struct self_id_sequence_enumerator enumerator;
	const u32 *entry;
	unsigned int count;

	enumerator.cursor = invalid_sequences;
	enumerator.quadlet_count = ARRAY_SIZE(invalid_sequences);

	entry = self_id_sequence_enumerator_next(&enumerator, &count);
	KUNIT_EXPECT_EQ(test, PTR_ERR(entry), -EPROTO);
}

static void test_self_id_sequence_get_port_status(struct kunit *test)
{
	static const u32 expected[] = {
		0x000000e5,
		0x00839e79,
		0x0091e79d,
		0x00a279e4,
	};
	u32 quadlets [] = {
		0x00000001,
		0x00800001,
		0x00900001,
		0x00a00000,
	};
	enum phy_packet_self_id_port_status port_status[28];
	unsigned int port_capacity;
	unsigned int port_index;

	KUNIT_ASSERT_EQ(test, ARRAY_SIZE(expected), ARRAY_SIZE(quadlets));

	// With an extra port.
	port_capacity = self_id_sequence_get_port_capacity(ARRAY_SIZE(expected)) + 1;
	KUNIT_ASSERT_EQ(test, port_capacity, ARRAY_SIZE(port_status));

	for (port_index = 0; port_index < port_capacity; ++port_index) {
		port_status[port_index] =
			self_id_sequence_get_port_status(expected, ARRAY_SIZE(expected), port_index);
		self_id_sequence_set_port_status(quadlets, ARRAY_SIZE(quadlets), port_index,
						 port_status[port_index]);
	}

	// Self ID zero.
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[0]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[1]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[2]);

	// Self ID one.
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[3]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[4]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[5]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[6]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[7]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[8]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[9]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[10]);

	// Self ID two.
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[11]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[12]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[13]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[14]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[15]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[16]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[17]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[18]);

	// Self ID three.
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[19]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[20]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[21]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[22]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[23]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_CHILD, port_status[24]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_PARENT, port_status[25]);
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NCONN, port_status[26]);

	// Our of order.
	KUNIT_EXPECT_EQ(test, PHY_PACKET_SELF_ID_PORT_STATUS_NONE, port_status[27]);

	KUNIT_EXPECT_MEMEQ(test, quadlets, expected, sizeof(expected));
}

static struct kunit_case self_id_sequence_helper_test_cases[] = {
	KUNIT_CASE(test_self_id_sequence_enumerator_valid),
	KUNIT_CASE(test_self_id_sequence_enumerator_invalid),
	KUNIT_CASE(test_self_id_sequence_get_port_status),
	{}
};

static struct kunit_suite self_id_sequence_helper_test_suite = {
	.name = "self-id-sequence-helper",
	.test_cases = self_id_sequence_helper_test_cases,
};
kunit_test_suite(self_id_sequence_helper_test_suite);

MODULE_DESCRIPTION("Unit test suite for helpers of self ID sequence");
MODULE_LICENSE("GPL");
