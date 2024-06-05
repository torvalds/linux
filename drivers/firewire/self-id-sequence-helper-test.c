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

static struct kunit_case self_id_sequence_helper_test_cases[] = {
	KUNIT_CASE(test_self_id_sequence_enumerator_valid),
	KUNIT_CASE(test_self_id_sequence_enumerator_invalid),
	{}
};

static struct kunit_suite self_id_sequence_helper_test_suite = {
	.name = "self-id-sequence-helper",
	.test_cases = self_id_sequence_helper_test_cases,
};
kunit_test_suite(self_id_sequence_helper_test_suite);

MODULE_DESCRIPTION("Unit test suite for helpers of self ID sequence");
MODULE_LICENSE("GPL");
