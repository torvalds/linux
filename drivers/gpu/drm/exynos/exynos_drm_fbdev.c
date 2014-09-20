/* exynos_drm_fbdev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_iommu.h"

#define MAX_CONNECTOR		4
#define PREFERRED_BPP		32

#define to_exynos_fbdev(x)	container_of(x, struct exynos_drm_fbdev,\
				drm_fb_helper)

struct exynos_drm_fbdev {
	struct drm_fb_helper		drm_fb_helper;
	struct exynos_drm_gem_obj	*exynos_gem_obj;
};

static int exynos_drm_fb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct exynos_drm_fbdev *exynos_fbd = to_exynos_fbdev(helper);
	struct exynos_drm_gem_obj *exynos_gem_obj = exynos_fbd->exynos_gem_obj;
	struct exynos_drm_gem_buf *buffer = exynos_gem_obj->buffer;
	unsigned long vm_size;
	int ret;

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

	vm_size = vma->vm_end - vma->vm_start;

	if (vm_size > buffer->size)
		return -EINVAL;

	ret = dma_mmap_attrs(helper->dev->dev, vma, buffer->pages,
		buffer->dma_addr, buffer->size, &buffer->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	return 0;
}

static struct fb_ops exynos_drm_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_mmap        = exynos_drm_fb_mmap,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_setcmap	= drm_fb_helper_setcmap,
};

static int exynos_drm_fbdev_update(struct drm_fb_helper *helper,
				     struct drm_framebuffer *fb)
{
	struct fb_info *fbi = helper->fbdev;
	struct drm_device *dev = helper->dev;
	struct exynos_drm_gem_buf *buffer;
	unsigned int size = fb->width * fb->height * (fb->bits_per_pixel >> 3);
	unsigned long offset;

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	/* RGB formats use only one buffer */
	buffer = exynos_drm_fb_buffer(fb, 0);
	if (!buffer) {
		DRM_DEBUG_KMS("buffer is null.\n");
		return -EFAULT;
	}

	/* map pages with kernel virtual space. */
	if (!buffer->kvaddr) {
		if (is_drm_iommu_supported(dev)) {
			unsigned int nr_pages = buffer->size >> PAGE_SHIFT;

			buffer->kvaddr = (void __iomem *) vmap(buffer->pages,
					nr_pages, VM_MAP,
					pgprot_writecombine(PAGE_KERNEL));
		} else {
			phys_addr_t dma_addr = buffer->dma_addr;
			if (dma_addr)
				buffer->kvaddr = (void __iomem *)phys_to_virt(dma_addr);
			else
				buffer->kvaddr = (void __iomem *)NULL;
		}
		if (!buffer->kvaddr) {
			DRM_ERROR("failed to map pages to kernel space.\n");
			return -EIO;
		}
	}

	/* buffer count to framebuffer always is 1 at booting time. */
	exynos_drm_fb_set_buf_cnt(fb, 1);

	offset = fbi->var.xoffset * (fb->bits_per_pixel >> 3);
	offset += fbi->var.yoffset * fb->pitches[0];

	fbi->screen_base = buffer->kvaddr + offset;
	fbi->screen_size = size;

	return 0;
}

