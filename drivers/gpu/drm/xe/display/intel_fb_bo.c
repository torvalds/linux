/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_modeset_helper.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_fb_bo.h"

void intel_fb_bo_framebuffer_fini(struct xe_bo *bo)
{
	if (bo->flags & XE_BO_CREATE_PINNED_BIT) {
		/* Unpin our kernel fb first */
		xe_bo_lock(bo, false);
		xe_bo_unpin(bo);
		xe_bo_unlock(bo);
	}
	xe_bo_put(bo);
}

int intel_fb_bo_framebuffer_init(struct intel_framebuffer *intel_fb,
				 struct xe_bo *bo,
				 struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_private *i915 = to_i915(bo->ttm.base.dev);
	int ret;

	xe_bo_get(bo);

	ret = ttm_bo_reserve(&bo->ttm, true, false, NULL);
	if (ret)
		return ret;

	if (!(bo->flags & XE_BO_SCANOUT_BIT)) {
		/*
		 * XE_BO_SCANOUT_BIT should ideally be set at creation, or is
		 * automatically set when creating FB. We cannot change caching
		 * mode when the boect is VM_BINDed, so we can only set
		 * coherency with display when unbound.
		 */
		if (XE_IOCTL_DBG(i915, !list_empty(&bo->ttm.base.gpuva.list))) {
			ttm_bo_unreserve(&bo->ttm);
			return -EINVAL;
		}
		bo->flags |= XE_BO_SCANOUT_BIT;
	}
	ttm_bo_unreserve(&bo->ttm);

	return ret;
}

struct xe_bo *intel_fb_bo_lookup_valid_bo(struct drm_i915_private *i915,
					  struct drm_file *filp,
					  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_gem_object *bo;
	struct drm_gem_object *gem = drm_gem_object_lookup(filp, mode_cmd->handles[0]);

	if (!gem)
		return ERR_PTR(-ENOENT);

	bo = gem_to_xe_bo(gem);
	/* Require vram placement or dma-buf import */
	if (IS_DGFX(i915) &&
	    !xe_bo_can_migrate(gem_to_xe_bo(gem), XE_PL_VRAM0) &&
	    bo->ttm.type != ttm_bo_type_sg) {
		drm_gem_object_put(gem);
		return ERR_PTR(-EREMOTE);
	}

	return bo;
}
