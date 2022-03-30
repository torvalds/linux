/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GT_GMCH_H__
#define __INTEL_GT_GMCH_H__

#include "intel_gtt.h"

/* For x86 platforms */
#if IS_ENABLED(CONFIG_X86)
void intel_gt_gmch_gen5_chipset_flush(struct intel_gt *gt);
int intel_gt_gmch_gen6_probe(struct i915_ggtt *ggtt);
int intel_gt_gmch_gen8_probe(struct i915_ggtt *ggtt);
int intel_gt_gmch_gen5_probe(struct i915_ggtt *ggtt);
int intel_gt_gmch_gen5_enable_hw(struct drm_i915_private *i915);

/* Stubs for non-x86 platforms */
#else
static inline void intel_gt_gmch_gen5_chipset_flush(struct intel_gt *gt)
{
}
static inline int intel_gt_gmch_gen5_probe(struct i915_ggtt *ggtt)
{
	/* No HW should be probed for this case yet, return fail */
	return -ENODEV;
}
static inline int intel_gt_gmch_gen6_probe(struct i915_ggtt *ggtt)
{
	/* No HW should be probed for this case yet, return fail */
	return -ENODEV;
}
static inline int intel_gt_gmch_gen8_probe(struct i915_ggtt *ggtt)
{
	/* No HW should be probed for this case yet, return fail */
	return -ENODEV;
}
static inline int intel_gt_gmch_gen5_enable_hw(struct drm_i915_private *i915)
{
	/* No HW should be enabled for this case yet, return fail */
	return -ENODEV;
}
#endif

#endif /* __INTEL_GT_GMCH_H__ */
