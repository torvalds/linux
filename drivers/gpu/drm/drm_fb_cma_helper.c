/*
 * drm kms/fb cma (contiguous memory allocator) helper functions
 *
 * Copyright (C) 2012 Analog Device Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Based on udl_fbdev.c
 *  Copyright (C) 2012 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <linux/module.h>

struct drm_fb_cma {
	struct drm_framebuffer		fb;
	struct drm_gem_cma_object	*obj[4];
};

struct drm_fbdev_cma {
	struct drm_fb_helper	fb_helper;
	struct drm_fb_cma	*fb;
};

static inline struct drm_fbdev_cma *to_fbdev_cma(struct drm_fb_helper *helper)
{
	return container_of(helper, struct drm_fbdev_cma, fb_helper);
}

static inline struct drm_fb_cma *to_fb_cma(struct drm_framebuffer *fb)
{
	return container_of(fb, struct drm_fb_cma, fb);
}

static void drm_fb_cma_destroy(struct drm_framebuffer *fb)
{
	struct drm_fb_cma *fb_cma = to_fb_cma(fb);
	int i;

	for (i = 0; i < 4; i++) {
		if (fb_cma->obj[i])
			drm_gem_object_unreference_unlocked(&fb_cma->obj[i]->base);
	}

	drm_framebuffer_cleanup(fb);
	kfree(fb_cma);
}

static int drm_fb_cma_create_handle(struct drm_framebuffer *fb,
	struct drm_file *file_priv, unsigned int *handle)
{
	struct drm_fb_cma *fb_cma = to_fb_cma(fb);

	return drm_gem_handle_create(file_priv,
			&fb_cma->obj[0]->base, handle);
}

static struct drm_framebuffer_funcs drm_fb_cma_funcs = {
	.destroy	= drm_fb_cma_destroy,
	.create_handle	= drm_fb_cma_create_handle,
};

static struct drm_fb_cma *drm_fb_cma_alloc(struct drm_device *dev,
	struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_cma_object **obj,
	unsigned int num_planes)
{
	struct drm_fb_cma *fb_cma;
	int ret;
	int i;

	fb_cma = kzalloc(sizeof(*fb_cma), GFP_KERNEL);
	if (!fb_cma)
		return ERR_PTR(-ENOMEM);

	ret = drm_framebuffer_init(dev, &fb_cma->fb, &drm_fb_cma_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initalize framebuffer: %d\n", ret);
		kfree(fb_cma);
		return ERR_PTR(ret);
	}

	drm_helper_mode_fill_fb_struct(&fb_cma->fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb_cma->obj[i] = obj[i];

	return fb_cma;
}

/**
 * drm_fb_cma_create() - (struct drm_mode_config_funcs *)->fb_create callback function
 *
 * If your hardware has special alignment or pitch requirements these should be
 * checked before calling this function.
 */
struct drm_framebuffer *drm_fb_cma_create(struct drm_device *dev,
	struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_fb_cma *fb_cma;
	struct drm_gem_cma_object *objs[4];
	struct drm_gem_object *obj;
	unsigned int hsub;
	unsigned int vsub;
	int ret;
	int i;

	hsub = drm_format_horz_chroma_subsampling(mode_cmd->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(mode_cmd->pixel_format);

	for (i = 0; i < drm_format_num_planes(mode_cmd->pixel_format); i++) {
		unsigned int width = mode_cmd->width / (i ? hsub : 1);
		unsigned int height = mode_cmd->height / (i ? vsub : 1);
		unsigned int min_size;

		obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		min_size = (height - 1) * mode_cmd->pitches[i]
			 + width * drm_format_plane_cpp(mode_cmd->pixel_format, i)
			 + mode_cmd->offsets[i];

		if (obj->size < min_size) {
			drm_gem_object_unreference_unlocked(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = to_drm_gem_cma_obj(obj);
	}

	fb_cma = drm_fb_cma_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb_cma)) {
		ret = PTR_ERR(fb_cma);
		goto err_gem_object_unreference;
	}

	return &fb_cma->fb;

err_gem_object_unreference:
	for (i--; i >= 0; i--)
		drm_gem_object_unreference_unlocked(&objs[i]->base);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_fb_cma_create);

/**
 * drm_fb_cma_get_gem_obj() - Get CMA GEM object for framebuffer
 * @fb: The framebuffer
 * @plane: Which plane
 *
 * Return the CMA GEM object for given framebuffer.
 *
 * This function will usually be called from the CRTC callback functions.
 */
struct drm_gem_cma_object *drm_fb_cma_get_gem_obj(struct drm_framebuffer *fb,
	unsigned int plane)
{
	struct drm_fb_cma *fb_cma = to_fb_cma(fb);

	if (plane >= 4)
		return NULL;

	return fb_cma->obj[plane];
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_obj);

static struct fb_ops drm_fbdev_cma_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_setcmap	= drm_fb_helper_setcmap,
};

static int drm_fbdev_cma_create(struct drm_fb_helper *helper,
	struct drm_fb_helper_surface_size *sizes)
{
	struct drm_fbdev_cma *fbdev_cma = to_fbdev_cma(helper);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_device *dev = helper->dev;
	struct drm_gem_cma_object *obj;
	struct drm_framebuffer *fb;
	unsigned int bytes_per_pixel;
	unsigned long offset;
	struct fb_info *fbi;
	size_t size;
	int ret;

