// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "xe_device.h"
#include "xe_guc_klv_helpers.h"
#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"

#define TEST_VF		VFID(1)

static int sriov_packet_test_init(struct kunit *test)
{
	struct xe_pci_fake_data fake = {
		.sriov_mode = XE_SRIOV_MODE_PF,
		.platform = XE_PANTHERLAKE, /* we need MEMIRQ */
		.subplatform = XE_SUBPLATFORM_NONE,
		.graphics_verx100 = 3000,
		.media_verx100 = 3000,
	};
	struct xe_device *xe;

	test->priv = &fake;
	xe_kunit_helper_xe_device_test_init(test);
	xe = test->priv;

	/* pretend we can support at least VF1 */
	xe->sriov.pf.device_total_vfs = 1;
	xe->sriov.pf.driver_max_vfs = 1;

	KUNIT_ASSERT_EQ(test, 0, xe_sriov_init(xe));
	KUNIT_ASSERT_TRUE(test, xe_sriov_pf_migration_supported(xe));

	return 0;
}

static void test_descriptor_init(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_sriov_packet **desc;

	/* note: with lock held we should avoid KUNIT_ASSERT() */
	guard(mutex)(pf_migration_mutex(xe, TEST_VF));

	KUNIT_EXPECT_EQ(test, 0, pf_descriptor_init(xe, TEST_VF));
	desc = pf_pick_descriptor(xe, TEST_VF);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, *desc);
	if (!*desc)
		return;
	KUNIT_EXPECT_NE(test, (*desc)->hdr.version, 0);
	KUNIT_EXPECT_EQ(test, (*desc)->hdr.version, XE_SRIOV_PACKET_SUPPORTED_VERSION);
	KUNIT_EXPECT_EQ(test, (*desc)->hdr.type, XE_SRIOV_PACKET_TYPE_DESCRIPTOR);
	KUNIT_EXPECT_NE(test, (*desc)->hdr.size, 0);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, (*desc)->vaddr);
	if (!(*desc)->vaddr)
		return;
	KUNIT_EXPECT_EQ(test, 0, xe_sriov_packet_process_descriptor(xe, TEST_VF, *desc));

	switch ((*desc)->hdr.version) {
	case 1:
		/* v1 is KLV based */
		KUNIT_EXPECT_TRUE(test, IS_ALIGNED((*desc)->hdr.size, sizeof(u32)));
		/* v1 has at least DEVID and REVID KLVs */
		KUNIT_EXPECT_LE(test, 2,
				xe_guc_klv_count((*desc)->vaddr,
						 (*desc)->hdr.size / sizeof(u32)));
		break;
	default:
		kunit_mark_skipped(test, "no test code for version %u\n", (*desc)->hdr.version);
		return;
	}
}

static struct kunit_case sriov_packet_test_cases[] = {
	KUNIT_CASE(test_descriptor_init),
	{}
};

static struct kunit_suite sriov_packet_suite = {
	.name = "sriov_packet",
	.test_cases = sriov_packet_test_cases,
	.init = sriov_packet_test_init,
};

kunit_test_suite(sriov_packet_suite);
