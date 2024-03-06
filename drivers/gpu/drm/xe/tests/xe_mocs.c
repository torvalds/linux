// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_mocs_test.h"
#include "tests/xe_pci_test.h"
#include "tests/xe_test.h"

#include "xe_pci.h"
#include "xe_gt.h"
#include "xe_mocs.h"
#include "xe_device.h"

struct live_mocs {
	struct xe_mocs_info table;
};

static int live_mocs_init(struct live_mocs *arg, struct xe_gt *gt)
{
	unsigned int flags;
	struct kunit *test = xe_cur_kunit();

	memset(arg, 0, sizeof(*arg));

	flags = get_mocs_settings(gt_to_xe(gt), &arg->table);

	kunit_info(test, "table size %d", arg->table.size);
	kunit_info(test, "table uc_index %d", arg->table.uc_index);
	kunit_info(test, "table n_entries %d", arg->table.n_entries);

	return flags;
}

static void read_l3cc_table(struct xe_gt *gt,
			    const struct xe_mocs_info *info)
{
	unsigned int i;
	u32 l3cc;
	u32 reg_val;
	u32 ret;

	struct kunit *test = xe_cur_kunit();

	xe_device_mem_access_get(gt_to_xe(gt));
	ret = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "Forcewake Failed.\n");
	mocs_dbg(&gt_to_xe(gt)->drm, "L3CC entries:%d\n", info->n_entries);
	for (i = 0;
	     i < (info->n_entries + 1) / 2 ?
	     (l3cc = l3cc_combine(get_entry_l3cc(info, 2 * i),
				  get_entry_l3cc(info, 2 * i + 1))), 1 : 0;
	     i++) {
		if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1250)
			reg_val = xe_gt_mcr_unicast_read_any(gt, XEHP_LNCFCMOCS(i));
		else
			reg_val = xe_mmio_read32(gt, XELP_LNCFCMOCS(i));
		mocs_dbg(&gt_to_xe(gt)->drm, "%d 0x%x 0x%x 0x%x\n", i,
			 XELP_LNCFCMOCS(i).addr, reg_val, l3cc);
		if (reg_val != l3cc)
			KUNIT_FAIL(test, "l3cc reg 0x%x has incorrect val.\n",
				   XELP_LNCFCMOCS(i).addr);
	}
	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
	xe_device_mem_access_put(gt_to_xe(gt));
}

static void read_mocs_table(struct xe_gt *gt,
			    const struct xe_mocs_info *info)
{
	struct xe_device *xe = gt_to_xe(gt);

	unsigned int i;
	u32 mocs;
	u32 reg_val;
	u32 ret;

	struct kunit *test = xe_cur_kunit();

	xe_device_mem_access_get(gt_to_xe(gt));
	ret = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "Forcewake Failed.\n");
	mocs_dbg(&gt_to_xe(gt)->drm, "Global MOCS entries:%d\n", info->n_entries);
	drm_WARN_ONCE(&xe->drm, !info->unused_entries_index,
		      "Unused entries index should have been defined\n");
	for (i = 0;
	     i < info->n_entries ? (mocs = get_entry_control(info, i)), 1 : 0;
	     i++) {
		if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1250)
			reg_val = xe_gt_mcr_unicast_read_any(gt, XEHP_GLOBAL_MOCS(i));
		else
			reg_val = xe_mmio_read32(gt, XELP_GLOBAL_MOCS(i));
		mocs_dbg(&gt_to_xe(gt)->drm, "%d 0x%x 0x%x 0x%x\n", i,
			 XELP_GLOBAL_MOCS(i).addr, reg_val, mocs);
		if (reg_val != mocs)
			KUNIT_FAIL(test, "mocs reg 0x%x has incorrect val.\n",
				   XELP_GLOBAL_MOCS(i).addr);
	}
	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
	xe_device_mem_access_put(gt_to_xe(gt));
}

static int mocs_kernel_test_run_device(struct xe_device *xe)
{
	/* Basic check the system is configured with the expected mocs table */

	struct live_mocs mocs;
	struct xe_gt *gt;

	unsigned int flags;
	int id;

	for_each_gt(gt, xe, id) {
		flags = live_mocs_init(&mocs, gt);
		if (flags & HAS_GLOBAL_MOCS)
			read_mocs_table(gt, &mocs.table);
		if (flags & HAS_LNCF_MOCS)
			read_l3cc_table(gt, &mocs.table);
	}
	return 0;
}

void xe_live_mocs_kernel_kunit(struct kunit *test)
{
	xe_call_for_each_device(mocs_kernel_test_run_device);
}
EXPORT_SYMBOL_IF_KUNIT(xe_live_mocs_kernel_kunit);
