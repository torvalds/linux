/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"

/* ---------------------------------------------------------------------- */

static int bochsfb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct bochs_device *bochs =
		container_of(fb_helper, struct bochs_device, fb.helper);
	struct bochs_bo *bo = gem_to_bochs_bo(bochs->fb.gfb.obj);

	return ttm_fbdev_mmap(vma, &bo->bo);
}

static struct fb_ops bochsfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_mmap = bochsfb_mmap,
};

static int bochsfb_create_object(struct bochs_device *bochs,
				 const struct drm_mode_fb_cmd2 *mode_cmd,
				 struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = bochs->dev;
	struct drm_gem_object *gobj;
	u32 size;
	int ret = 0;

	size = mode_cmd->pitches[0] * mode_cmd->height;
	ret = bochs_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static int bochsfb_create(struct drm_fb_helper *helper,
			  struct drm_fb_helper_surface_size *sizes)
{
	struct bochs_device *bochs =
		container_of(helper, struct bochs_device, fb.helper);
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct bochs_bo *bo = NULL;
	int size, ret;

	if (sizes->surface_bpp != 32)
		return -EINVAL;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	size = mode_cmd.pitches[0] * mode_cmd.height;

	/* alloc, pin & map bo */
	ret = bochsfb_create_object(bochs, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	bo = gem_to_bochs_bo(gobj);

	ret = ttm_bo_reserve(&bo->bo, true, false, NULL);
	if (ret)
		return ret;

	ret = bochs_bo_pin(bo, TTM_PL_FLAG_VRAM, NULL);
	if (ret) {
		DRM_ERROR("failed to pin fbcon\n");
		ttm_bo_unreserve(&bo->bo);
		return ret;
	}

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages,
			  &bo->kmap);
	if (ret) {
		DRM_ERROR("failed to kmap fbcon\n");
		ttm_bo_unreserve(&bo->bo);
		return ret;
	}

	ttm_bo_unreserve(&bo->bo);

	/* init fb device */
	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->par = &bochs->fb.helper;

	ret = bochs_framebuffer_init(bochs->dev, &bochs->fb.gfb, &mode_cmd, gobj);
	if (ret) {
		drm_fb_helper_release_fbi(helper);
		return ret;
	}

	bochs->fb.size = size;

	/* setup helper */
	fb = &bochs->fb.gfb.base;
	bochs->fb.helper.fb = fb;

	strcpy(info->fix.id, "bochsdrmfb");

	info->flags = FBINFO_DEFAULT;
	info->fbops = &bochsfb_ops;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &bochs->fb.helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

	drm_vma_offset_remove(&bo->bo.bdev->vma_manager, &bo->bo.vma_node);
	info->fix.smem_start = 0;
	info->fix.smem_len = size;

	return 0;
}

static int bochs_fbdev_destroy(struct bochs_device *bochs)
{
	struct bochs_framebuffer *gfb = &bochs->fb.gfb;

	DRM_DEBUG_DRIVER("\n");

	drm_fb_helper_unregister_fbi(&bochs->fb.helper);
	drm_fb_helper_release_fbi(&bochs->fb.helper);

	if (gfb->obj) {
		drm_gem_object_unreference_unlocked(gfb->obj);
		gfb->obj = NULL;
	}

	drm_fb_helper_fini(&bochs->fb.helper);
	drm_framebuffer_unregister_private(&gfb->base);
	drm_framebuffer_cleanup(&gfb->base);

	return 0;
}

static const struct drm_fb_helper_funcs bochs_fb_helper_funcs = {
	.fb_probe = bochsfb_create,
};

int bochs_fbdev_init(struct bochs_device *bochs)
{
	int ret;

	drm_fb_helper_prepare(bochs->dev, &bochs->fb.helper,
			      &bochs_fb_helper_funcs);

	ret = drm_fb_helper_init(bochs->dev, &bochs->fb.helper,
				 1, 1);
	if (ret)
		return ret;

	ret = drm_fb_helper_single_add_all_connectors(&bochs->fb.helper);
	if (ret)
		goto fini;

	drm_helper_disable_unused_functions(bochs->dev);

	ret = drm_fb_helper_initial_config(&bochs->fb.helper, 32);
	if (ret)
		goto fini;

	bochs->fb.initialized = true;
	return 0;

fini:
	drm_fb_helper_fini(&bochs->fb.helper);
	return ret;
}

void bochs_fbdev_fini(struct bochs_device *bochs)
{
	if (!bochs->fb.initialized)
		return;

	bochs_fbdev_destroy(bochs);
	bochs->fb.initialized = false;
}
