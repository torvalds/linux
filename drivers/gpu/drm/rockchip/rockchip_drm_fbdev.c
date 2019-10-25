// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#include <drm/drm.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"

#define PREFERRED_BPP		32
#define to_drm_private(x) \
		container_of(x, struct rockchip_drm_private, fbdev_helper)

static int rockchip_fbdev_mmap(struct fb_info *info,
			       struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct rockchip_drm_private *private = to_drm_private(helper);

	return rockchip_gem_mmap_buf(private->fbdev_bo, vma);
}

static struct fb_ops rockchip_drm_fbdev_ops = {
	.owner		= THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_mmap	= rockchip_fbdev_mmap,
	.fb_fillrect	= drm_fb_helper_cfb_fillrect,
	.fb_copyarea	= drm_fb_helper_cfb_copyarea,
	.fb_imageblit	= drm_fb_helper_cfb_imageblit,
};

static int rockchip_drm_fbdev_create(struct drm_fb_helper *helper,
				     struct drm_fb_helper_surface_size *sizes)
{
	struct rockchip_drm_private *private = to_drm_private(helper);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_device *dev = helper->dev;
	struct rockchip_gem_object *rk_obj;
	struct drm_framebuffer *fb;
	unsigned int bytes_per_pixel;
	unsigned long offset;
	struct fb_info *fbi;
	size_t size;
	int ret;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
		sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	rk_obj = rockchip_gem_create_object(dev, size, true);
	if (IS_ERR(rk_obj))
		return -ENOMEM;

	private->fbdev_bo = &rk_obj->base;

	fbi = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		DRM_DEV_ERROR(dev->dev, "Failed to create framebuffer info.\n");
		ret = PTR_ERR(fbi);
		goto out;
	}

	helper->fb = rockchip_drm_framebuffer_init(dev, &mode_cmd,
						   private->fbdev_bo);
	if (IS_ERR(helper->fb)) {
		DRM_DEV_ERROR(dev->dev,
			      "Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(helper->fb);
		goto out;
	}

	fbi->fbops = &rockchip_drm_fbdev_ops;

	fb = helper->fb;
	drm_fb_helper_fill_info(fbi, helper, sizes);

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	dev->mode_config.fb_base = 0;
	fbi->screen_base = rk_obj->kvaddr + offset;
	fbi->screen_size = rk_obj->base.size;
	fbi->fix.smem_len = rk_obj->base.size;

	DRM_DEBUG_KMS("FB [%dx%d]-%d kvaddr=%p offset=%ld size=%zu\n",
		      fb->width, fb->height, fb->format->depth,
		      rk_obj->kvaddr,
		      offset, size);

	return 0;

out:
	rockchip_gem_free_object(&rk_obj->base);
	return ret;
}

static const struct drm_fb_helper_funcs rockchip_drm_fb_helper_funcs = {
	.fb_probe = rockchip_drm_fbdev_create,
};

int rockchip_drm_fbdev_init(struct drm_device *dev)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;
	int ret;

	if (!dev->mode_config.num_crtc || !dev->mode_config.num_connector)
		return -EINVAL;

	helper = &private->fbdev_helper;

	drm_fb_helper_prepare(dev, helper, &rockchip_drm_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, helper, ROCKCHIP_MAX_CONNECTOR);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev,
			      "Failed to initialize drm fb helper - %d.\n",
			      ret);
		return ret;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev,
			      "Failed to add connectors - %d.\n", ret);
		goto err_drm_fb_helper_fini;
	}

	ret = drm_fb_helper_initial_config(helper, PREFERRED_BPP);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev,
			      "Failed to set initial hw config - %d.\n",
			      ret);
		goto err_drm_fb_helper_fini;
	}

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(helper);
	return ret;
}

void rockchip_drm_fbdev_fini(struct drm_device *dev)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;

	helper = &private->fbdev_helper;

	drm_fb_helper_unregister_fbi(helper);

	if (helper->fb)
		drm_framebuffer_put(helper->fb);

	drm_fb_helper_fini(helper);
}
