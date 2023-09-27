// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_pat.h"

#include "regs/xe_reg_defs.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_mmio.h"

#define _PAT_INDEX(index)			_PICK_EVEN_2RANGES(index, 8, \
								   0x4800, 0x4804, \
								   0x4848, 0x484c)

#define XELPG_L4_POLICY_MASK			REG_GENMASK(3, 2)
#define XELPG_PAT_3_UC				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 3)
#define XELPG_PAT_1_WT				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 1)
#define XELPG_PAT_0_WB				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 0)
#define XELPG_INDEX_COH_MODE_MASK		REG_GENMASK(1, 0)
#define XELPG_3_COH_2W				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 3)
#define XELPG_2_COH_1W				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 2)
#define XELPG_0_COH_NON				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 0)

#define XEHPC_CLOS_LEVEL_MASK			REG_GENMASK(3, 2)
#define XEHPC_PAT_CLOS(x)			REG_FIELD_PREP(XEHPC_CLOS_LEVEL_MASK, x)

#define XELP_MEM_TYPE_MASK			REG_GENMASK(1, 0)
#define XELP_PAT_WB				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 3)
#define XELP_PAT_WT				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 2)
#define XELP_PAT_WC				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 1)
#define XELP_PAT_UC				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 0)

struct xe_pat_ops {
	void (*program_graphics)(struct xe_gt *gt, const u32 table[], int n_entries);
	void (*program_media)(struct xe_gt *gt, const u32 table[], int n_entries);
};

static const u32 xelp_pat_table[] = {
	[0] = XELP_PAT_WB,
	[1] = XELP_PAT_WC,
	[2] = XELP_PAT_WT,
	[3] = XELP_PAT_UC,
	[4] = XELP_PAT_WB,
	[5] = XELP_PAT_WB,
	[6] = XELP_PAT_WB,
	[7] = XELP_PAT_WB,
};

static const u32 xehpc_pat_table[] = {
	[0] = XELP_PAT_UC,
	[1] = XELP_PAT_WC,
	[2] = XELP_PAT_WT,
	[3] = XELP_PAT_WB,
	[4] = XEHPC_PAT_CLOS(1) | XELP_PAT_WT,
	[5] = XEHPC_PAT_CLOS(1) | XELP_PAT_WB,
	[6] = XEHPC_PAT_CLOS(2) | XELP_PAT_WT,
	[7] = XEHPC_PAT_CLOS(2) | XELP_PAT_WB,
};

static const u32 xelpg_pat_table[] = {
	[0] = XELPG_PAT_0_WB,
	[1] = XELPG_PAT_1_WT,
	[2] = XELPG_PAT_3_UC,
	[3] = XELPG_PAT_0_WB | XELPG_2_COH_1W,
	[4] = XELPG_PAT_0_WB | XELPG_3_COH_2W,
};

static void program_pat(struct xe_gt *gt, const u32 table[], int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg reg = XE_REG(_PAT_INDEX(i));

		xe_mmio_write32(gt, reg, table[i]);
	}
}

static void program_pat_mcr(struct xe_gt *gt, const u32 table[], int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg_mcr reg_mcr = XE_REG_MCR(_PAT_INDEX(i));

		xe_gt_mcr_multicast_write(gt, reg_mcr, table[i]);
	}
}

static const struct xe_pat_ops xelp_pat_ops = {
	.program_graphics = program_pat,
};

static const struct xe_pat_ops xehp_pat_ops = {
	.program_graphics = program_pat_mcr,
};

/*
 * SAMedia register offsets are adjusted by the write methods and they target
 * registers that are not MCR, while for normal GT they are MCR
 */
static const struct xe_pat_ops xelpg_pat_ops = {
	.program_graphics = program_pat,
	.program_media = program_pat_mcr,
};

void xe_pat_init_early(struct xe_device *xe)
{
	if (xe->info.platform == XE_METEORLAKE) {
		xe->pat.ops = &xelpg_pat_ops;
		xe->pat.table = xelpg_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelpg_pat_table);
	} else if (xe->info.platform == XE_PVC) {
		xe->pat.ops = &xehp_pat_ops;
		xe->pat.table = xehpc_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xehpc_pat_table);
	} else if (xe->info.platform == XE_DG2) {
		/*
		 * Table is the same as previous platforms, but programming
		 * method has changed.
		 */
		xe->pat.ops = &xehp_pat_ops;
		xe->pat.table = xelp_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelp_pat_table);
	} else if (GRAPHICS_VERx100(xe) <= 1210) {
		xe->pat.ops = &xelp_pat_ops;
		xe->pat.table = xelp_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelp_pat_table);
	} else {
		/*
		 * Going forward we expect to need new PAT settings for most
		 * new platforms; failure to provide a new table can easily
		 * lead to subtle, hard-to-debug problems.  If none of the
		 * conditions above match the platform we're running on we'll
		 * raise an error rather than trying to silently inherit the
		 * most recent platform's behavior.
		 */
		drm_err(&xe->drm, "Missing PAT table for platform with graphics version %d.%02d!\n",
			GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
	}
}

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (!xe->pat.ops)
		return;

	if (xe_gt_is_media_type(gt))
		xe->pat.ops->program_media(gt, xe->pat.table, xe->pat.n_entries);
	else
		xe->pat.ops->program_graphics(gt, xe->pat.table, xe->pat.n_entries);
}
