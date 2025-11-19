// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.
/* This file is intended to be included into mpam_devices.c */

#include <kunit/test.h>

static void test_mpam_reset_msc_bitmap(struct kunit *test)
{
	char __iomem *buf = kunit_kzalloc(test, SZ_16K, GFP_KERNEL);
	struct mpam_msc fake_msc = {};
	u32 *test_result;

	if (!buf)
		return;

	fake_msc.mapped_hwpage = buf;
	fake_msc.mapped_hwpage_sz = SZ_16K;
	cpumask_copy(&fake_msc.accessibility, cpu_possible_mask);

	/* Satisfy lockdep checks */
	mutex_init(&fake_msc.part_sel_lock);
	mutex_lock(&fake_msc.part_sel_lock);

	test_result = (u32 *)(buf + MPAMCFG_CPBM);

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 0);
	KUNIT_EXPECT_EQ(test, test_result[0], 0);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 1);
	KUNIT_EXPECT_EQ(test, test_result[0], 1);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 16);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 32);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffffffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 33);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffffffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 1);
	test_result[0] = 0;
	test_result[1] = 0;

	mutex_unlock(&fake_msc.part_sel_lock);
}

static struct kunit_case mpam_devices_test_cases[] = {
	KUNIT_CASE(test_mpam_reset_msc_bitmap),
	{}
};

static struct kunit_suite mpam_devices_test_suite = {
	.name = "mpam_devices_test_suite",
	.test_cases = mpam_devices_test_cases,
};

kunit_test_suites(&mpam_devices_test_suite);
