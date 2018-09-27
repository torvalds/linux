/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"
#include <drm/drm_gem_framebuffer_helper.h>

/* ---------------------------------------------------------------------- */

static int bochsfb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct bochs_bo *bo = gem_to_bochs_bo(fb_helper->fb->obj[0]);

	return ttm_fbdev_mmap(vma, &bo->bo);
}

static struct fb_ops bochsfb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
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
	mode_cmd.pitches[0] = sizes->surface_width * 4;
	mode_cmd.pixel_format = DRM_FORMAT_HOST_XRGB8888;
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
	if (IS_ERR(info)) {
		DRM_ERROR("Failed to allocate fbi: %ld\n", PTR_ERR(info));
		return PTR_ERR(info);
	}

	info->par = &bochs->fb.helper;

	fb = drm_gem_fbdev_fb_create(bochs->dev, sizes, 0, gobj, NULL);
	if (IS_ERR(fb)) {
		DRM_ERROR("Failed to create framebuffer: %ld\n", PTR_ERR(fb));
		return PTR_ERR(fb);
	}

	/* setup helper */
	bochs->fb.helper.fb = fb;

	strcpy(info->fix.id, "bochsdrmfb");

	info->fbops = &bochsfb_ops;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, &bochs->fb.helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

	drm_vma_offset_remove(&bo->bo.bdev->vma_manager, &bo->bo.vma_node);
	info->fix.smem_start = 0;
	info->fix.smem_len = size;
	return 0;
}

static const struct drm_fb_helper_funcs bochs_fb_helper_funcs = {
	.fb_probe = bochsfb_create,
};

static struct drm_framebuffer *
bochs_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (mode_cmd->pixel_format != DRM_FORMAT_XRGB8888 &&
	    mode_cmd->pixel_format != DRM_FORMAT_BGRX8888)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, file, mode_cmd);
}

const struct drm_mode_config_funcs bochs_mode_funcs = {
	.fb_create = bochs_gem_fb_create,
};

int bochs_fbdev_init(struct bochs_device *bochs)
{
	return drm_fb_helper_fbdev_setup(bochs->dev, &bochs->fb.helper,
					 &bochs_fb_helper_funcs, 32, 1);
}

void bochs_fbdev_fini(struct bochs_device *bochs)
{
	drm_fb_helper_fbdev_teardown(bochs->dev);
}
