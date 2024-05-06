/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_FB_PIN_H__
#define __INTEL_FB_PIN_H__

#include <linux/types.h>

struct drm_framebuffer;
struct i915_vma;
struct intel_plane_state;
struct i915_gtt_view;

struct i915_vma *
intel_fb_pin_to_ggtt(const struct drm_framebuffer *fb,
		     bool phys_cursor,
		     const struct i915_gtt_view *view,
		     bool uses_fence,
		     unsigned long *out_flags);

void intel_fb_unpin_vma(struct i915_vma *vma, unsigned long flags);

int intel_plane_pin_fb(struct intel_plane_state *plane_state);
void intel_plane_unpin_fb(struct intel_plane_state *old_plane_state);

#endif
