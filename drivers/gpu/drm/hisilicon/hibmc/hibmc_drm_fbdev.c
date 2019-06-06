// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_probe_helper.h>

#include "hibmc_drm_drv.h"

static int hibmcfb_create_object(
				struct hibmc_drm_private *priv,
				const struct drm_mode_fb_cmd2 *mode_cmd,
				struct drm_gem_object **gobj_p)
{
	struct drm_gem_object *gobj;
	struct drm_device *dev = priv->dev;
	u32 size;
	int ret = 0;

	size = mode_cmd->pitches[0] * mode_cmd->height;
	ret = hibmc_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static struct fb_ops hibmc_drm_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int hibmc_drm_fb_create(struct drm_fb_helper *helper,
			       struct drm_fb_helper_surface_size *sizes)
{
	struct hibmc_fbdev *hi_fbdev =
		container_of(helper, struct hibmc_fbdev, helper);
	struct hibmc_drm_private *priv = helper->dev->dev_private;
	struct fb_info *info;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	int ret = 0;
	int ret1;
	size_t size;
	unsigned int bytes_per_pixel;
	struct hibmc_bo *bo = NULL;

	DRM_DEBUG_DRIVER("surface width(%d), height(%d) and bpp(%d)\n",
			 sizes->surface_width, sizes->surface_height,
			 sizes->surface_bpp);

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = PAGE_ALIGN(mode_cmd.pitches[0] * mode_cmd.height);

	ret = hibmcfb_create_object(priv, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object: %d\n", ret);
		return -ENOMEM;
	}

	bo = gem_to_hibmc_bo(gobj);

	ret = ttm_bo_reserve(&bo->bo, true, false, NULL);
	if (ret) {
		DRM_ERROR("failed to reserve ttm_bo: %d\n", ret);
		goto out_unref_gem;
	}

	ret = hibmc_bo_pin(bo, TTM_PL_FLAG_VRAM, NULL);
	if (ret) {
		DRM_ERROR("failed to pin fbcon: %d\n", ret);
		goto out_unreserve_ttm_bo;
	}

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
	if (ret) {
		DRM_ERROR("failed to kmap fbcon: %d\n", ret);
		goto out_unpin_bo;
	}
	ttm_bo_unreserve(&bo->bo);

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		DRM_ERROR("failed to allocate fbi: %d\n", ret);
		goto out_release_fbi;
	}

	hi_fbdev->fb = hibmc_framebuffer_init(priv->dev, &mode_cmd, gobj);
	if (IS_ERR(hi_fbdev->fb)) {
		ret = PTR_ERR(hi_fbdev->fb);
		hi_fbdev->fb = NULL;
		DRM_ERROR("failed to initialize framebuffer: %d\n", ret);
		goto out_release_fbi;
	}

	priv->fbdev->size = size;
	hi_fbdev->helper.fb = &hi_fbdev->fb->fb;

	info->fbops = &hibmc_drm_fb_ops;

	drm_fb_helper_fill_info(info, &priv->fbdev->helper, sizes);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

	info->fix.smem_start = bo->bo.mem.bus.offset + bo->bo.mem.bus.base;
	info->fix.smem_len = size;
	return 0;

out_release_fbi:
	ret1 = ttm_bo_reserve(&bo->bo, true, false, NULL);
	if (ret1) {
		DRM_ERROR("failed to rsv ttm_bo when release fbi: %d\n", ret1);
		goto out_unref_gem;
	}
	ttm_bo_kunmap(&bo->kmap);
out_unpin_bo:
	hibmc_bo_unpin(bo);
out_unreserve_ttm_bo:
	ttm_bo_unreserve(&bo->bo);
out_unref_gem:
	drm_gem_object_put_unlocked(gobj);

	return ret;
}

static void hibmc_fbdev_destroy(struct hibmc_fbdev *fbdev)
{
	struct hibmc_framebuffer *gfb = fbdev->fb;
	struct drm_fb_helper *fbh = &fbdev->helper;

	drm_fb_helper_unregister_fbi(fbh);

	drm_fb_helper_fini(fbh);

	if (gfb)
		drm_framebuffer_put(&gfb->fb);
}

static const struct drm_fb_helper_funcs hibmc_fbdev_helper_funcs = {
	.fb_probe = hibmc_drm_fb_create,
};

int hibmc_fbdev_init(struct hibmc_drm_private *priv)
{
	int ret;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	struct hibmc_fbdev *hifbdev;

	hifbdev = devm_kzalloc(priv->dev->dev, sizeof(*hifbdev), GFP_KERNEL);
	if (!hifbdev) {
		DRM_ERROR("failed to allocate hibmc_fbdev\n");
		return -ENOMEM;
	}

	priv->fbdev = hifbdev;
	drm_fb_helper_prepare(priv->dev, &hifbdev->helper,
			      &hibmc_fbdev_helper_funcs);

	/* Now just one crtc and one channel */
	ret = drm_fb_helper_init(priv->dev, &hifbdev->helper, 1);
	if (ret) {
		DRM_ERROR("failed to initialize fb helper: %d\n", ret);
		return ret;
	}

	ret = drm_fb_helper_single_add_all_connectors(&hifbdev->helper);
	if (ret) {
		DRM_ERROR("failed to add all connectors: %d\n", ret);
		goto fini;
	}

	ret = drm_fb_helper_initial_config(&hifbdev->helper, 16);
	if (ret) {
		DRM_ERROR("failed to setup initial conn config: %d\n", ret);
		goto fini;
	}

	var = &hifbdev->helper.fbdev->var;
	fix = &hifbdev->helper.fbdev->fix;

	DRM_DEBUG_DRIVER("Member of info->var is :\n"
			 "xres=%d\n"
			 "yres=%d\n"
			 "xres_virtual=%d\n"
			 "yres_virtual=%d\n"
			 "xoffset=%d\n"
			 "yoffset=%d\n"
			 "bits_per_pixel=%d\n"
			 "...\n", var->xres, var->yres, var->xres_virtual,
			 var->yres_virtual, var->xoffset, var->yoffset,
			 var->bits_per_pixel);
	DRM_DEBUG_DRIVER("Member of info->fix is :\n"
			 "smem_start=%lx\n"
			 "smem_len=%d\n"
			 "type=%d\n"
			 "type_aux=%d\n"
			 "visual=%d\n"
			 "xpanstep=%d\n"
			 "ypanstep=%d\n"
			 "ywrapstep=%d\n"
			 "line_length=%d\n"
			 "accel=%d\n"
			 "capabilities=%d\n"
			 "...\n", fix->smem_start, fix->smem_len, fix->type,
			 fix->type_aux, fix->visual, fix->xpanstep,
			 fix->ypanstep, fix->ywrapstep, fix->line_length,
			 fix->accel, fix->capabilities);

	return 0;

fini:
	drm_fb_helper_fini(&hifbdev->helper);
	return ret;
}

void hibmc_fbdev_fini(struct hibmc_drm_private *priv)
{
	if (!priv->fbdev)
		return;

	hibmc_fbdev_destroy(priv->fbdev);
	priv->fbdev = NULL;
}
