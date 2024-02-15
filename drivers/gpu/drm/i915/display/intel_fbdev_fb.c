/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_fb_helper.h>

#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_fbdev_fb.h"

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
				    DIV_ROUND_UP(sizes->surface_bpp, 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = PAGE_ALIGN(size);

	obj = ERR_PTR(-ENODEV);
	if (HAS_LMEM(dev_priv)) {
		obj = i915_gem_object_create_lmem(dev_priv, size,
						  I915_BO_ALLOC_CONTIGUOUS |
						  I915_BO_ALLOC_USER);
	} else {
		/*
		 * If the FB is too big, just don't use it since fbdev is not very
		 * important and we should probably use that space with FBC or other
		 * features.
		 *
		 * Also skip stolen on MTL as Wa_22018444074 mitigation.
		 */
		if (!(IS_METEORLAKE(dev_priv)) && size * 2 < dev_priv->dsm.usable_size)
			obj = i915_gem_object_create_stolen(dev_priv, size);
		if (IS_ERR(obj))
			obj = i915_gem_object_create_shmem(dev_priv, size);
	}

	if (IS_ERR(obj)) {
		drm_err(&dev_priv->drm, "failed to allocate framebuffer (%pe)\n", obj);
		return ERR_PTR(-ENOMEM);
	}

	fb = intel_framebuffer_create(obj, &mode_cmd);
	i915_gem_object_put(obj);

	return fb;
}

int intel_fbdev_fb_fill_info(struct drm_i915_private *i915, struct fb_info *info,
			     struct drm_i915_gem_object *obj, struct i915_vma *vma)
{
	struct i915_gem_ww_ctx ww;
	void __iomem *vaddr;
	int ret;

	if (i915_gem_object_is_lmem(obj)) {
		struct intel_memory_region *mem = obj->mm.region;

		/* Use fbdev's framebuffer from lmem for discrete */
		info->fix.smem_start =
			(unsigned long)(mem->io.start +
					i915_gem_object_get_dma_address(obj, 0) -
					mem->region.start);
		info->fix.smem_len = obj->base.size;
	} else {
		struct i915_ggtt *ggtt = to_gt(i915)->ggtt;

		/* Our framebuffer is the entirety of fbdev's system memory */
		info->fix.smem_start =
			(unsigned long)(ggtt->gmadr.start + i915_ggtt_offset(vma));
		info->fix.smem_len = vma->size;
	}

	for_i915_gem_ww(&ww, ret, false) {
		ret = i915_gem_object_lock(vma->obj, &ww);

		if (ret)
			continue;

		vaddr = i915_vma_pin_iomap(vma);
		if (IS_ERR(vaddr)) {
			drm_err(&i915->drm,
				"Failed to remap framebuffer into virtual memory (%pe)\n", vaddr);
			ret = PTR_ERR(vaddr);
			continue;
		}
	}

	if (ret)
		return ret;

	info->screen_base = vaddr;
	info->screen_size = intel_bo_to_drm_bo(obj)->size;

	return 0;
}
