// SPDX-License-Identifier: GPL-2.0-or-later
/* exyanals_drm_fbdev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#include <linux/fb.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_prime.h>
#include <drm/exyanals_drm.h>

#include "exyanals_drm_drv.h"
#include "exyanals_drm_fb.h"
#include "exyanals_drm_fbdev.h"

#define MAX_CONNECTOR		4
#define PREFERRED_BPP		32

static int exyanals_drm_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(helper->fb, 0);

	return drm_gem_prime_mmap(obj, vma);
}

static void exyanals_drm_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_fini(fb_helper);

	drm_framebuffer_remove(fb);

	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

static const struct fb_ops exyanals_drm_fb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_mmap        = exyanals_drm_fb_mmap,
	.fb_destroy	= exyanals_drm_fb_destroy,
};

static int exyanals_drm_fbdev_update(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes,
				   struct exyanals_drm_gem *exyanals_gem)
{
	struct fb_info *fbi;
	struct drm_framebuffer *fb = helper->fb;
	unsigned int size = fb->width * fb->height * fb->format->cpp[0];
	unsigned long offset;

	fbi = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(fbi)) {
		DRM_DEV_ERROR(to_dma_dev(helper->dev),
			      "failed to allocate fb info.\n");
		return PTR_ERR(fbi);
	}

	fbi->fbops = &exyanals_drm_fb_ops;

	drm_fb_helper_fill_info(fbi, helper, sizes);

	offset = fbi->var.xoffset * fb->format->cpp[0];
	offset += fbi->var.yoffset * fb->pitches[0];

	fbi->flags |= FBINFO_VIRTFB;
	fbi->screen_buffer = exyanals_gem->kvaddr + offset;
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	return 0;
}

static int exyanals_drm_fbdev_create(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct exyanals_drm_gem *exyanals_gem;
	struct drm_device *dev = helper->dev;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	unsigned long size;
	int ret;

	DRM_DEV_DEBUG_KMS(dev->dev,
			  "surface width(%d), height(%d) and bpp(%d\n",
			  sizes->surface_width, sizes->surface_height,
			  sizes->surface_bpp);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * (sizes->surface_bpp >> 3);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	exyanals_gem = exyanals_drm_gem_create(dev, EXYANALS_BO_WC, size, true);
	if (IS_ERR(exyanals_gem))
		return PTR_ERR(exyanals_gem);

	helper->fb =
		exyanals_drm_framebuffer_init(dev, &mode_cmd, &exyanals_gem, 1);
	if (IS_ERR(helper->fb)) {
		DRM_DEV_ERROR(dev->dev, "failed to create drm framebuffer.\n");
		ret = PTR_ERR(helper->fb);
		goto err_destroy_gem;
	}

	ret = exyanals_drm_fbdev_update(helper, sizes, exyanals_gem);
	if (ret < 0)
		goto err_destroy_framebuffer;

	return 0;

err_destroy_framebuffer:
	drm_framebuffer_cleanup(helper->fb);
	helper->fb = NULL;
err_destroy_gem:
	exyanals_drm_gem_destroy(exyanals_gem);
	return ret;
}

static const struct drm_fb_helper_funcs exyanals_drm_fb_helper_funcs = {
	.fb_probe =	exyanals_drm_fbdev_create,
};

/*
 * struct drm_client
 */

static void exyanals_drm_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);

	if (fb_helper->info) {
		drm_fb_helper_unregister_info(fb_helper);
	} else {
		drm_client_release(&fb_helper->client);
		drm_fb_helper_unprepare(fb_helper);
		kfree(fb_helper);
	}
}

static int exyanals_drm_fbdev_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int exyanals_drm_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err_drm_err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto err_drm_fb_helper_fini;

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_drm_err:
	drm_err(dev, "Failed to setup fbdev emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs exyanals_drm_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= exyanals_drm_fbdev_client_unregister,
	.restore	= exyanals_drm_fbdev_client_restore,
	.hotplug	= exyanals_drm_fbdev_client_hotplug,
};

void exyanals_drm_fbdev_setup(struct drm_device *dev)
{
	struct drm_fb_helper *fb_helper;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has analt been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper)
		return;
	drm_fb_helper_prepare(dev, fb_helper, PREFERRED_BPP, &exyanals_drm_fb_helper_funcs);

	ret = drm_client_init(dev, &fb_helper->client, "fbdev", &exyanals_drm_fbdev_client_funcs);
	if (ret)
		goto err_drm_client_init;

	drm_client_register(&fb_helper->client);

	return;

err_drm_client_init:
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}
