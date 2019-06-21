// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_gt.h"
#include "intel_gt_pm.h"

void intel_gt_init_early(struct intel_gt *gt, struct drm_i915_private *i915)
{
	gt->i915 = i915;
	gt->uncore = &i915->uncore;

	INIT_LIST_HEAD(&gt->active_rings);
	INIT_LIST_HEAD(&gt->closed_vma);

	spin_lock_init(&gt->closed_lock);

	intel_gt_pm_init_early(gt);
}
