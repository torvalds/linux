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
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/reservation.h>

#define DEFAULT_FBDEFIO_DELAY_MS 50

struct drm_fb_cma {
	struct drm_framebuffer		fb;
	struct drm_gem_cma_object	*obj[4];
};

struct drm_fbdev_cma {
	struct drm_fb_helper	fb_helper;
	struct drm_fb_cma	*fb;
	const struct drm_framebuffer_funcs *fb_funcs;
};

/**
 * DOC: framebuffer cma helper functions
 *
 * Provides helper functions for creating a cma (contiguous memory allocator)
 * backed framebuffer.
 *
 * drm_fb_cma_create() is used in the &drm_mode_config_funcs.fb_create
 * callback function to create a cma backed framebuffer.
 *
 * An fbdev framebuffer backed by cma is also available by calling
 * drm_fbdev_cma_init(). drm_fbdev_cma_fini() tears it down.
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
 *         .destroy       = drm_fb_cma_destroy,
 *         .create_handle = drm_fb_cma_create_handle,
 *         .dirty         = driver_fb_dirty,
 *     };
 *
 * Initialize::
 *
 *     fbdev = drm_fbdev_cma_init_with_funcs(dev, 16,
 *                                           dev->mode_config.num_crtc,
 *                                           dev->mode_config.num_connector,
 *                                           &driver_fb_funcs);
 *
 */

static inline struct drm_fbdev_cma *to_fbdev_cma(struct drm_fb_helper *helper)
{
	return container_of(helper, struct drm_fbdev_cma, fb_helper);
}

static inline struct drm_fb_cma *to_fb_cma(struct drm_framebuffer *fb)
{
	return container_of(fb, struct drm_fb_cma, fb);
}

void drm_fb_cma_destroy(struct drm_framebuffer *fb)
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
EXPORT_SYMBOL(drm_fb_cma_destroy);

int drm_fb_cma_create_handle(struct drm_framebuffer *fb,
	struct drm_file *file_priv, unsigned int *handle)
{
	struct drm_fb_cma *fb_cma = to_fb_cma(fb);

	return drm_gem_handle_create(file_priv,
			&fb_cma->obj[0]->base, handle);
}
EXPORT_SYMBOL(drm_fb_cma_create_handle);

static struct drm_framebuffer_funcs drm_fb_cma_funcs = {
	.destroy	= drm_fb_cma_destroy,
	.create_handle	= drm_fb_cma_create_handle,
};

static struct drm_fb_cma *drm_fb_cma_alloc(struct drm_device *dev,
	const struct drm_mode_fb_cmd2 *mode_cmd,
	struct drm_gem_cma_object **obj,
	unsigned int num_planes, const struct drm_framebuffer_funcs *funcs)
{
	struct drm_fb_cma *fb_cma;
	int ret;
	int i;

	fb_cma = kzalloc(sizeof(*fb_cma), GFP_KERNEL);
	if (!fb_cma)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, &fb_cma->fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb_cma->obj[i] = obj[i];

	ret = drm_framebuffer_init(dev, &fb_cma->fb, funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n", ret);
		kfree(fb_cma);
		return ERR_PTR(ret);
	}

	return fb_cma;
}

/**
 * drm_fb_cma_create_with_funcs() - helper function for the
 *                                  &drm_mode_config_funcs.fb_create
 *                                  callback
 * @dev: DRM device
 * @file_priv: drm file for the ioctl call
 * @mode_cmd: metadata from the userspace fb creation request
 * @funcs: vtable to be used for the new framebuffer object
 *
 * This can be used to set &drm_framebuffer_funcs for drivers that need the
 * &drm_framebuffer_funcs.dirty callback. Use drm_fb_cma_create() if you don't
 * need to change &drm_framebuffer_funcs.
 */
