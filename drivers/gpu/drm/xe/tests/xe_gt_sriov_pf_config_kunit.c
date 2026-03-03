// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"

#define TEST_MAX_VFS	63
#define TEST_VRAM	0x37a800000ull

static void pf_set_admin_mode(struct xe_device *xe, bool enable)
{
	/* should match logic of xe_sriov_pf_admin_only() */
	xe->sriov.pf.admin_only = enable;
	KUNIT_EXPECT_EQ(kunit_get_current_test(), enable, xe_sriov_pf_admin_only(xe));
}

static void pf_set_usable_vram(struct xe_device *xe, u64 usable)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tile);
	xe->mem.vram->usable_size = usable;
	tile->mem.vram->usable_size = usable;
	KUNIT_ASSERT_EQ(test, usable, xe_vram_region_usable_size(tile->mem.vram));
}

static const void *num_vfs_gen_param(struct kunit *test, const void *prev, char *desc)
{
	unsigned long next = 1 + (unsigned long)prev;

	if (next > TEST_MAX_VFS)
		return NULL;
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%lu VF%s",
		 next, str_plural(next));
	return (void *)next;
}

static int pf_gt_config_test_init(struct kunit *test)
{
	struct xe_pci_fake_data fake = {
		.sriov_mode = XE_SRIOV_MODE_PF,
		.platform = XE_BATTLEMAGE, /* any random DGFX platform with SR-IOV */
		.subplatform = XE_SUBPLATFORM_NONE,
		.graphics_verx100 = 2001,
	};
	struct xe_vram_region *vram;
	struct xe_device *xe;
	struct xe_gt *gt;

	test->priv = &fake;
	xe_kunit_helper_xe_device_test_init(test);

	xe = test->priv;
	KUNIT_ASSERT_TRUE(test, IS_SRIOV_PF(xe));

	gt = xe_root_mmio_gt(xe);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gt);
	test->priv = gt;

	/* pretend it has some VRAM */
	KUNIT_ASSERT_TRUE(test, IS_DGFX(xe));
	vram = kunit_kzalloc(test, sizeof(*vram), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vram);
	vram->usable_size = TEST_VRAM;
	xe->mem.vram = vram;
	xe->tiles[0].mem.vram = vram;

	/* pretend we have a valid LMTT */
	KUNIT_ASSERT_TRUE(test, xe_device_has_lmtt(xe));
	KUNIT_ASSERT_GE(test, GRAPHICS_VERx100(xe), 1260);
	xe->tiles[0].sriov.pf.lmtt.ops = &lmtt_ml_ops;

	/* pretend it can support up to 63 VFs */
	xe->sriov.pf.device_total_vfs = TEST_MAX_VFS;
	xe->sriov.pf.driver_max_vfs = TEST_MAX_VFS;
	KUNIT_ASSERT_EQ(test, xe_sriov_pf_get_totalvfs(xe), 63);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_EQ(test, xe_sriov_init(xe), 0);

	/* more sanity checks */
	KUNIT_EXPECT_EQ(test, GUC_ID_MAX + 1, SZ_64K);
	KUNIT_EXPECT_EQ(test, GUC_NUM_DOORBELLS, SZ_256);

	return 0;
}

static void fair_contexts_1vf(struct kunit *test)
{
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, SZ_32K, pf_profile_fair_ctxs(gt, 1));

	pf_set_admin_mode(xe, true);
	KUNIT_ASSERT_TRUE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, SZ_64K - SZ_1K, pf_profile_fair_ctxs(gt, 1));
}

static void fair_contexts(struct kunit *test)
{
	unsigned int num_vfs = (unsigned long)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));

	KUNIT_EXPECT_TRUE(test, is_power_of_2(pf_profile_fair_ctxs(gt, num_vfs)));
	KUNIT_EXPECT_GT(test, GUC_ID_MAX, num_vfs * pf_profile_fair_ctxs(gt, num_vfs));

	if (num_vfs > 31)
		KUNIT_ASSERT_EQ(test, SZ_1K, pf_profile_fair_ctxs(gt, num_vfs));
	else if (num_vfs > 15)
		KUNIT_ASSERT_EQ(test, SZ_2K, pf_profile_fair_ctxs(gt, num_vfs));
	else if (num_vfs > 7)
		KUNIT_ASSERT_EQ(test, SZ_4K, pf_profile_fair_ctxs(gt, num_vfs));
	else if (num_vfs > 3)
		KUNIT_ASSERT_EQ(test, SZ_8K, pf_profile_fair_ctxs(gt, num_vfs));
	else if (num_vfs > 1)
		KUNIT_ASSERT_EQ(test, SZ_16K, pf_profile_fair_ctxs(gt, num_vfs));
	else
		KUNIT_ASSERT_EQ(test, SZ_32K, pf_profile_fair_ctxs(gt, num_vfs));
}

