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

#define MTL_L4_POLICY_MASK			REG_GENMASK(3, 2)
#define MTL_PAT_3_UC				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 3)
#define MTL_PAT_1_WT				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 1)
#define MTL_PAT_0_WB				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 0)
#define MTL_INDEX_COH_MODE_MASK			REG_GENMASK(1, 0)
#define MTL_3_COH_2W				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 3)
#define MTL_2_COH_1W				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 2)
#define MTL_0_COH_NON				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 0)

#define PVC_CLOS_LEVEL_MASK			REG_GENMASK(3, 2)
#define PVC_PAT_CLOS(x)				REG_FIELD_PREP(PVC_CLOS_LEVEL_MASK, x)

#define TGL_MEM_TYPE_MASK			REG_GENMASK(1, 0)
#define TGL_PAT_WB				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 3)
#define TGL_PAT_WT				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 2)
#define TGL_PAT_WC				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 1)
#define TGL_PAT_UC				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 0)

static const u32 tgl_pat_table[] = {
	[0] = TGL_PAT_WB,
	[1] = TGL_PAT_WC,
	[2] = TGL_PAT_WT,
	[3] = TGL_PAT_UC,
	[4] = TGL_PAT_WB,
	[5] = TGL_PAT_WB,
	[6] = TGL_PAT_WB,
	[7] = TGL_PAT_WB,
};

static const u32 pvc_pat_table[] = {
	[0] = TGL_PAT_UC,
	[1] = TGL_PAT_WC,
	[2] = TGL_PAT_WT,
	[3] = TGL_PAT_WB,
	[4] = PVC_PAT_CLOS(1) | TGL_PAT_WT,
	[5] = PVC_PAT_CLOS(1) | TGL_PAT_WB,
	[6] = PVC_PAT_CLOS(2) | TGL_PAT_WT,
	[7] = PVC_PAT_CLOS(2) | TGL_PAT_WB,
};

static const u32 mtl_pat_table[] = {
	[0] = MTL_PAT_0_WB,
	[1] = MTL_PAT_1_WT,
	[2] = MTL_PAT_3_UC,
	[3] = MTL_PAT_0_WB | MTL_2_COH_1W,
	[4] = MTL_PAT_0_WB | MTL_3_COH_2W,
};

#define PROGRAM_PAT_UNICAST(gt, table) do { \
	for (int i = 0; i < ARRAY_SIZE(table); i++) \
		xe_mmio_write32(gt, _PAT_INDEX(i), table[i]); \
} while (0)

#define PROGRAM_PAT_MCR(gt, table) do { \
	for (int i = 0; i < ARRAY_SIZE(table); i++) \
		xe_gt_mcr_multicast_write(gt, MCR_REG(_PAT_INDEX(i)), table[i]); \
} while (0)

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE) {
		if (xe_gt_is_media_type(gt))
			PROGRAM_PAT_UNICAST(gt, mtl_pat_table);
		else
			PROGRAM_PAT_MCR(gt, mtl_pat_table);
	} else if (xe->info.platform == XE_PVC) {
		PROGRAM_PAT_MCR(gt, pvc_pat_table);
	} else if (xe->info.platform == XE_DG2) {
		PROGRAM_PAT_MCR(gt, pvc_pat_table);
	} else if (GRAPHICS_VERx100(xe) <= 1210) {
		PROGRAM_PAT_UNICAST(gt, tgl_pat_table);
	} else {
		/*
		 * Going forward we expect to need new PAT settings for most
		 * new platforms; failure to provide a new table can easily
		 * lead to subtle, hard-to-debug problems.  If none of the
		 * conditions above match the platform we're running on we'll
		 * raise an error rather than trying to silently inherit the
		 * most recent platform's behavior.
		 */
		drm_err(&xe->drm, "Missing PAT table for platform with graphics version %d.%2d!\n",
			GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
	}
}
