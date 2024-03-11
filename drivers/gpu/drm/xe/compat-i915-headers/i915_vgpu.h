/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _I915_VGPU_H_
#define _I915_VGPU_H_

#include <linux/types.h>

struct drm_i915_private;
struct i915_ggtt;

static inline void intel_vgpu_detect(struct drm_i915_private *i915)
{
}
static inline bool intel_vgpu_active(struct drm_i915_private *i915)
{
	return false;
}
static inline void intel_vgpu_register(struct drm_i915_private *i915)
{
}
static inline bool intel_vgpu_has_full_ppgtt(struct drm_i915_private *i915)
{
	return false;
}
static inline bool intel_vgpu_has_hwsp_emulation(struct drm_i915_private *i915)
{
	return false;
}
static inline bool intel_vgpu_has_huge_gtt(struct drm_i915_private *i915)
{
	return false;
}
static inline int intel_vgt_balloon(struct i915_ggtt *ggtt)
{
	return 0;
}
static inline void intel_vgt_deballoon(struct i915_ggtt *ggtt)
{
}

#endif /* _I915_VGPU_H_ */