static void fair_doorbells_1vf(struct kunit *test)
{
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, 128, pf_profile_fair_dbs(gt, 1));

	pf_set_admin_mode(xe, true);
	KUNIT_ASSERT_TRUE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, 240, pf_profile_fair_dbs(gt, 1));
}

static void fair_doorbells(struct kunit *test)
{
	unsigned int num_vfs = (unsigned long)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));

	KUNIT_EXPECT_TRUE(test, is_power_of_2(pf_profile_fair_dbs(gt, num_vfs)));
	KUNIT_EXPECT_GE(test, GUC_NUM_DOORBELLS, (num_vfs + 1) * pf_profile_fair_dbs(gt, num_vfs));

	if (num_vfs > 31)
		KUNIT_ASSERT_EQ(test, SZ_4, pf_profile_fair_dbs(gt, num_vfs));
	else if (num_vfs > 15)
		KUNIT_ASSERT_EQ(test, SZ_8, pf_profile_fair_dbs(gt, num_vfs));
	else if (num_vfs > 7)
		KUNIT_ASSERT_EQ(test, SZ_16, pf_profile_fair_dbs(gt, num_vfs));
	else if (num_vfs > 3)
		KUNIT_ASSERT_EQ(test, SZ_32, pf_profile_fair_dbs(gt, num_vfs));
	else if (num_vfs > 1)
		KUNIT_ASSERT_EQ(test, SZ_64, pf_profile_fair_dbs(gt, num_vfs));
	else
		KUNIT_ASSERT_EQ(test, SZ_128, pf_profile_fair_dbs(gt, num_vfs));
}

static void fair_ggtt_1vf(struct kunit *test)
{
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, SZ_2G, pf_profile_fair_ggtt(gt, 1));

	pf_set_admin_mode(xe, true);
	KUNIT_ASSERT_TRUE(test, xe_sriov_pf_admin_only(xe));
	KUNIT_EXPECT_EQ(test, SZ_2G + SZ_1G + SZ_512M, pf_profile_fair_ggtt(gt, 1));
}

static void fair_ggtt(struct kunit *test)
{
	unsigned int num_vfs = (unsigned long)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);
	u64 alignment = pf_get_ggtt_alignment(gt);
	u64 shareable = SZ_2G + SZ_1G + SZ_512M;

	pf_set_admin_mode(xe, false);
	KUNIT_ASSERT_FALSE(test, xe_sriov_pf_admin_only(xe));

	KUNIT_EXPECT_TRUE(test, IS_ALIGNED(pf_profile_fair_ggtt(gt, num_vfs), alignment));
	KUNIT_EXPECT_GE(test, shareable, num_vfs * pf_profile_fair_ggtt(gt, num_vfs));

	if (num_vfs > 56)
		KUNIT_ASSERT_EQ(test, SZ_64M - SZ_8M, pf_profile_fair_ggtt(gt, num_vfs));
	else if (num_vfs > 28)
		KUNIT_ASSERT_EQ(test, SZ_64M, pf_profile_fair_ggtt(gt, num_vfs));
	else if (num_vfs > 14)
		KUNIT_ASSERT_EQ(test, SZ_128M, pf_profile_fair_ggtt(gt, num_vfs));
	else if (num_vfs > 7)
		KUNIT_ASSERT_EQ(test, SZ_256M, pf_profile_fair_ggtt(gt, num_vfs));
	else if (num_vfs > 3)
		KUNIT_ASSERT_EQ(test, SZ_512M, pf_profile_fair_ggtt(gt, num_vfs));
	else if (num_vfs > 1)
		KUNIT_ASSERT_EQ(test, SZ_1G, pf_profile_fair_ggtt(gt, num_vfs));
	else
		KUNIT_ASSERT_EQ(test, SZ_2G, pf_profile_fair_ggtt(gt, num_vfs));
}

