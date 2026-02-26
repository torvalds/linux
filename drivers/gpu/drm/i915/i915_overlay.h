/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef __I915_OVERLAY_H__
#define __I915_OVERLAY_H__

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_gem_object;
struct i915_vma;

bool i915_overlay_is_active(struct drm_device *drm);
int i915_overlay_on(struct drm_device *drm,
		    u32 frontbuffer_bits);
int i915_overlay_continue(struct drm_device *drm,
			  struct i915_vma *vma,
			  bool load_polyphase_filter);
int i915_overlay_off(struct drm_device *drm);
int i915_overlay_recover_from_interrupt(struct drm_device *drm);
int i915_overlay_release_old_vid(struct drm_device *drm);

void i915_overlay_reset(struct drm_device *drm);

struct i915_vma *i915_overlay_pin_fb(struct drm_device *drm,
				     struct drm_gem_object *obj,
				     u32 *offset);
void i915_overlay_unpin_fb(struct drm_device *drm,
			   struct i915_vma *vma);

struct drm_gem_object *
i915_overlay_obj_lookup(struct drm_device *drm,
			struct drm_file *file_priv,
			u32 handle);

void __iomem *i915_overlay_setup(struct drm_device *drm,
				 bool needs_physical);
void i915_overlay_cleanup(struct drm_device *drm);

#endif /* __I915_OVERLAY_H__ */
