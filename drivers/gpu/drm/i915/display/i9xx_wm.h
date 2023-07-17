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

int ilk_wm_max_level(const struct drm_i915_private *i915);
bool ilk_disable_lp_wm(struct drm_i915_private *i915);
void ilk_wm_sanitize(struct drm_i915_private *i915);
bool intel_set_memory_cxsr(struct drm_i915_private *i915, bool enable);
void i9xx_wm_init(struct drm_i915_private *i915);

#endif /* __I9XX_WM_H__ */