static const u64 vram_sizes[] = {
	SZ_4G - SZ_512M,
	SZ_8G + SZ_4G - SZ_512M,
	SZ_16G - SZ_512M,
	SZ_32G - SZ_512M,
	SZ_64G - SZ_512M,
	TEST_VRAM,
};

static void u64_param_get_desc(const u64 *p, char *desc)
{
	string_get_size(*p, 1, STRING_UNITS_2, desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(vram_size, vram_sizes, u64_param_get_desc);

static void fair_vram_1vf(struct kunit *test)
{
	const u64 usable = *(const u64 *)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, false);
	pf_set_usable_vram(xe, usable);

	KUNIT_EXPECT_NE(test, 0, pf_profile_fair_lmem(gt, 1));
	KUNIT_EXPECT_GE(test, usable, pf_profile_fair_lmem(gt, 1));
	KUNIT_EXPECT_TRUE(test, is_power_of_2(pf_profile_fair_lmem(gt, 1)));
	KUNIT_EXPECT_GE(test, usable - pf_profile_fair_lmem(gt, 1), pf_profile_fair_lmem(gt, 1));
}

static void fair_vram_1vf_admin_only(struct kunit *test)
{
	const u64 usable = *(const u64 *)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);

	pf_set_admin_mode(xe, true);
	pf_set_usable_vram(xe, usable);

	KUNIT_EXPECT_NE(test, 0, pf_profile_fair_lmem(gt, 1));
	KUNIT_EXPECT_GE(test, usable, pf_profile_fair_lmem(gt, 1));
	KUNIT_EXPECT_LT(test, usable - pf_profile_fair_lmem(gt, 1), pf_profile_fair_lmem(gt, 1));
	KUNIT_EXPECT_TRUE(test, IS_ALIGNED(pf_profile_fair_lmem(gt, 1), SZ_1G));
}

static void fair_vram(struct kunit *test)
{
	unsigned int num_vfs = (unsigned long)test->param_value;
	struct xe_gt *gt = test->priv;
	struct xe_device *xe = gt_to_xe(gt);
	u64 alignment = pf_get_lmem_alignment(gt);
	char size[10];

	pf_set_admin_mode(xe, false);

	string_get_size(pf_profile_fair_lmem(gt, num_vfs), 1, STRING_UNITS_2, size, sizeof(size));
	kunit_info(test, "fair %s %llx\n", size, pf_profile_fair_lmem(gt, num_vfs));

	KUNIT_EXPECT_TRUE(test, is_power_of_2(pf_profile_fair_lmem(gt, num_vfs)));
	KUNIT_EXPECT_TRUE(test, IS_ALIGNED(pf_profile_fair_lmem(gt, num_vfs), alignment));
	KUNIT_EXPECT_GE(test, TEST_VRAM, num_vfs * pf_profile_fair_lmem(gt, num_vfs));
}

static struct kunit_case pf_gt_config_test_cases[] = {
	KUNIT_CASE(fair_contexts_1vf),
	KUNIT_CASE(fair_doorbells_1vf),
	KUNIT_CASE(fair_ggtt_1vf),
	KUNIT_CASE_PARAM(fair_vram_1vf, vram_size_gen_params),
	KUNIT_CASE_PARAM(fair_vram_1vf_admin_only, vram_size_gen_params),
	KUNIT_CASE_PARAM(fair_contexts, num_vfs_gen_param),
	KUNIT_CASE_PARAM(fair_doorbells, num_vfs_gen_param),
	KUNIT_CASE_PARAM(fair_ggtt, num_vfs_gen_param),
	KUNIT_CASE_PARAM(fair_vram, num_vfs_gen_param),
	{}
};

static struct kunit_suite pf_gt_config_suite = {
	.name = "pf_gt_config",
	.test_cases = pf_gt_config_test_cases,
	.init = pf_gt_config_test_init,
};

kunit_test_suite(pf_gt_config_suite);
