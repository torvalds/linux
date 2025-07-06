// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "i915_drv.h"
#include "gt/intel_gt.h"
#include "gt/intel_sa_media.h"

int intel_sa_mediagt_setup(struct intel_gt *gt, phys_addr_t phys_addr,
			   u32 gsi_offset)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore;

	uncore = drmm_kzalloc(&i915->drm, sizeof(*uncore), GFP_KERNEL);
	if (!uncore)
		return -ENOMEM;

	uncore->gsi_offset = gsi_offset;

	gt->irq_lock = to_gt(i915)->irq_lock;
	intel_gt_common_init_early(gt);
	intel_uncore_init_early(uncore, gt);

	/*
	 * Standalone media shares the general MMIO space with the primary
	 * GT.  We'll reuse the primary GT's mapping.
	 */
	uncore->regs = intel_uncore_regs(&i915->uncore);
	if (drm_WARN_ON(&i915->drm, uncore->regs == NULL))
		return -EIO;

	gt->uncore = uncore;
	gt->phys_addr = phys_addr;

	/*
	 * For current platforms we can assume there's only a single
	 * media GT and cache it for quick lookup.
	 */
	drm_WARN_ON(&i915->drm, i915->media_gt);
	i915->media_gt = gt;

	return 0;
}
