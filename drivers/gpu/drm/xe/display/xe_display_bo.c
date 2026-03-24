// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_gem.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_fb.h"
#include "xe_bo.h"
#include "xe_display_bo.h"
#include "xe_pxp.h"

static bool xe_display_bo_is_protected(struct drm_gem_object *obj)
{
	return xe_bo_is_protected(gem_to_xe_bo(obj));
}

static int xe_display_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);

	return xe_bo_read(bo, offset, dst, size);
}

static int xe_display_bo_framebuffer_init(struct drm_gem_object *obj,
					  struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_device *xe = to_xe_device(bo->ttm.base.dev);
	int ret;

	/*
	 * Some modifiers require physical alignment of 64KiB VRAM pages;
	 * require that the BO in those cases is created correctly.
	 */
	if (XE_IOCTL_DBG(xe, intel_fb_needs_64k_phys(mode_cmd->modifier[0]) &&
			     !(bo->flags & XE_BO_FLAG_NEEDS_64K)))
		return -EINVAL;

	xe_bo_get(bo);

	ret = ttm_bo_reserve(&bo->ttm, true, false, NULL);
	if (ret)
		goto err;

	if (!(bo->flags & XE_BO_FLAG_FORCE_WC)) {
		/*
		 * XE_BO_FLAG_FORCE_WC should ideally be set at creation, or is
		 * automatically set when creating FB. We cannot change caching
		 * mode when the bo is VM_BINDed, so we can only set
		 * coherency with display when unbound.
		 */
		if (XE_IOCTL_DBG(xe, xe_bo_is_vm_bound(bo))) {
			ttm_bo_unreserve(&bo->ttm);
			ret = -EINVAL;
			goto err;
		}
		bo->flags |= XE_BO_FLAG_FORCE_WC;
	}
	ttm_bo_unreserve(&bo->ttm);
	return 0;

err:
	xe_bo_put(bo);
	return ret;
}

static void xe_display_bo_framebuffer_fini(struct drm_gem_object *obj)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);

	if (bo->flags & XE_BO_FLAG_PINNED) {
		/* Unpin our kernel fb first */
		xe_bo_lock(bo, false);
		xe_bo_unpin(bo);
		xe_bo_unlock(bo);
	}
	xe_bo_put(bo);
}

static struct drm_gem_object *
xe_display_bo_framebuffer_lookup(struct drm_device *drm,
				 struct drm_file *filp,
				 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_bo *bo;
	struct drm_gem_object *gem = drm_gem_object_lookup(filp, mode_cmd->handles[0]);

	if (!gem)
		return ERR_PTR(-ENOENT);

	bo = gem_to_xe_bo(gem);
	/* Require vram placement or dma-buf import */
	if (IS_DGFX(xe) &&
	    !xe_bo_can_migrate(bo, XE_PL_VRAM0) &&
	    bo->ttm.type != ttm_bo_type_sg) {
		drm_gem_object_put(gem);
		return ERR_PTR(-EREMOTE);
	}

	return gem;
}

const struct intel_display_bo_interface xe_display_bo_interface = {
	.is_protected = xe_display_bo_is_protected,
	.key_check = xe_pxp_obj_key_check,
	.fb_mmap = drm_gem_prime_mmap,
	.read_from_page = xe_display_bo_read_from_page,
	.framebuffer_init = xe_display_bo_framebuffer_init,
	.framebuffer_fini = xe_display_bo_framebuffer_fini,
	.framebuffer_lookup = xe_display_bo_framebuffer_lookup,
};
