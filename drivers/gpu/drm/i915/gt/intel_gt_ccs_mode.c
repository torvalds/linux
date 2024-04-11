// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_ccs_mode.h"
#include "intel_gt_regs.h"

void intel_gt_apply_ccs_mode(struct intel_gt *gt)
{
	int cslice;
	u32 mode = 0;
	int first_ccs = __ffs(CCS_MASK(gt));

	if (!IS_DG2(gt->i915))
		return;

	/* Build the value for the fixed CCS load balancing */
	for (cslice = 0; cslice < I915_MAX_CCS; cslice++) {
		if (CCS_MASK(gt) & BIT(cslice))
			/*
			 * If available, assign the cslice
			 * to the first available engine...
			 */
			mode |= XEHP_CCS_MODE_CSLICE(cslice, first_ccs);

		else
			/*
			 * ... otherwise, mark the cslice as
			 * unavailable if no CCS dispatches here
			 */
			mode |= XEHP_CCS_MODE_CSLICE(cslice,
						     XEHP_CCS_MODE_CSLICE_MASK);
	}

	intel_uncore_write(gt->uncore, XEHP_CCS_MODE, mode);
}
