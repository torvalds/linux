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

static void tgl_setup_private_ppat(struct xe_gt *gt)
{
	/* TGL doesn't support LLC or AGE settings */
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg, GEN8_PPAT_WC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg, GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg, GEN8_PPAT_UC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(5).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(6).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(7).reg, GEN8_PPAT_WB);
}

static void pvc_setup_private_ppat(struct xe_gt *gt)
{
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, GEN8_PPAT_UC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg, GEN8_PPAT_WC);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg, GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg, GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg,
			GEN12_PPAT_CLOS(1) | GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(5).reg,
			GEN12_PPAT_CLOS(1) | GEN8_PPAT_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(6).reg,
			GEN12_PPAT_CLOS(2) | GEN8_PPAT_WT);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(7).reg,
			GEN12_PPAT_CLOS(2) | GEN8_PPAT_WB);
}

#define MTL_PPAT_L4_CACHE_POLICY_MASK   REG_GENMASK(3, 2)
#define MTL_PAT_INDEX_COH_MODE_MASK     REG_GENMASK(1, 0)
#define MTL_PPAT_3_UC   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 3)
#define MTL_PPAT_1_WT   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 1)
#define MTL_PPAT_0_WB   REG_FIELD_PREP(MTL_PPAT_L4_CACHE_POLICY_MASK, 0)
#define MTL_3_COH_2W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 3)
#define MTL_2_COH_1W    REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 2)
#define MTL_0_COH_NON   REG_FIELD_PREP(MTL_PAT_INDEX_COH_MODE_MASK, 0)

static void mtl_setup_private_ppat(struct xe_gt *gt)
{
	xe_mmio_write32(gt, GEN12_PAT_INDEX(0).reg, MTL_PPAT_0_WB);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(1).reg,
			MTL_PPAT_1_WT | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(2).reg,
			MTL_PPAT_3_UC | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(3).reg,
			MTL_PPAT_0_WB | MTL_2_COH_1W);
	xe_mmio_write32(gt, GEN12_PAT_INDEX(4).reg,
			MTL_PPAT_0_WB | MTL_3_COH_2W);
}

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE)
		mtl_setup_private_ppat(gt);
	else if (xe->info.platform == XE_PVC)
		pvc_setup_private_ppat(gt);
	else
		tgl_setup_private_ppat(gt);
}
