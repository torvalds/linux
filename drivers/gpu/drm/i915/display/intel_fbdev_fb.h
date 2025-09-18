/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_FBDEV_FB_H__
#define __INTEL_FBDEV_FB_H__

struct drm_device;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct fb_info;
struct i915_vma;
struct intel_display;

struct intel_framebuffer *intel_fbdev_fb_alloc(struct drm_device *drm,
					       struct drm_mode_fb_cmd2 *mode_cmd);
int intel_fbdev_fb_fill_info(struct intel_display *display, struct fb_info *info,
			     struct drm_gem_object *obj, struct i915_vma *vma);

#endif
