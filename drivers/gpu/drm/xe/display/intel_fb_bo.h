/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_FB_BO_H__
#define __INTEL_FB_BO_H__

struct drm_file;
struct drm_mode_fb_cmd2;
struct drm_i915_private;
struct intel_framebuffer;
struct xe_bo;

void intel_fb_bo_framebuffer_fini(struct xe_bo *bo);
int intel_fb_bo_framebuffer_init(struct intel_framebuffer *intel_fb,
				 struct xe_bo *bo,
				 struct drm_mode_fb_cmd2 *mode_cmd);

struct xe_bo *intel_fb_bo_lookup_valid_bo(struct drm_i915_private *i915,
					  struct drm_file *filp,
					  const struct drm_mode_fb_cmd2 *mode_cmd);

#endif
