/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_WM_H__
#define __INTEL_WM_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;

bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state);
void intel_print_wm_latency(struct drm_i915_private *i915,
			    const char *name, const u16 wm[]);
void intel_wm_init(struct drm_i915_private *i915);

#endif /* __INTEL_WM_H__ */
