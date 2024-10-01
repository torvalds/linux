// SPDX-License-Identifier: GPL-2.0-only
//
// uapi_test.c - An application of Kunit to check layout of structures exposed to user space for
//		 FireWire subsystem.
//
// Copyright (c) 2023 Takashi Sakamoto

#include <kunit/test.h>
#include <linux/firewire-cdev.h>

// Known issue added at v2.6.27 kernel.
static void structure_layout_event_response(struct kunit *test)
{
#if defined(CONFIG_X86_32)
	// 4 bytes alignment for aggregate type including 8 bytes storage types.
	KUNIT_EXPECT_EQ(test, 20, sizeof(struct fw_cdev_event_response));
#else
	// 8 bytes alignment for aggregate type including 8 bytes storage types.
	KUNIT_EXPECT_EQ(test, 24, sizeof(struct fw_cdev_event_response));
#endif

	KUNIT_EXPECT_EQ(test, 0, offsetof(struct fw_cdev_event_response, closure));
	KUNIT_EXPECT_EQ(test, 8, offsetof(struct fw_cdev_event_response, type));
	KUNIT_EXPECT_EQ(test, 12, offsetof(struct fw_cdev_event_response, rcode));
	KUNIT_EXPECT_EQ(test, 16, offsetof(struct fw_cdev_event_response, length));
	KUNIT_EXPECT_EQ(test, 20, offsetof(struct fw_cdev_event_response, data));
}

// Added at v6.5.
static void structure_layout_event_request3(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 56, sizeof(struct fw_cdev_event_request3));

	KUNIT_EXPECT_EQ(test, 0, offsetof(struct fw_cdev_event_request3, closure));
	KUNIT_EXPECT_EQ(test, 8, offsetof(struct fw_cdev_event_request3, type));
	KUNIT_EXPECT_EQ(test, 12, offsetof(struct fw_cdev_event_request3, tcode));
	KUNIT_EXPECT_EQ(test, 16, offsetof(struct fw_cdev_event_request3, offset));
	KUNIT_EXPECT_EQ(test, 24, offsetof(struct fw_cdev_event_request3, source_node_id));
	KUNIT_EXPECT_EQ(test, 28, offsetof(struct fw_cdev_event_request3, destination_node_id));
	KUNIT_EXPECT_EQ(test, 32, offsetof(struct fw_cdev_event_request3, card));
	KUNIT_EXPECT_EQ(test, 36, offsetof(struct fw_cdev_event_request3, generation));
	KUNIT_EXPECT_EQ(test, 40, offsetof(struct fw_cdev_event_request3, handle));
	KUNIT_EXPECT_EQ(test, 44, offsetof(struct fw_cdev_event_request3, length));
	KUNIT_EXPECT_EQ(test, 48, offsetof(struct fw_cdev_event_request3, tstamp));
	KUNIT_EXPECT_EQ(test, 56, offsetof(struct fw_cdev_event_request3, data));
}

// Added at v6.5.
static void structure_layout_event_response2(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 32, sizeof(struct fw_cdev_event_response2));

	KUNIT_EXPECT_EQ(test, 0, offsetof(struct fw_cdev_event_response2, closure));
	KUNIT_EXPECT_EQ(test, 8, offsetof(struct fw_cdev_event_response2, type));
	KUNIT_EXPECT_EQ(test, 12, offsetof(struct fw_cdev_event_response2, rcode));
	KUNIT_EXPECT_EQ(test, 16, offsetof(struct fw_cdev_event_response2, length));
	KUNIT_EXPECT_EQ(test, 20, offsetof(struct fw_cdev_event_response2, request_tstamp));
	KUNIT_EXPECT_EQ(test, 24, offsetof(struct fw_cdev_event_response2, response_tstamp));
	KUNIT_EXPECT_EQ(test, 32, offsetof(struct fw_cdev_event_response2, data));
}

// Added at v6.5.
static void structure_layout_event_phy_packet2(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 24, sizeof(struct fw_cdev_event_phy_packet2));

	KUNIT_EXPECT_EQ(test, 0, offsetof(struct fw_cdev_event_phy_packet2, closure));
	KUNIT_EXPECT_EQ(test, 8, offsetof(struct fw_cdev_event_phy_packet2, type));
	KUNIT_EXPECT_EQ(test, 12, offsetof(struct fw_cdev_event_phy_packet2, rcode));
	KUNIT_EXPECT_EQ(test, 16, offsetof(struct fw_cdev_event_phy_packet2, length));
	KUNIT_EXPECT_EQ(test, 20, offsetof(struct fw_cdev_event_phy_packet2, tstamp));
	KUNIT_EXPECT_EQ(test, 24, offsetof(struct fw_cdev_event_phy_packet2, data));
}

static struct kunit_case structure_layout_test_cases[] = {
	KUNIT_CASE(structure_layout_event_response),
	KUNIT_CASE(structure_layout_event_request3),
	KUNIT_CASE(structure_layout_event_response2),
	KUNIT_CASE(structure_layout_event_phy_packet2),
	{}
};

static struct kunit_suite structure_layout_test_suite = {
	.name = "firewire-uapi-structure-layout",
	.test_cases = structure_layout_test_cases,
};
kunit_test_suite(structure_layout_test_suite);

MODULE_DESCRIPTION("FireWire UAPI unit test suite");
MODULE_LICENSE("GPL");
