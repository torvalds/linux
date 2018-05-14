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
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_print.h>
#include <linux/module.h>

#define DEFAULT_FBDEFIO_DELAY_MS 50

struct drm_fbdev_cma {
	struct drm_fb_helper	fb_helper;
	const struct drm_framebuffer_funcs *fb_funcs;
};

/**
 * DOC: framebuffer cma helper functions
 *
 * Provides helper functions for creating a cma (contiguous memory allocator)
 * backed framebuffer.
 *
 * drm_gem_fb_create() is used in the &drm_mode_config_funcs.fb_create
 * callback function to create a cma backed framebuffer.
 *
 * An fbdev framebuffer backed by cma is also available by calling
 * drm_fb_cma_fbdev_init(). drm_fb_cma_fbdev_fini() tears it down.
 * If the &drm_framebuffer_funcs.dirty callback is set, fb_deferred_io will be
 * set up automatically. &drm_framebuffer_funcs.dirty is called by
 * drm_fb_helper_deferred_io() in process context (&struct delayed_work).
 *
 * Example fbdev deferred io code::
 *
 *     static int driver_fb_dirty(struct drm_framebuffer *fb,
 *                                struct drm_file *file_priv,
 *                                unsigned flags, unsigned color,
 *                                struct drm_clip_rect *clips,
 *                                unsigned num_clips)
 *     {
 *         struct drm_gem_cma_object *cma = drm_fb_cma_get_gem_obj(fb, 0);
 *         ... push changes ...
 *         return 0;
 *     }
 *
 *     static struct drm_framebuffer_funcs driver_fb_funcs = {
 *         .destroy       = drm_gem_fb_destroy,
 *         .create_handle = drm_gem_fb_create_handle,
 *         .dirty         = driver_fb_dirty,
 *     };
 *
 * Initialize::
 *
 *     fbdev = drm_fb_cma_fbdev_init_with_funcs(dev, 16,
 *                                           dev->mode_config.num_crtc,
 *                                           dev->mode_config.num_connector,
 *                                           &driver_fb_funcs);
 *
 */

static inline struct drm_fbdev_cma *to_fbdev_cma(struct drm_fb_helper *helper)
{
	return container_of(helper, struct drm_fbdev_cma, fb_helper);
}

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
	struct drm_gem_object *gem;

	gem = drm_gem_fb_get_obj(fb, plane);
	if (!gem)
		return NULL;

	return to_drm_gem_cma_obj(gem);
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_obj);

/**
 * drm_fb_cma_get_gem_addr() - Get physical address for framebuffer
 * @fb: The framebuffer
 * @state: Which state of drm plane
 * @plane: Which plane
 * Return the CMA GEM address for given framebuffer.
 *
 * This function will usually be called from the PLANE callback functions.
 */
dma_addr_t drm_fb_cma_get_gem_addr(struct drm_framebuffer *fb,
				   struct drm_plane_state *state,
				   unsigned int plane)
{
	struct drm_gem_cma_object *obj;
	dma_addr_t paddr;

	obj = drm_fb_cma_get_gem_obj(fb, plane);
	if (!obj)
		return 0;

	paddr = obj->paddr + fb->offsets[plane];
	paddr += fb->format->cpp[plane] * (state->src_x >> 16);
	paddr += fb->pitches[plane] * (state->src_y >> 16);

	return paddr;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_addr);

static int drm_fb_cma_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(info->device, vma, info->screen_base,
				     info->fix.smem_start, info->fix.smem_len);
}

static struct fb_ops drm_fbdev_cma_ops = {
	.owner		= THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_fillrect	= drm_fb_helper_sys_fillrect,
	.fb_copyarea	= drm_fb_helper_sys_copyarea,
	.fb_imageblit	= drm_fb_helper_sys_imageblit,
	.fb_mmap	= drm_fb_cma_mmap,
};

static int drm_fbdev_cma_deferred_io_mmap(struct fb_info *info,
					  struct vm_area_struct *vma)
{
	fb_deferred_io_mmap(info, vma);
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return 0;
}

