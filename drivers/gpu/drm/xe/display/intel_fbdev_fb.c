/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_fbdev_fb.h"

#include <drm/drm_fb_helper.h>

#include "xe_gt.h"
#include "xe_ttm_stolen_mgr.h"

#include "i915_drv.h"
#include "intel_display_types.h"

struct drm_framebuffer *intel_fbdev_fb_alloc(struct drm_fb_helper *helper,
			 struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb;
	struct drm_device *dev = helper->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct drm_i915_gem_object *obj;
	int size;

	/* we don't do packed 24bpp */
	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width *
				    DIV_ROUND_UP(sizes->surface_bpp, 8), XE_PAGE_SIZE);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = PAGE_ALIGN(size);
	obj = ERR_PTR(-ENODEV);

	if (!IS_DGFX(dev_priv)) {
		obj = xe_bo_create_pin_map(dev_priv, xe_device_get_root_tile(dev_priv),
					   NULL, size,
					   ttm_bo_type_kernel, XE_BO_SCANOUT_BIT |
					   XE_BO_CREATE_STOLEN_BIT |
					   XE_BO_CREATE_PINNED_BIT);
		if (!IS_ERR(obj))
			drm_info(&dev_priv->drm, "Allocated fbdev into stolen\n");
		else
			drm_info(&dev_priv->drm, "Allocated fbdev into stolen failed: %li\n", PTR_ERR(obj));
	}
	if (IS_ERR(obj)) {
		obj = xe_bo_create_pin_map(dev_priv, xe_device_get_root_tile(dev_priv), NULL, size,
					  ttm_bo_type_kernel, XE_BO_SCANOUT_BIT |
					  XE_BO_CREATE_VRAM_IF_DGFX(xe_device_get_root_tile(dev_priv)) |
					  XE_BO_CREATE_PINNED_BIT);
	}

	if (IS_ERR(obj)) {
		drm_err(&dev_priv->drm, "failed to allocate framebuffer (%pe)\n", obj);
		fb = ERR_PTR(-ENOMEM);
		goto err;
	}

	fb = intel_framebuffer_create(obj, &mode_cmd);
	if (IS_ERR(fb)) {
		xe_bo_unpin_map_no_vm(obj);
		goto err;
	}

	drm_gem_object_put(intel_bo_to_drm_bo(obj));
	return fb;

err:
	return fb;
}

int intel_fbdev_fb_fill_info(struct drm_i915_private *i915, struct fb_info *info,
			      struct drm_i915_gem_object *obj, struct i915_vma *vma)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!(obj->flags & XE_BO_CREATE_SYSTEM_BIT)) {
		if (obj->flags & XE_BO_CREATE_STOLEN_BIT)
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
	info->screen_size = intel_bo_to_drm_bo(obj)->size;

	return 0;
}