struct drm_framebuffer *drm_fb_cma_create_with_funcs(struct drm_device *dev,
	struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd,
	const struct drm_framebuffer_funcs *funcs)
{
	const struct drm_format_info *info;
	struct drm_fb_cma *fb_cma;
	struct drm_gem_cma_object *objs[4];
	struct drm_gem_object *obj;
	int ret;
	int i;

	info = drm_format_info(mode_cmd->pixel_format);
	if (!info)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = mode_cmd->width / (i ? info->hsub : 1);
		unsigned int height = mode_cmd->height / (i ? info->vsub : 1);
		unsigned int min_size;

		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		min_size = (height - 1) * mode_cmd->pitches[i]
			 + width * info->cpp[i]
			 + mode_cmd->offsets[i];

		if (obj->size < min_size) {
			drm_gem_object_unreference_unlocked(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = to_drm_gem_cma_obj(obj);
	}

	fb_cma = drm_fb_cma_alloc(dev, mode_cmd, objs, i, funcs);
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
EXPORT_SYMBOL_GPL(drm_fb_cma_create_with_funcs);

/**
 * drm_fb_cma_create() - &drm_mode_config_funcs.fb_create callback function
 * @dev: DRM device
 * @file_priv: drm file for the ioctl call
 * @mode_cmd: metadata from the userspace fb creation request
 *
 * If your hardware has special alignment or pitch requirements these should be
 * checked before calling this function. Use drm_fb_cma_create_with_funcs() if
 * you need to set &drm_framebuffer_funcs.dirty.
 */
struct drm_framebuffer *drm_fb_cma_create(struct drm_device *dev,
	struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_fb_cma_create_with_funcs(dev, file_priv, mode_cmd,
					    &drm_fb_cma_funcs);
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

/**
 * drm_fb_cma_prepare_fb() - Prepare CMA framebuffer
 * @plane: Which plane
 * @state: Plane state attach fence to
 *
 * This should be set as the &struct drm_plane_helper_funcs.prepare_fb hook.
 *
 * This function checks if the plane FB has an dma-buf attached, extracts
 * the exclusive fence and attaches it to plane state for the atomic helper
 * to wait on.
 *
 * There is no need for cleanup_fb for CMA based framebuffer drivers.
 */
int drm_fb_cma_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	struct dma_buf *dma_buf;
	struct dma_fence *fence;

	if ((plane->state->fb == state->fb) || !state->fb)
		return 0;

	dma_buf = drm_fb_cma_get_gem_obj(state->fb, 0)->base.dma_buf;
	if (dma_buf) {
		fence = reservation_object_get_excl_rcu(dma_buf->resv);
		drm_atomic_set_fence_for_plane(state, fence);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_prepare_fb);

#ifdef CONFIG_DEBUG_FS
static void drm_fb_cma_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	struct drm_fb_cma *fb_cma = to_fb_cma(fb);
	int i;

	seq_printf(m, "fb: %dx%d@%4.4s\n", fb->width, fb->height,
			(char *)&fb->format->format);

	for (i = 0; i < fb->format->num_planes; i++) {
		seq_printf(m, "   %d: offset=%d pitch=%d, obj: ",
				i, fb->offsets[i], fb->pitches[i]);
		drm_gem_cma_describe(fb_cma->obj[i], m);
	}
}

/**
 * drm_fb_cma_debugfs_show() - Helper to list CMA framebuffer objects
 *			       in debugfs.
 * @m: output file
 * @arg: private data for the callback
 */
int drm_fb_cma_debugfs_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_framebuffer *fb;

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(fb, dev)
		drm_fb_cma_describe(fb, m);
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_debugfs_show);
#endif

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

	fbi = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		ret = PTR_ERR(fbi);
		goto err_gem_free_object;
	}

	fbdev_cma->fb = drm_fb_cma_alloc(dev, &mode_cmd, &obj, 1,
					 fbdev_cma->fb_funcs);
	if (IS_ERR(fbdev_cma->fb)) {
		dev_err(dev->dev, "Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(fbdev_cma->fb);
		goto err_fb_info_destroy;
	}

	fb = &fbdev_cma->fb->fb;
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

	if (fbdev_cma->fb_funcs->dirty) {
		ret = drm_fbdev_cma_defio_init(fbi, obj);
		if (ret)
			goto err_cma_destroy;
	}

	return 0;

err_cma_destroy:
	drm_framebuffer_remove(&fbdev_cma->fb->fb);
err_fb_info_destroy:
	drm_fb_helper_release_fbi(helper);
err_gem_free_object:
	drm_gem_object_unreference_unlocked(&obj->base);
	return ret;
}

static const struct drm_fb_helper_funcs drm_fb_cma_helper_funcs = {
	.fb_probe = drm_fbdev_cma_create,
};

/**
 * drm_fbdev_cma_init_with_funcs() - Allocate and initializes a drm_fbdev_cma struct
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device
 * @num_crtc: Number of CRTCs
 * @max_conn_count: Maximum number of connectors
 * @funcs: fb helper functions, in particular a custom dirty() callback
 *
 * Returns a newly allocated drm_fbdev_cma struct or a ERR_PTR.
 */
struct drm_fbdev_cma *drm_fbdev_cma_init_with_funcs(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int num_crtc,
	unsigned int max_conn_count, const struct drm_framebuffer_funcs *funcs)
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
	return drm_fbdev_cma_init_with_funcs(dev, preferred_bpp, num_crtc,
				max_conn_count, &drm_fb_cma_funcs);
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
	drm_fb_helper_release_fbi(&fbdev_cma->fb_helper);

	if (fbdev_cma->fb)
		drm_framebuffer_remove(&fbdev_cma->fb->fb);

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
void drm_fbdev_cma_set_suspend(struct drm_fbdev_cma *fbdev_cma, int state)
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
					int state)
{
	if (fbdev_cma)
		drm_fb_helper_set_suspend_unlocked(&fbdev_cma->fb_helper,
						   state);
}
EXPORT_SYMBOL(drm_fbdev_cma_set_suspend_unlocked);
