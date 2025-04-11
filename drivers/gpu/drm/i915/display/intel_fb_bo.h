/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_FB_BO_H__
#define __INTEL_FB_BO_H__

struct drm_device;
struct drm_file;
struct drm_framebuffer;
struct drm_gem_object;
struct drm_mode_fb_cmd2;

void intel_fb_bo_framebuffer_fini(struct drm_gem_object *obj);

int intel_fb_bo_framebuffer_init(struct drm_framebuffer *fb,
				 struct drm_gem_object *obj,
				 struct drm_mode_fb_cmd2 *mode_cmd);

struct drm_gem_object *
intel_fb_bo_lookup_valid_bo(struct drm_device *drm,
			    struct drm_file *filp,
			    const struct drm_mode_fb_cmd2 *user_mode_cmd);

#endif
