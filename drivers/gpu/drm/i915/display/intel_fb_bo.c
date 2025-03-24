/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_framebuffer.h>

#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_fb.h"
#include "intel_fb_bo.h"

void intel_fb_bo_framebuffer_fini(struct drm_gem_object *obj)
{
	/* Nothing to do for i915 */
}

int intel_fb_bo_framebuffer_init(struct intel_framebuffer *intel_fb,
				 struct drm_gem_object *_obj,
				 struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned int tiling, stride;

	i915_gem_object_lock(obj, NULL);
	tiling = i915_gem_object_get_tiling(obj);
	stride = i915_gem_object_get_stride(obj);
	i915_gem_object_unlock(obj);

	if (mode_cmd->flags & DRM_MODE_FB_MODIFIERS) {
		/*
		 * If there's a fence, enforce that
		 * the fb modifier and tiling mode match.
		 */
		if (tiling != I915_TILING_NONE &&
		    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
			drm_dbg_kms(&i915->drm,
				    "tiling_mode doesn't match fb modifier\n");
			return -EINVAL;
		}
	} else {
		if (tiling == I915_TILING_X) {
			mode_cmd->modifier[0] = I915_FORMAT_MOD_X_TILED;
		} else if (tiling == I915_TILING_Y) {
			drm_dbg_kms(&i915->drm,
				    "No Y tiling for legacy addfb\n");
			return -EINVAL;
		}
	}

	/*
	 * gen2/3 display engine uses the fence if present,
	 * so the tiling mode must match the fb modifier exactly.
	 */
	if (DISPLAY_VER(i915) < 4 &&
	    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
		drm_dbg_kms(&i915->drm,
			    "tiling_mode must match fb modifier exactly on gen2/3\n");
		return -EINVAL;
	}

	/*
	 * If there's a fence, enforce that
	 * the fb pitch and fence stride match.
	 */
	if (tiling != I915_TILING_NONE && mode_cmd->pitches[0] != stride) {
		drm_dbg_kms(&i915->drm,
			    "pitch (%d) must match tiling stride (%d)\n",
			    mode_cmd->pitches[0], stride);
		return -EINVAL;
	}

	return 0;
}

struct drm_gem_object *
intel_fb_bo_lookup_valid_bo(struct drm_i915_private *i915,
			    struct drm_file *filp,
			    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);

	/* object is backed with LMEM for discrete */
	if (HAS_LMEM(i915) && !i915_gem_object_can_migrate(obj, INTEL_REGION_LMEM_0)) {
		/* object is "remote", not in local memory */
		i915_gem_object_put(obj);
		drm_dbg_kms(&i915->drm, "framebuffer must reside in local memory\n");
		return ERR_PTR(-EREMOTE);
	}

	return intel_bo_to_drm_bo(obj);
}
