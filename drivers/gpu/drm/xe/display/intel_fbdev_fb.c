/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/fb.h>

#include "intel_fbdev_fb.h"
#include "xe_bo.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_wa.h"

#include <generated/xe_device_wa_oob.h>

/*
 * FIXME: There shouldn't be any reason to have XE_PAGE_SIZE stride
 * alignment. The same 64 as i915 uses should be fine, and we shouldn't need to
 * have driver specific values. However, dropping the stride alignment to 64
 * leads to underflowing the bo pin count in the atomic cleanup work.
 */
u32 intel_fbdev_fb_pitch_align(u32 stride)
{
	return ALIGN(stride, XE_PAGE_SIZE);
}

bool intel_fbdev_fb_prefer_stolen(struct drm_device *drm, unsigned int size)
{
	struct xe_device *xe = to_xe_device(drm);
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
	return stolen->size >= size * 2;
}

struct drm_gem_object *intel_fbdev_fb_bo_create(struct drm_device *drm, int size)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_bo *obj;

	obj = ERR_PTR(-ENODEV);

	if (intel_fbdev_fb_prefer_stolen(drm, size)) {
		obj = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe),
						size,
						ttm_bo_type_kernel, XE_BO_FLAG_SCANOUT |
						XE_BO_FLAG_STOLEN |
						XE_BO_FLAG_GGTT, false);
		if (!IS_ERR(obj))
			drm_info(&xe->drm, "Allocated fbdev into stolen\n");
		else
			drm_info(&xe->drm, "Allocated fbdev into stolen failed: %li\n", PTR_ERR(obj));
	} else {
		drm_info(&xe->drm, "Allocating fbdev: Stolen memory not preferred.\n");
	}

	if (IS_ERR(obj)) {
		obj = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe), size,
						ttm_bo_type_kernel, XE_BO_FLAG_SCANOUT |
						XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
						XE_BO_FLAG_GGTT, false);
	}

	if (IS_ERR(obj)) {
		drm_err(&xe->drm, "failed to allocate framebuffer (%pe)\n", obj);
		return ERR_PTR(-ENOMEM);
	}

	return &obj->ttm.base;
}

void intel_fbdev_fb_bo_destroy(struct drm_gem_object *obj)
{
	xe_bo_unpin_map_no_vm(gem_to_xe_bo(obj));
}

int intel_fbdev_fb_fill_info(struct drm_device *drm, struct fb_info *info,
			     struct drm_gem_object *_obj, struct i915_vma *vma)
{
	struct xe_bo *obj = gem_to_xe_bo(_obj);
	struct pci_dev *pdev = to_pci_dev(drm->dev);

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