static int drm_fbdev_cma_defio_init(struct fb_info *fbi,
				    struct drm_gem_cma_object *cma_obj)
{
	struct fb_deferred_io *fbdefio;
	struct fb_ops *fbops;

	/*
	 * Per device structures are needed because:
	 * fbops: fb_deferred_io_cleanup() clears fbops.fb_mmap
	 * fbdefio: individual delays
	 */
	fbdefio = kzalloc(sizeof(*fbdefio), GFP_KERNEL);
	fbops = kzalloc(sizeof(*fbops), GFP_KERNEL);
	if (!fbdefio || !fbops) {
		kfree(fbdefio);
		kfree(fbops);
		return -ENOMEM;
	}

	/* can't be offset from vaddr since dirty() uses cma_obj */
	fbi->screen_buffer = cma_obj->vaddr;
	/* fb_deferred_io_fault() needs a physical address */
	fbi->fix.smem_start = page_to_phys(virt_to_page(fbi->screen_buffer));

	*fbops = *fbi->fbops;
	fbi->fbops = fbops;

	fbdefio->delay = msecs_to_jiffies(DEFAULT_FBDEFIO_DELAY_MS);
	fbdefio->deferred_io = drm_fb_helper_deferred_io;
	fbi->fbdefio = fbdefio;
	fb_deferred_io_init(fbi);
	fbi->fbops->fb_mmap = drm_fbdev_cma_deferred_io_mmap;

	return 0;
}

static void drm_fbdev_cma_defio_fini(struct fb_info *fbi)
{
	if (!fbi->fbdefio)
		return;

	fb_deferred_io_cleanup(fbi);
	kfree(fbi->fbdefio);
	kfree(fbi->fbops);
}

static int
drm_fbdev_cma_create(struct drm_fb_helper *helper,
	struct drm_fb_helper_surface_size *sizes)
{
	struct drm_fbdev_cma *fbdev_cma = to_fbdev_cma(helper);
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
	size = sizes->surface_width * sizes->surface_height * bytes_per_pixel;
	obj = drm_gem_cma_create(dev, size);
	if (IS_ERR(obj))
		return -ENOMEM;

	fbi = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		ret = PTR_ERR(fbi);
		goto err_gem_free_object;
	}

	fb = drm_gem_fbdev_fb_create(dev, sizes, 0, &obj->base,
				     fbdev_cma->fb_funcs);
	if (IS_ERR(fb)) {
		dev_err(dev->dev, "Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(fb);
		goto err_fb_info_destroy;
	}

	helper->fb = fb;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &drm_fbdev_cma_ops;

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(fbi, helper, sizes->fb_width, sizes->fb_height);

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	dev->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = obj->vaddr + offset;
	fbi->fix.smem_start = (unsigned long)(obj->paddr + offset);
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	if (fb->funcs->dirty) {
		ret = drm_fbdev_cma_defio_init(fbi, obj);
		if (ret)
			goto err_cma_destroy;
	}

	return 0;

err_cma_destroy:
	drm_framebuffer_remove(fb);
err_fb_info_destroy:
	drm_fb_helper_fini(helper);
err_gem_free_object:
	drm_gem_object_put_unlocked(&obj->base);
	return ret;
}

static const struct drm_fb_helper_funcs drm_fb_cma_helper_funcs = {
	.fb_probe = drm_fbdev_cma_create,
};

