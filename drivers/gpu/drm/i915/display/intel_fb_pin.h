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
struct iosys_map;

struct i915_vma *
intel_fb_pin_to_ggtt(const struct drm_framebuffer *fb,
		     const struct i915_gtt_view *view,
		     unsigned int alignment,
		     unsigned int phys_alignment,
		     unsigned int vtd_guard,
		     bool uses_fence,
		     unsigned long *out_flags);

void intel_fb_unpin_vma(struct i915_vma *vma, unsigned long flags);

int intel_plane_pin_fb(struct intel_plane_state *new_plane_state,
		       const struct intel_plane_state *old_plane_state);
void intel_plane_unpin_fb(struct intel_plane_state *old_plane_state);
void intel_fb_get_map(struct i915_vma *vma, struct iosys_map *map);

#endif
