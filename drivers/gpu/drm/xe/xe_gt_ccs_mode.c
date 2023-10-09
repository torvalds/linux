// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_gt.h"
#include "xe_gt_ccs_mode.h"
#include "xe_mmio.h"

static void __xe_gt_apply_ccs_mode(struct xe_gt *gt, u32 num_engines)
{
	u32 mode = CCS_MODE_CSLICE_0_3_MASK; /* disable all by default */
	int num_slices = hweight32(CCS_MASK(gt));
	struct xe_device *xe = gt_to_xe(gt);
	int width, cslice = 0;
	u32 config = 0;

	xe_assert(xe, xe_gt_ccs_mode_enabled(gt));

	xe_assert(xe, num_engines && num_engines <= num_slices);
	xe_assert(xe, !(num_slices % num_engines));

	/*
	 * Loop over all available slices and assign each a user engine.
	 * For example, if there are four compute slices available, the
	 * assignment of compute slices to compute engines would be,
	 *
	 * With 1 engine (ccs0):
	 *   slice 0, 1, 2, 3: ccs0
	 *
	 * With 2 engines (ccs0, ccs1):
	 *   slice 0, 2: ccs0
	 *   slice 1, 3: ccs1
	 *
	 * With 4 engines (ccs0, ccs1, ccs2, ccs3):
	 *   slice 0: ccs0
	 *   slice 1: ccs1
	 *   slice 2: ccs2
	 *   slice 3: ccs3
	 */
	for (width = num_slices / num_engines; width; width--) {
		struct xe_hw_engine *hwe;
		enum xe_hw_engine_id id;

		for_each_hw_engine(hwe, gt, id) {
			if (hwe->class != XE_ENGINE_CLASS_COMPUTE)
				continue;

			if (hwe->logical_instance >= num_engines)
				break;

			config |= BIT(hwe->instance) << XE_HW_ENGINE_CCS0;

			/* If a slice is fused off, leave disabled */
			while ((CCS_MASK(gt) & BIT(cslice)) == 0)
				cslice++;

			mode &= ~CCS_MODE_CSLICE(cslice, CCS_MODE_CSLICE_MASK);
			mode |= CCS_MODE_CSLICE(cslice, hwe->instance);
			cslice++;
		}
	}

	xe_mmio_write32(gt, CCS_MODE, mode);

	xe_gt_info(gt, "CCS_MODE=%x config:%08x, num_engines:%d, num_slices:%d\n",
		   mode, config, num_engines, num_slices);
}

void xe_gt_apply_ccs_mode(struct xe_gt *gt)
{
	if (!gt->ccs_mode)
		return;

	__xe_gt_apply_ccs_mode(gt, gt->ccs_mode);
}
