/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I9XX_WM_H__
#define __I9XX_WM_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;

#ifdef I915
bool ilk_disable_lp_wm(struct drm_i915_private *i915);
void ilk_wm_sanitize(struct drm_i915_private *i915);
bool intel_set_memory_cxsr(struct drm_i915_private *i915, bool enable);
void i9xx_wm_init(struct drm_i915_private *i915);
#else
static inline bool ilk_disable_lp_wm(struct drm_i915_private *i915)
{
	return false;
}
static inline void ilk_wm_sanitize(struct drm_i915_private *i915)
{
}
static inline bool intel_set_memory_cxsr(struct drm_i915_private *i915, bool enable)
{
	return false;
}
static inline void i9xx_wm_init(struct drm_i915_private *i915)
{
}
#endif

#endif /* __I9XX_WM_H__ */
