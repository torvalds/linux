// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_pat.h"

#include "regs/xe_reg_defs.h"
#include "xe_gt.h"
#include "xe_mmio.h"

#define GEN12_PAT_INDEX(index)			_MMIO(0x4800 + (index) * 4)

#define GEN8_PPAT_WB			(3<<0)
#define GEN8_PPAT_WT			(2<<0)
#define GEN8_PPAT_WC			(1<<0)
#define GEN8_PPAT_UC			(0<<0)
#define GEN12_PPAT_CLOS(x)              ((x)<<2)

const u32 tgl_pat_table[] = {
	[0] = GEN8_PPAT_WB,
	[1] = GEN8_PPAT_WC,
	[2] = GEN8_PPAT_WT,
	[3] = GEN8_PPAT_UC,
	[4] = GEN8_PPAT_WB,
	[5] = GEN8_PPAT_WB,
	[6] = GEN8_PPAT_WB,
	[7] = GEN8_PPAT_WB,
};

const u32 pvc_pat_table[] = {
	[0] = GEN8_PPAT_UC,
	[1] = GEN8_PPAT_WC,
	[2] = GEN8_PPAT_WT,
	[3] = GEN8_PPAT_WB,
	[4] = GEN12_PPAT_CLOS(1) | GEN8_PPAT_WT,
	[5] = GEN12_PPAT_CLOS(1) | GEN8_PPAT_WB,
	[6] = GEN12_PPAT_CLOS(2) | GEN8_PPAT_WT,
	[7] = GEN12_PPAT_CLOS(2) | GEN8_PPAT_WB,
};

#define MTL_PPAT_L4_CACHE_POLICY_MASK   REG_GENMASK(3, 2)
#define MTL_PAT_INDEX_COH_MODE_MASK     REG_GENMASK(1, 0)
#define MTL_PPAT_3_UC   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 3)
#define MTL_PPAT_1_WT   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 1)
#define MTL_PPAT_0_WB   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 0)
#define MTL_3_COH_2W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 3)
#define MTL_2_COH_1W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 2)
#define MTL_0_COH_NON   REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 0)

const u32 mtl_pat_table[] = {
	[0] = MTL_PPAT_0_WB,
	[1] = MTL_PPAT_1_WT | MTL_2_COH_1W,
	[2] = MTL_PPAT_3_UC | MTL_2_COH_1W,
	[3] = MTL_PPAT_0_WB | MTL_2_COH_1W,
	[4] = MTL_PPAT_0_WB | MTL_3_COH_2W,
};

#define PROGRAM_PAT_UNICAST(gt, table) do { \
	for (int i = 0; i < ARRAY_SIZE(table); i++) \
		xe_mmio_write32(gt, GEN12_PAT_INDEX(i).reg, table[i]); \
} while (0)

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE)
		PROGRAM_PAT_UNICAST(gt, mtl_pat_table);
	else if (xe->info.platform == XE_PVC)
		PROGRAM_PAT_UNICAST(gt, pvc_pat_table);
	else
		PROGRAM_PAT_UNICAST(gt, tgl_pat_table);
}
