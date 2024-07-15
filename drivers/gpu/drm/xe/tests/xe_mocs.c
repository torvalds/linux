// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_mocs_test.h"
#include "tests/xe_pci_test.h"
#include "tests/xe_test.h"

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_mocs.h"
#include "xe_pci.h"
#include "xe_pm.h"

struct live_mocs {
	struct xe_mocs_info table;
};

static int live_mocs_init(struct live_mocs *arg, struct xe_gt *gt)
{
	unsigned int flags;
	struct kunit *test = xe_cur_kunit();

	memset(arg, 0, sizeof(*arg));

	flags = get_mocs_settings(gt_to_xe(gt), &arg->table);

	kunit_info(test, "gt %d", gt->info.id);
	kunit_info(test, "gt type %d", gt->info.type);
	kunit_info(test, "table size %d", arg->table.size);
	kunit_info(test, "table uc_index %d", arg->table.uc_index);
	kunit_info(test, "table n_entries %d", arg->table.n_entries);

	return flags;
}

static void read_l3cc_table(struct xe_gt *gt,
			    const struct xe_mocs_info *info)
{
	struct kunit *test = xe_cur_kunit();
	u32 l3cc, l3cc_expected;
	unsigned int i;
	u32 reg_val;
	u32 ret;

	ret = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "Forcewake Failed.\n");

	for (i = 0; i < info->n_entries; i++) {
		if (!(i & 1)) {
			if (regs_are_mcr(gt))
				reg_val = xe_gt_mcr_unicast_read_any(gt, XEHP_LNCFCMOCS(i >> 1));
			else
				reg_val = xe_mmio_read32(gt, XELP_LNCFCMOCS(i >> 1));

			mocs_dbg(gt, "reg_val=0x%x\n", reg_val);
		} else {
			/* Just re-use value read on previous iteration */
			reg_val >>= 16;
		}

		l3cc_expected = get_entry_l3cc(info, i);
		l3cc = reg_val & 0xffff;

		mocs_dbg(gt, "[%u] expected=0x%x actual=0x%x\n",
			 i, l3cc_expected, l3cc);

		KUNIT_EXPECT_EQ_MSG(test, l3cc_expected, l3cc,
				    "l3cc idx=%u has incorrect val.\n", i);
	}
	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

static void read_mocs_table(struct xe_gt *gt,
			    const struct xe_mocs_info *info)
{
	struct kunit *test = xe_cur_kunit();
	u32 mocs, mocs_expected;
	unsigned int i;
	u32 reg_val;
	u32 ret;

	KUNIT_EXPECT_TRUE_MSG(test, info->unused_entries_index,
			      "Unused entries index should have been defined\n");

	ret = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "Forcewake Failed.\n");

	for (i = 0; i < info->n_entries; i++) {
		if (regs_are_mcr(gt))
			reg_val = xe_gt_mcr_unicast_read_any(gt, XEHP_GLOBAL_MOCS(i));
		else
			reg_val = xe_mmio_read32(gt, XELP_GLOBAL_MOCS(i));

		mocs_expected = get_entry_control(info, i);
		mocs = reg_val;

		mocs_dbg(gt, "[%u] expected=0x%x actual=0x%x\n",
			 i, mocs_expected, mocs);

		KUNIT_EXPECT_EQ_MSG(test, mocs_expected, mocs,
				    "mocs reg 0x%x has incorrect val.\n", i);
	}

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

static int mocs_kernel_test_run_device(struct xe_device *xe)
{
	/* Basic check the system is configured with the expected mocs table */

	struct live_mocs mocs;
	struct xe_gt *gt;

	unsigned int flags;
	int id;

	xe_pm_runtime_get(xe);

	for_each_gt(gt, xe, id) {
		flags = live_mocs_init(&mocs, gt);
		if (flags & HAS_GLOBAL_MOCS)
			read_mocs_table(gt, &mocs.table);
		if (flags & HAS_LNCF_MOCS)
			read_l3cc_table(gt, &mocs.table);
	}

	xe_pm_runtime_put(xe);

	return 0;
}

void xe_live_mocs_kernel_kunit(struct kunit *test)
{
	xe_call_for_each_device(mocs_kernel_test_run_device);
}
EXPORT_SYMBOL_IF_KUNIT(xe_live_mocs_kernel_kunit);

static int mocs_reset_test_run_device(struct xe_device *xe)
{
	/* Check the mocs setup is retained over GT reset */

	struct live_mocs mocs;
	struct xe_gt *gt;
	unsigned int flags;
	int id;
	struct kunit *test = xe_cur_kunit();

	xe_pm_runtime_get(xe);

	for_each_gt(gt, xe, id) {
		flags = live_mocs_init(&mocs, gt);
		kunit_info(test, "mocs_reset_test before reset\n");
		if (flags & HAS_GLOBAL_MOCS)
			read_mocs_table(gt, &mocs.table);
		if (flags & HAS_LNCF_MOCS)
			read_l3cc_table(gt, &mocs.table);

		xe_gt_reset_async(gt);
		flush_work(&gt->reset.worker);

		kunit_info(test, "mocs_reset_test after reset\n");
		if (flags & HAS_GLOBAL_MOCS)
			read_mocs_table(gt, &mocs.table);
		if (flags & HAS_LNCF_MOCS)
			read_l3cc_table(gt, &mocs.table);
	}

	xe_pm_runtime_put(xe);

	return 0;
}

void xe_live_mocs_reset_kunit(struct kunit *test)
{
	xe_call_for_each_device(mocs_reset_test_run_device);
}
EXPORT_SYMBOL_IF_KUNIT(xe_live_mocs_reset_kunit);
