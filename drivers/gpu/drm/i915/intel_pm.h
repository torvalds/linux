/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PM_H__
#define __INTEL_PM_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;

void intel_init_clock_gating(struct drm_i915_private *dev_priv);
void intel_suspend_hw(struct drm_i915_private *dev_priv);
int ilk_wm_max_level(const struct drm_i915_private *dev_priv);
void intel_init_pm(struct drm_i915_private *dev_priv);
void intel_init_clock_gating_hooks(struct drm_i915_private *dev_priv);
void intel_pm_setup(struct drm_i915_private *dev_priv);
void g4x_wm_get_hw_state(struct drm_i915_private *dev_priv);
void vlv_wm_get_hw_state(struct drm_i915_private *dev_priv);
void ilk_wm_get_hw_state(struct drm_i915_private *dev_priv);
void g4x_wm_sanitize(struct drm_i915_private *dev_priv);
void vlv_wm_sanitize(struct drm_i915_private *dev_priv);
bool ilk_disable_lp_wm(struct drm_i915_private *dev_priv);
bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state);
void intel_print_wm_latency(struct drm_i915_private *dev_priv,
			    const char *name, const u16 wm[]);

bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable);

#endif /* __INTEL_PM_H__ */
