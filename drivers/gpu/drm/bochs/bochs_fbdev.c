/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"

/* ---------------------------------------------------------------------- */

static struct fb_ops bochsfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int bochsfb_create_object(struct bochs_device *bochs,
				 struct drm_mode_fb_cmd2 *mode_cmd,
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
	struct drm_device *dev = bochs->dev;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct device *device = &dev->pdev->dev;
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

	ret = ttm_bo_reserve(&bo->bo, true, false, false, 0);
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
	info = framebuffer_alloc(0, device);
	if (info == NULL)
		return -ENOMEM;

	info->par = &bochs->fb.helper;

	ret = bochs_framebuffer_init(bochs->dev, &bochs->fb.gfb, &mode_cmd, gobj);
	if (ret)
		return ret;

	bochs->fb.size = size;

	/* setup helper */
	fb = &bochs->fb.gfb.base;
	bochs->fb.helper.fb = fb;
	bochs->fb.helper.fbdev = info;

	strcpy(info->fix.id, "bochsdrmfb");

	info->flags = FBINFO_DEFAULT;
	info->fbops = &bochsfb_ops;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &bochs->fb.helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

#if 0
	/* FIXME: get this right for mmap(/dev/fb0) */
	info->fix.smem_start = bochs_bo_mmap_offset(bo);
	info->fix.smem_len = size;
#endif

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("%s: can't allocate color map\n", info->fix.id);
		return -ENOMEM;
	}

	return 0;
}

static int bochs_fbdev_destroy(struct bochs_device *bochs)
{
	struct bochs_framebuffer *gfb = &bochs->fb.gfb;
	struct fb_info *info;

	DRM_DEBUG_DRIVER("\n");

	if (bochs->fb.helper.fbdev) {
		info = bochs->fb.helper.fbdev;

		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (gfb->obj) {
		drm_gem_object_unreference_unlocked(gfb->obj);
		gfb->obj = NULL;
	}

	drm_fb_helper_fini(&bochs->fb.helper);
	drm_framebuffer_unregister_private(&gfb->base);
	drm_framebuffer_cleanup(&gfb->base);

	return 0;
}

void bochs_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			u16 blue, int regno)
{
}

void bochs_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			u16 *blue, int regno)
{
	*red   = regno;
	*green = regno;
	*blue  = regno;
}

static struct drm_fb_helper_funcs bochs_fb_helper_funcs = {
	.gamma_set = bochs_fb_gamma_set,
	.gamma_get = bochs_fb_gamma_get,
	.fb_probe = bochsfb_create,
};

int bochs_fbdev_init(struct bochs_device *bochs)
{
	int ret;

	bochs->fb.helper.funcs = &bochs_fb_helper_funcs;

	ret = drm_fb_helper_init(bochs->dev, &bochs->fb.helper,
				 1, 1);
	if (ret)
		return ret;

	drm_fb_helper_single_add_all_connectors(&bochs->fb.helper);
	drm_helper_disable_unused_functions(bochs->dev);
	drm_fb_helper_initial_config(&bochs->fb.helper, 32);

	bochs->fb.initialized = true;
	return 0;
}

void bochs_fbdev_fini(struct bochs_device *bochs)
{
	if (!bochs->fb.initialized)
		return;

	bochs_fbdev_destroy(bochs);
	bochs->fb.initialized = false;
}
