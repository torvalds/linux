// SPDX-License-Identifier: GPL-2.0-or-later
/* exynos_drm_fbdev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"

#define MAX_CONNECTOR		4
#define PREFERRED_BPP		32

#define to_exynos_fbdev(x)	container_of(x, struct exynos_drm_fbdev,\
				drm_fb_helper)

struct exynos_drm_fbdev {
	struct drm_fb_helper	drm_fb_helper;
	struct exynos_drm_gem	*exynos_gem;
};

static int exynos_drm_fb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct exynos_drm_fbdev *exynos_fbd = to_exynos_fbdev(helper);
	struct exynos_drm_gem *exynos_gem = exynos_fbd->exynos_gem;
	unsigned long vm_size;
	int ret;

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

	vm_size = vma->vm_end - vma->vm_start;

	if (vm_size > exynos_gem->size)
		return -EINVAL;

	ret = dma_mmap_attrs(to_dma_dev(helper->dev), vma, exynos_gem->cookie,
			     exynos_gem->dma_addr, exynos_gem->size,
			     exynos_gem->dma_attrs);
	if (ret < 0) {
		DRM_DEV_ERROR(to_dma_dev(helper->dev), "failed to mmap.\n");
		return ret;
	}

	return 0;
}

static const struct fb_ops exynos_drm_fb_ops = {
	.owner		= THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_mmap        = exynos_drm_fb_mmap,
	.fb_fillrect	= drm_fb_helper_cfb_fillrect,
	.fb_copyarea	= drm_fb_helper_cfb_copyarea,
	.fb_imageblit	= drm_fb_helper_cfb_imageblit,
};

static int exynos_drm_fbdev_update(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes,
				   struct exynos_drm_gem *exynos_gem)
{
	struct fb_info *fbi;
	struct drm_framebuffer *fb = helper->fb;
	unsigned int size = fb->width * fb->height * fb->format->cpp[0];
	unsigned long offset;

	fbi = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		DRM_DEV_ERROR(to_dma_dev(helper->dev),
			      "failed to allocate fb info.\n");
		return PTR_ERR(fbi);
	}

	fbi->fbops = &exynos_drm_fb_ops;

	drm_fb_helper_fill_info(fbi, helper, sizes);

	offset = fbi->var.xoffset * fb->format->cpp[0];
	offset += fbi->var.yoffset * fb->pitches[0];

	fbi->screen_base = exynos_gem->kvaddr + offset;
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	return 0;
}

static int exynos_drm_fbdev_create(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct exynos_drm_fbdev *exynos_fbdev = to_exynos_fbdev(helper);
	struct exynos_drm_gem *exynos_gem;
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

	exynos_gem = exynos_drm_gem_create(dev, EXYNOS_BO_WC, size, true);
	if (IS_ERR(exynos_gem))
		return PTR_ERR(exynos_gem);

	exynos_fbdev->exynos_gem = exynos_gem;

	helper->fb =
		exynos_drm_framebuffer_init(dev, &mode_cmd, &exynos_gem, 1);
	if (IS_ERR(helper->fb)) {
		DRM_DEV_ERROR(dev->dev, "failed to create drm framebuffer.\n");
		ret = PTR_ERR(helper->fb);
		goto err_destroy_gem;
	}

	ret = exynos_drm_fbdev_update(helper, sizes, exynos_gem);
	if (ret < 0)
		goto err_destroy_framebuffer;

	return ret;

err_destroy_framebuffer:
	drm_framebuffer_cleanup(helper->fb);
err_destroy_gem:
	exynos_drm_gem_destroy(exynos_gem);

	/*
	 * if failed, all resources allocated above would be released by
	 * drm_mode_config_cleanup() when drm_load() had been called prior
	 * to any specific driver such as fimd or hdmi driver.
	 */

	return ret;
}

static const struct drm_fb_helper_funcs exynos_drm_fb_helper_funcs = {
	.fb_probe =	exynos_drm_fbdev_create,
};

int exynos_drm_fbdev_init(struct drm_device *dev)
{
	struct exynos_drm_fbdev *fbdev;
	struct exynos_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;
	int ret;

	if (!dev->mode_config.num_crtc)
		return 0;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	private->fb_helper = helper = &fbdev->drm_fb_helper;

	drm_fb_helper_prepare(dev, helper, &exynos_drm_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, helper);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev,
			      "failed to initialize drm fb helper.\n");
		goto err_init;
	}

	ret = drm_fb_helper_initial_config(helper, PREFERRED_BPP);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev,
			      "failed to set up hw configuration.\n");
		goto err_setup;
	}

	return 0;

err_setup:
	drm_fb_helper_fini(helper);

err_init:
	private->fb_helper = NULL;
	kfree(fbdev);

	return ret;
}

static void exynos_drm_fbdev_destroy(struct drm_device *dev,
				      struct drm_fb_helper *fb_helper)
{
	struct drm_framebuffer *fb;

	/* release drm framebuffer and real buffer */
	if (fb_helper->fb && fb_helper->fb->funcs) {
		fb = fb_helper->fb;
		if (fb)
			drm_framebuffer_remove(fb);
	}

	drm_fb_helper_unregister_fbi(fb_helper);

	drm_fb_helper_fini(fb_helper);
}

void exynos_drm_fbdev_fini(struct drm_device *dev)
{
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_fbdev *fbdev;

	if (!private || !private->fb_helper)
		return;

	fbdev = to_exynos_fbdev(private->fb_helper);

	exynos_drm_fbdev_destroy(dev, private->fb_helper);
	kfree(fbdev);
	private->fb_helper = NULL;
}

