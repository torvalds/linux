/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/fb.h>

#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fbdev_fb.h"

struct intel_framebuffer *intel_fbdev_fb_alloc(struct drm_device *drm,
					       struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct intel_display *display = to_intel_display(drm);
	struct drm_i915_private *dev_priv = to_i915(drm);
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	int size;

	size = mode_cmd->pitches[0] * mode_cmd->height;
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
		if (!display->platform.meteorlake && size * 2 < dev_priv->dsm.usable_size)
			obj = i915_gem_object_create_stolen(dev_priv, size);
		if (IS_ERR(obj))
			obj = i915_gem_object_create_shmem(dev_priv, size);
	}

	if (IS_ERR(obj)) {
		drm_err(drm, "failed to allocate framebuffer (%pe)\n", obj);
		return ERR_PTR(-ENOMEM);
	}

	fb = intel_framebuffer_create(intel_bo_to_drm_bo(obj),
				      drm_get_format_info(drm,
							  mode_cmd->pixel_format,
							  mode_cmd->modifier[0]),
				      mode_cmd);
	if (IS_ERR(fb)) {
		i915_gem_object_put(obj);
		goto err;
	}

	i915_gem_object_put(obj);

	return to_intel_framebuffer(fb);
err:
	return ERR_CAST(fb);
}

int intel_fbdev_fb_fill_info(struct intel_display *display, struct fb_info *info,
			     struct drm_gem_object *_obj, struct i915_vma *vma)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
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
			drm_err(display->drm,
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