static int exynos_drm_fbdev_create(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct exynos_drm_fbdev *exynos_fbdev = to_exynos_fbdev(helper);
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_device *dev = helper->dev;
	struct fb_info *fbi;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct platform_device *pdev = dev->platformdev;
	unsigned long size;
	int ret;

	DRM_DEBUG_KMS("surface width(%d), height(%d) and bpp(%d\n",
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * (sizes->surface_bpp >> 3);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	mutex_lock(&dev->struct_mutex);

	fbi = framebuffer_alloc(0, &pdev->dev);
	if (!fbi) {
		DRM_ERROR("failed to allocate fb info.\n");
		ret = -ENOMEM;
		goto out;
	}

	size = mode_cmd.pitches[0] * mode_cmd.height;

	exynos_gem_obj = exynos_drm_gem_create(dev, EXYNOS_BO_CONTIG, size);
	/*
	 * If physically contiguous memory allocation fails and if IOMMU is
	 * supported then try to get buffer from non physically contiguous
	 * memory area.
	 */
	if (IS_ERR(exynos_gem_obj) && is_drm_iommu_supported(dev)) {
		dev_warn(&pdev->dev, "contiguous FB allocation failed, falling back to non-contiguous\n");
		exynos_gem_obj = exynos_drm_gem_create(dev, EXYNOS_BO_NONCONTIG,
							size);
	}

	if (IS_ERR(exynos_gem_obj)) {
		ret = PTR_ERR(exynos_gem_obj);
		goto err_release_framebuffer;
	}

	exynos_fbdev->exynos_gem_obj = exynos_gem_obj;

	helper->fb = exynos_drm_framebuffer_init(dev, &mode_cmd,
			&exynos_gem_obj->base);
	if (IS_ERR(helper->fb)) {
		DRM_ERROR("failed to create drm framebuffer.\n");
		ret = PTR_ERR(helper->fb);
		goto err_destroy_gem;
	}

	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &exynos_drm_fb_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("failed to allocate cmap.\n");
		goto err_destroy_framebuffer;
	}

	ret = exynos_drm_fbdev_update(helper, helper->fb);
	if (ret < 0)
		goto err_dealloc_cmap;

	mutex_unlock(&dev->struct_mutex);
	return ret;

err_dealloc_cmap:
	fb_dealloc_cmap(&fbi->cmap);
err_destroy_framebuffer:
	drm_framebuffer_cleanup(helper->fb);
err_destroy_gem:
	exynos_drm_gem_destroy(exynos_gem_obj);
err_release_framebuffer:
	framebuffer_release(fbi);

/*
 * if failed, all resources allocated above would be released by
 * drm_mode_config_cleanup() when drm_load() had been called prior
 * to any specific driver such as fimd or hdmi driver.
 */
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static const struct drm_fb_helper_funcs exynos_drm_fb_helper_funcs = {
	.fb_probe =	exynos_drm_fbdev_create,
};

static bool exynos_drm_fbdev_is_anything_connected(struct drm_device *dev)
{
	struct drm_connector *connector;
	bool ret = false;

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->status != connector_status_connected)
			continue;

		ret = true;
		break;
	}
	mutex_unlock(&dev->mode_config.mutex);

	return ret;
}

int exynos_drm_fbdev_init(struct drm_device *dev)
{
	struct exynos_drm_fbdev *fbdev;
	struct exynos_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;
	unsigned int num_crtc;
	int ret;

	if (!dev->mode_config.num_crtc || !dev->mode_config.num_connector)
		return 0;

	if (!exynos_drm_fbdev_is_anything_connected(dev))
		return 0;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	private->fb_helper = helper = &fbdev->drm_fb_helper;

	drm_fb_helper_prepare(dev, helper, &exynos_drm_fb_helper_funcs);

	num_crtc = dev->mode_config.num_crtc;

	ret = drm_fb_helper_init(dev, helper, num_crtc, MAX_CONNECTOR);
	if (ret < 0) {
		DRM_ERROR("failed to initialize drm fb helper.\n");
		goto err_init;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DRM_ERROR("failed to register drm_fb_helper_connector.\n");
		goto err_setup;

	}

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(helper, PREFERRED_BPP);
	if (ret < 0) {
		DRM_ERROR("failed to set up hw configuration.\n");
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
	struct exynos_drm_fbdev *exynos_fbd = to_exynos_fbdev(fb_helper);
	struct exynos_drm_gem_obj *exynos_gem_obj = exynos_fbd->exynos_gem_obj;
	struct drm_framebuffer *fb;

	if (is_drm_iommu_supported(dev) && exynos_gem_obj->buffer->kvaddr)
		vunmap(exynos_gem_obj->buffer->kvaddr);

	/* release drm framebuffer and real buffer */
	if (fb_helper->fb && fb_helper->fb->funcs) {
		fb = fb_helper->fb;
		if (fb) {
			drm_framebuffer_unregister_private(fb);
			drm_framebuffer_remove(fb);
		}
	}

	/* release linux framebuffer */
	if (fb_helper->fbdev) {
		struct fb_info *info;
		int ret;

		info = fb_helper->fbdev;
		ret = unregister_framebuffer(info);
		if (ret < 0)
			DRM_DEBUG_KMS("failed unregister_framebuffer()\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	drm_fb_helper_fini(fb_helper);
}

void exynos_drm_fbdev_fini(struct drm_device *dev)
{
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_fbdev *fbdev;

	if (!private || !private->fb_helper)
		return;

	fbdev = to_exynos_fbdev(private->fb_helper);

	if (fbdev->exynos_gem_obj)
		exynos_drm_gem_destroy(fbdev->exynos_gem_obj);

	exynos_drm_fbdev_destroy(dev, private->fb_helper);
	kfree(fbdev);
	private->fb_helper = NULL;
}

void exynos_drm_fbdev_restore_mode(struct drm_device *dev)
{
	struct exynos_drm_private *private = dev->dev_private;

	if (!private || !private->fb_helper)
		return;

	drm_fb_helper_restore_fbdev_mode_unlocked(private->fb_helper);
}
