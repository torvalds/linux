/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef __XE_FB_PIN_H__
#define __XE_FB_PIN_H__

#include <linux/types.h>

struct drm_gem_object;
struct i915_vma;
struct intel_fb_pin_params;

int xe_fb_pin_ggtt_pin(struct drm_gem_object *obj,
		       const struct intel_fb_pin_params *pin_params,
		       struct i915_vma **out_ggtt_vma,
		       u32 *out_offset,
		       int *out_fence_id);

extern const struct intel_display_fb_pin_interface xe_display_fb_pin_interface;

#endif /* __XE_FB_PIN_H__ */
