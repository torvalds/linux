// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <linux/fb.h>

#include <drm/drm_gem.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_fb.h"
#include "xe_bo.h"
#include "xe_display_bo.h"
#include "xe_pxp.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_wa.h"

#include <generated/xe_device_wa_oob.h>

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

	if (!(bo->flags & XE_BO_FLAG_FORCE_WC) &&
	    bo->ttm.type != ttm_bo_type_sg) {
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

#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
/*
 * FIXME: There shouldn't be any reason to have XE_PAGE_SIZE stride
 * alignment. The same 64 as i915 uses should be fine, and we shouldn't need to
 * have driver specific values. However, dropping the stride alignment to 64
 * leads to underflowing the bo pin count in the atomic cleanup work.
 */
static u32 xe_display_bo_fbdev_pitch_align(u32 stride)
{
	return ALIGN(stride, XE_PAGE_SIZE);
}

bool xe_display_bo_fbdev_prefer_stolen(struct xe_device *xe, unsigned int size)
{
	struct ttm_resource_manager *stolen;

	stolen = ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
	if (!stolen)
		return false;

	if (IS_DGFX(xe))
		return false;

	if (XE_DEVICE_WA(xe, 22019338487_display))
		return false;

	/*
	 * If the FB is too big, just don't use it since fbdev is not very
	 * important and we should probably use that space with FBC or other
	 * features.
	 */
	return stolen->size >= (size * 2) >> PAGE_SHIFT;
}

static struct drm_gem_object *xe_display_bo_fbdev_create(struct drm_device *drm, int size)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_bo *obj;

	obj = ERR_PTR(-ENODEV);

	if (xe_display_bo_fbdev_prefer_stolen(xe, size)) {
		obj = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe),
						size,
						ttm_bo_type_kernel,
						XE_BO_FLAG_FORCE_WC |
						XE_BO_FLAG_STOLEN |
						XE_BO_FLAG_GGTT,
						false);
		if (!IS_ERR(obj))
			drm_info(&xe->drm, "Allocated fbdev into stolen\n");
		else
			drm_info(&xe->drm, "Allocated fbdev into stolen failed: %li\n", PTR_ERR(obj));
	} else {
		drm_info(&xe->drm, "Allocating fbdev: Stolen memory not preferred.\n");
	}

	if (IS_ERR(obj)) {
		obj = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe), size,
						ttm_bo_type_kernel,
						XE_BO_FLAG_FORCE_WC |
						XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
						XE_BO_FLAG_GGTT,
						false);
	}

	if (IS_ERR(obj)) {
		drm_err(&xe->drm, "failed to allocate framebuffer (%pe)\n", obj);
		return ERR_PTR(-ENOMEM);
	}

	return &obj->ttm.base;
}

static void xe_display_bo_fbdev_destroy(struct drm_gem_object *obj)
{
	xe_bo_unpin_map_no_vm(gem_to_xe_bo(obj));
}

static int xe_display_bo_fbdev_fill_info(struct drm_gem_object *_obj, struct fb_info *info,
					 struct i915_vma *vma)
{
	struct xe_bo *obj = gem_to_xe_bo(_obj);
	struct pci_dev *pdev = to_pci_dev(_obj->dev->dev);

	if (!(obj->flags & XE_BO_FLAG_SYSTEM)) {
		if (obj->flags & XE_BO_FLAG_STOLEN)
			info->fix.smem_start = xe_ttm_stolen_io_offset(obj, 0);
		else
			info->fix.smem_start =
				pci_resource_start(pdev, 2) +
				xe_bo_addr(obj, 0, XE_PAGE_SIZE);

		info->fix.smem_len = obj->ttm.base.size;
	} else {
		/* XXX: Pure fiction, as the BO may not be physically accessible.. */
		info->fix.smem_start = 0;
		info->fix.smem_len = obj->ttm.base.size;
	}
	XE_WARN_ON(iosys_map_is_null(&obj->vmap));

	info->screen_base = obj->vmap.vaddr_iomem;
	info->screen_size = obj->ttm.base.size;

	return 0;
}
#endif

const struct intel_display_bo_interface xe_display_bo_interface = {
	.is_protected = xe_display_bo_is_protected,
	.key_check = xe_pxp_obj_key_check,
	.fb_mmap = drm_gem_prime_mmap,
	.read_from_page = xe_display_bo_read_from_page,
	.framebuffer_init = xe_display_bo_framebuffer_init,
	.framebuffer_fini = xe_display_bo_framebuffer_fini,
	.framebuffer_lookup = xe_display_bo_framebuffer_lookup,
#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
	.fbdev_create = xe_display_bo_fbdev_create,
	.fbdev_destroy = xe_display_bo_fbdev_destroy,
	.fbdev_fill_info = xe_display_bo_fbdev_fill_info,
	.fbdev_pitch_align = xe_display_bo_fbdev_pitch_align,
#endif
};