	DRM_DEBUG_KMS("surface width(%d), height(%d) and bpp(%d)\n",
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
		sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	obj = drm_gem_cma_create(dev, size);
	if (IS_ERR(obj))
		return -ENOMEM;

	fbi = framebuffer_alloc(0, dev->dev);
	if (!fbi) {
		dev_err(dev->dev, "Failed to allocate framebuffer info.\n");
		ret = -ENOMEM;
		goto err_drm_gem_cma_free_object;
	}

	fbdev_cma->fb = drm_fb_cma_alloc(dev, &mode_cmd, &obj, 1);
	if (IS_ERR(fbdev_cma->fb)) {
		dev_err(dev->dev, "Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(fbdev_cma->fb);
		goto err_framebuffer_release;
	}

	fb = &fbdev_cma->fb->fb;
	helper->fb = fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &drm_fbdev_cma_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		dev_err(dev->dev, "Failed to allocate color map.\n");
		goto err_drm_fb_cma_destroy;
	}

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	dev->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = obj->vaddr + offset;
	fbi->fix.smem_start = (unsigned long)(obj->paddr + offset);
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	return 0;

err_drm_fb_cma_destroy:
	drm_fb_cma_destroy(fb);
err_framebuffer_release:
	framebuffer_release(fbi);
err_drm_gem_cma_free_object:
	drm_gem_cma_free_object(&obj->base);
	return ret;
}

static int drm_fbdev_cma_probe(struct drm_fb_helper *helper,
	struct drm_fb_helper_surface_size *sizes)
{
	int ret = 0;

	if (!helper->fb) {
		ret = drm_fbdev_cma_create(helper, sizes);
		if (ret < 0)
			return ret;
		ret = 1;
	}

	return ret;
}

static struct drm_fb_helper_funcs drm_fb_cma_helper_funcs = {
	.fb_probe = drm_fbdev_cma_probe,
};

/**
 * drm_fbdev_cma_init() - Allocate and initializes a drm_fbdev_cma struct
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device
 * @num_crtc: Number of CRTCs
 * @max_conn_count: Maximum number of connectors
 *
 * Returns a newly allocated drm_fbdev_cma struct or a ERR_PTR.
 */
struct drm_fbdev_cma *drm_fbdev_cma_init(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int num_crtc,
	unsigned int max_conn_count)
{
	struct drm_fbdev_cma *fbdev_cma;
	struct drm_fb_helper *helper;
	int ret;

	fbdev_cma = kzalloc(sizeof(*fbdev_cma), GFP_KERNEL);
	if (!fbdev_cma) {
		dev_err(dev->dev, "Failed to allocate drm fbdev.\n");
		return ERR_PTR(-ENOMEM);
	}

	fbdev_cma->fb_helper.funcs = &drm_fb_cma_helper_funcs;
	helper = &fbdev_cma->fb_helper;

	ret = drm_fb_helper_init(dev, helper, num_crtc, max_conn_count);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to initialize drm fb helper.\n");
		goto err_free;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to add connectors.\n");
		goto err_drm_fb_helper_fini;

	}

	ret = drm_fb_helper_initial_config(helper, preferred_bpp);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to set inital hw configuration.\n");
		goto err_drm_fb_helper_fini;
	}

	return fbdev_cma;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(helper);
err_free:
	kfree(fbdev_cma);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_init);

/**
 * drm_fbdev_cma_fini() - Free drm_fbdev_cma struct
 * @fbdev_cma: The drm_fbdev_cma struct
 */
void drm_fbdev_cma_fini(struct drm_fbdev_cma *fbdev_cma)
{
	if (fbdev_cma->fb_helper.fbdev) {
		struct fb_info *info;
		int ret;

		info = fbdev_cma->fb_helper.fbdev;
		ret = unregister_framebuffer(info);
		if (ret < 0)
			DRM_DEBUG_KMS("failed unregister_framebuffer()\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	if (fbdev_cma->fb)
		drm_fb_cma_destroy(&fbdev_cma->fb->fb);

	drm_fb_helper_fini(&fbdev_cma->fb_helper);
	kfree(fbdev_cma);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_fini);

/**
 * drm_fbdev_cma_restore_mode() - Restores initial framebuffer mode
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 *
 * This function is usually called from the DRM drivers lastclose callback.
 */
void drm_fbdev_cma_restore_mode(struct drm_fbdev_cma *fbdev_cma)
{
	if (fbdev_cma)
		drm_fb_helper_restore_fbdev_mode(&fbdev_cma->fb_helper);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_restore_mode);

/**
 * drm_fbdev_cma_hotplug_event() - Poll for hotpulug events
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 *
 * This function is usually called from the DRM drivers output_poll_changed
 * callback.
 */
void drm_fbdev_cma_hotplug_event(struct drm_fbdev_cma *fbdev_cma)
{
	if (fbdev_cma)
		drm_fb_helper_hotplug_event(&fbdev_cma->fb_helper);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_hotplug_event);