/**
 * drm_fb_cma_fbdev_init_with_funcs() - Allocate and initialize fbdev emulation
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device.
 *                 @dev->mode_config.preferred_depth is used if this is zero.
 * @max_conn_count: Maximum number of connectors.
 *                  @dev->mode_config.num_connector is used if this is zero.
 * @funcs: Framebuffer functions, in particular a custom dirty() callback.
 *         Can be NULL.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_fb_cma_fbdev_init_with_funcs(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int max_conn_count,
	const struct drm_framebuffer_funcs *funcs)
{
	struct drm_fbdev_cma *fbdev_cma;
	struct drm_fb_helper *fb_helper;
	int ret;

	if (!preferred_bpp)
		preferred_bpp = dev->mode_config.preferred_depth;
	if (!preferred_bpp)
		preferred_bpp = 32;

	if (!max_conn_count)
		max_conn_count = dev->mode_config.num_connector;

	fbdev_cma = kzalloc(sizeof(*fbdev_cma), GFP_KERNEL);
	if (!fbdev_cma)
		return -ENOMEM;

	fbdev_cma->fb_funcs = funcs;
	fb_helper = &fbdev_cma->fb_helper;

	drm_fb_helper_prepare(dev, fb_helper, &drm_fb_cma_helper_funcs);

	ret = drm_fb_helper_init(dev, fb_helper, max_conn_count);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "Failed to initialize fbdev helper.\n");
		goto err_free;
	}

	ret = drm_fb_helper_single_add_all_connectors(fb_helper);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "Failed to add connectors.\n");
		goto err_drm_fb_helper_fini;
	}

	ret = drm_fb_helper_initial_config(fb_helper, preferred_bpp);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "Failed to set fbdev configuration.\n");
		goto err_drm_fb_helper_fini;
	}

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_free:
	kfree(fbdev_cma);

	return ret;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_fbdev_init_with_funcs);

/**
 * drm_fb_cma_fbdev_init() - Allocate and initialize fbdev emulation
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device.
 *                 @dev->mode_config.preferred_depth is used if this is zero.
 * @max_conn_count: Maximum number of connectors.
 *                  @dev->mode_config.num_connector is used if this is zero.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_fb_cma_fbdev_init(struct drm_device *dev, unsigned int preferred_bpp,
			  unsigned int max_conn_count)
{
	return drm_fb_cma_fbdev_init_with_funcs(dev, preferred_bpp,
						max_conn_count, NULL);
}
EXPORT_SYMBOL_GPL(drm_fb_cma_fbdev_init);

/**
 * drm_fb_cma_fbdev_fini() - Teardown fbdev emulation
 * @dev: DRM device
 */
void drm_fb_cma_fbdev_fini(struct drm_device *dev)
{
	struct drm_fb_helper *fb_helper = dev->fb_helper;

	if (!fb_helper)
		return;

	/* Unregister if it hasn't been done already */
	if (fb_helper->fbdev && fb_helper->fbdev->dev)
		drm_fb_helper_unregister_fbi(fb_helper);

	if (fb_helper->fbdev)
		drm_fbdev_cma_defio_fini(fb_helper->fbdev);

	if (fb_helper->fb)
		drm_framebuffer_remove(fb_helper->fb);

	drm_fb_helper_fini(fb_helper);
	kfree(to_fbdev_cma(fb_helper));
}
EXPORT_SYMBOL_GPL(drm_fb_cma_fbdev_fini);

/**
 * drm_fbdev_cma_init_with_funcs() - Allocate and initializes a drm_fbdev_cma struct
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device
 * @max_conn_count: Maximum number of connectors
 * @funcs: fb helper functions, in particular a custom dirty() callback
 *
 * Returns a newly allocated drm_fbdev_cma struct or a ERR_PTR.
 */
struct drm_fbdev_cma *drm_fbdev_cma_init_with_funcs(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int max_conn_count,
	const struct drm_framebuffer_funcs *funcs)
{
	struct drm_fbdev_cma *fbdev_cma;
	struct drm_fb_helper *helper;
	int ret;

	fbdev_cma = kzalloc(sizeof(*fbdev_cma), GFP_KERNEL);
	if (!fbdev_cma) {
		dev_err(dev->dev, "Failed to allocate drm fbdev.\n");
		return ERR_PTR(-ENOMEM);
	}
	fbdev_cma->fb_funcs = funcs;

	helper = &fbdev_cma->fb_helper;

	drm_fb_helper_prepare(dev, helper, &drm_fb_cma_helper_funcs);

	ret = drm_fb_helper_init(dev, helper, max_conn_count);
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
		dev_err(dev->dev, "Failed to set initial hw configuration.\n");
		goto err_drm_fb_helper_fini;
	}

	return fbdev_cma;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(helper);
