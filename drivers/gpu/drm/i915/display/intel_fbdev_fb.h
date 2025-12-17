/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_FBDEV_FB_H__
#define __INTEL_FBDEV_FB_H__

#include <linux/types.h>

struct drm_device;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct fb_info;
struct i915_vma;

u32 intel_fbdev_fb_pitch_align(u32 stride);
struct drm_gem_object *intel_fbdev_fb_bo_create(struct drm_device *drm, int size);
void intel_fbdev_fb_bo_destroy(struct drm_gem_object *obj);
int intel_fbdev_fb_fill_info(struct drm_device *drm, struct fb_info *info,
			     struct drm_gem_object *obj, struct i915_vma *vma);

#endif
