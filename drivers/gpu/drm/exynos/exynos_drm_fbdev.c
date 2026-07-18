// SPDX-License-Identifier: GPL-2.0-or-later
/* exynos_drm_fbdev.c
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
#include <drm/drm_print.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_gem.h"

#define MAX_CONNECTOR		4

static int exynos_drm_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(helper->fb, 0);

	return drm_gem_prime_mmap(obj, vma);
}

static void exynos_drm_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;

	drm_fb_helper_fini(fb_helper);

	drm_client_buffer_delete(fb_helper->buffer);
	drm_client_release(&fb_helper->client);
}

static const struct fb_ops exynos_drm_fb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_mmap        = exynos_drm_fb_mmap,
	.fb_destroy	= exynos_drm_fb_destroy,
};

static const struct drm_fb_helper_funcs exynos_drm_fbdev_helper_funcs = {
};

int exynos_drm_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
					struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_dev *client = &helper->client;
	struct drm_device *dev = client->dev;
	struct drm_file *file = client->file;
	struct fb_info *info = helper->info;
	u32 fourcc, pitch;
	u64 size;
	const struct drm_format_info *format;
	struct exynos_drm_gem *exynos_gem;
	struct drm_gem_object *obj;
	struct drm_client_buffer *buffer;
	u32 handle;
	int ret;

	DRM_DEV_DEBUG_KMS(dev->dev,
			  "surface width(%d), height(%d) and bpp(%d\n",
			  sizes->surface_width, sizes->surface_height,
			  sizes->surface_bpp);

	fourcc = drm_mode_legacy_fb_format(sizes->surface_bpp, sizes->surface_depth);
	if (fourcc == DRM_FORMAT_INVALID)
		return -EINVAL;
	format = drm_get_format_info(dev, fourcc, DRM_FORMAT_MOD_LINEAR);
	if (!format)
		return -EINVAL;
	pitch = drm_format_info_min_pitch(format, 0, sizes->surface_width);
	if (!pitch)
		return -EINVAL;
	if (check_mul_overflow(pitch, sizes->surface_height, &size))
		return -EINVAL;
	size = ALIGN(size, PAGE_SIZE);
	if (size < PAGE_SIZE)
		return -EINVAL;

	exynos_gem = exynos_drm_gem_create(dev, EXYNOS_BO_WC, size, true);
	if (IS_ERR(exynos_gem))
		return PTR_ERR(exynos_gem);
	obj = &exynos_gem->base;

	ret = drm_gem_handle_create(file, obj, &handle);
	if (ret)
		goto err_drm_gem_object_put;

	buffer = drm_client_buffer_create(client, sizes->surface_width, sizes->surface_height,
					  fourcc, handle, pitch);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		goto err_drm_gem_handle_delete;
	}

	helper->funcs = &exynos_drm_fbdev_helper_funcs;
	helper->buffer = buffer;
	helper->fb = buffer->fb;

	info->fbops = &exynos_drm_fb_ops;

	drm_fb_helper_fill_info(info, helper, sizes);

	info->flags |= FBINFO_VIRTFB;
	info->screen_buffer = exynos_gem->kvaddr;
	info->screen_size = obj->size;
	info->fix.smem_len = obj->size;

	/* The handle is only needed for creating the framebuffer. */
	drm_gem_handle_delete(file, handle);

	/* The framebuffer still holds a reference on the GEM object. */
	drm_gem_object_put(obj);

	return 0;

err_drm_gem_handle_delete:
	drm_gem_handle_delete(file, handle);
err_drm_gem_object_put:
	drm_gem_object_put(obj);
	return ret;
}