err_free:
	kfree(fbdev_cma);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_init_with_funcs);

static const struct drm_framebuffer_funcs drm_fb_cma_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
};

/**
 * drm_fbdev_cma_init() - Allocate and initializes a drm_fbdev_cma struct
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device
 * @max_conn_count: Maximum number of connectors
 *
 * Returns a newly allocated drm_fbdev_cma struct or a ERR_PTR.
 */
struct drm_fbdev_cma *drm_fbdev_cma_init(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int max_conn_count)
{
	return drm_fbdev_cma_init_with_funcs(dev, preferred_bpp,
					     max_conn_count,
					     &drm_fb_cma_funcs);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_init);

/**
 * drm_fbdev_cma_fini() - Free drm_fbdev_cma struct
 * @fbdev_cma: The drm_fbdev_cma struct
 */
void drm_fbdev_cma_fini(struct drm_fbdev_cma *fbdev_cma)
{
	drm_fb_helper_unregister_fbi(&fbdev_cma->fb_helper);
	if (fbdev_cma->fb_helper.fbdev)
		drm_fbdev_cma_defio_fini(fbdev_cma->fb_helper.fbdev);

	if (fbdev_cma->fb_helper.fb)
		drm_framebuffer_remove(fbdev_cma->fb_helper.fb);

	drm_fb_helper_fini(&fbdev_cma->fb_helper);
	kfree(fbdev_cma);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_fini);

/**
 * drm_fbdev_cma_restore_mode() - Restores initial framebuffer mode
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 *
 * This function is usually called from the &drm_driver.lastclose callback.
 */
void drm_fbdev_cma_restore_mode(struct drm_fbdev_cma *fbdev_cma)
{
	if (fbdev_cma)
		drm_fb_helper_restore_fbdev_mode_unlocked(&fbdev_cma->fb_helper);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_restore_mode);

/**
 * drm_fbdev_cma_hotplug_event() - Poll for hotpulug events
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 *
 * This function is usually called from the &drm_mode_config.output_poll_changed
 * callback.
 */
void drm_fbdev_cma_hotplug_event(struct drm_fbdev_cma *fbdev_cma)
{
	if (fbdev_cma)
		drm_fb_helper_hotplug_event(&fbdev_cma->fb_helper);
}
EXPORT_SYMBOL_GPL(drm_fbdev_cma_hotplug_event);

/**
 * drm_fbdev_cma_set_suspend - wrapper around drm_fb_helper_set_suspend
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * Calls drm_fb_helper_set_suspend, which is a wrapper around
 * fb_set_suspend implemented by fbdev core.
 */
void drm_fbdev_cma_set_suspend(struct drm_fbdev_cma *fbdev_cma, bool state)
{
	if (fbdev_cma)
		drm_fb_helper_set_suspend(&fbdev_cma->fb_helper, state);
}
EXPORT_SYMBOL(drm_fbdev_cma_set_suspend);

/**
 * drm_fbdev_cma_set_suspend_unlocked - wrapper around
 *                                      drm_fb_helper_set_suspend_unlocked
 * @fbdev_cma: The drm_fbdev_cma struct, may be NULL
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * Calls drm_fb_helper_set_suspend, which is a wrapper around
 * fb_set_suspend implemented by fbdev core.
 */
void drm_fbdev_cma_set_suspend_unlocked(struct drm_fbdev_cma *fbdev_cma,
					bool state)
{
	if (fbdev_cma)
		drm_fb_helper_set_suspend_unlocked(&fbdev_cma->fb_helper,
						   state);
}
EXPORT_SYMBOL(drm_fbdev_cma_set_suspend_unlocked);
