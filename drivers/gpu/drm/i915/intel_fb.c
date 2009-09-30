/*
 * Copyright © 2007 David Airlie
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     David Airlie
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_fb_helper.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

struct intelfb_par {
	struct drm_fb_helper helper;
	struct intel_framebuffer *intel_fb;
	struct drm_display_mode *our_mode;
};

static struct fb_ops intelfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_setcolreg = drm_fb_helper_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
};

static struct drm_fb_helper_funcs intel_fb_helper_funcs = {
	.gamma_set = intel_crtc_fb_gamma_set,
};


/**
 * Curretly it is assumed that the old framebuffer is reused.
 *
 * LOCKING
 * caller should hold the mode config lock.
 *
 */
int intelfb_resize(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_display_mode *mode = crtc->desired_mode;

	fb = crtc->fb;
	if (!fb)
		return 1;

	info = fb->fbdev;
	if (!info)
		return 1;

	if (!mode)
		return 1;

	info->var.xres = mode->hdisplay;
	info->var.right_margin = mode->hsync_start - mode->hdisplay;
	info->var.hsync_len = mode->hsync_end - mode->hsync_start;
	info->var.left_margin = mode->htotal - mode->hsync_end;
	info->var.yres = mode->vdisplay;
	info->var.lower_margin = mode->vsync_start - mode->vdisplay;
	info->var.vsync_len = mode->vsync_end - mode->vsync_start;
	info->var.upper_margin = mode->vtotal - mode->vsync_end;
	info->var.pixclock = 10000000 / mode->htotal * 1000 / mode->vtotal * 100;
	/* avoid overflow */
	info->var.pixclock = info->var.pixclock * 1000 / mode->vrefresh;

	return 0;
}
EXPORT_SYMBOL(intelfb_resize);

static int intelfb_create(struct drm_device *dev, uint32_t fb_width,
			  uint32_t fb_height, uint32_t surface_width,
			  uint32_t surface_height,
			  uint32_t surface_depth, uint32_t surface_bpp,
			  struct drm_framebuffer **fb_p)
{
	struct fb_info *info;
	struct intelfb_par *par;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
	struct drm_mode_fb_cmd mode_cmd;
	struct drm_gem_object *fbo = NULL;
	struct drm_i915_gem_object *obj_priv;
	struct device *device = &dev->pdev->dev;
	int size, ret, mmio_bar = IS_I9XX(dev) ? 0 : 1;

	mode_cmd.width = surface_width;
	mode_cmd.height = surface_height;

	mode_cmd.bpp = surface_bpp;
	mode_cmd.pitch = ALIGN(mode_cmd.width * ((mode_cmd.bpp + 1) / 8), 64);
	mode_cmd.depth = surface_depth;

	size = mode_cmd.pitch * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);
	fbo = drm_gem_object_alloc(dev, size);
	if (!fbo) {
		DRM_ERROR("failed to allocate framebuffer\n");
		ret = -ENOMEM;
		goto out;
	}
	obj_priv = fbo->driver_private;

	mutex_lock(&dev->struct_mutex);

	ret = i915_gem_object_pin(fbo, PAGE_SIZE);
	if (ret) {
		DRM_ERROR("failed to pin fb: %d\n", ret);
		goto out_unref;
	}

	/* Flush everything out, we'll be doing GTT only from now on */
	i915_gem_object_set_to_gtt_domain(fbo, 1);

	ret = intel_framebuffer_create(dev, &mode_cmd, &fb, fbo);
	if (ret) {
		DRM_ERROR("failed to allocate fb.\n");
		goto out_unpin;
	}

	list_add(&fb->filp_head, &dev->mode_config.fb_kernel_list);

	intel_fb = to_intel_framebuffer(fb);
	*fb_p = fb;

	info = framebuffer_alloc(sizeof(struct intelfb_par), device);
	if (!info) {
		ret = -ENOMEM;
		goto out_unpin;
	}

	par = info->par;

	par->helper.funcs = &intel_fb_helper_funcs;
	par->helper.dev = dev;
	ret = drm_fb_helper_init_crtc_count(&par->helper, 2,
					    INTELFB_CONN_LIMIT);
	if (ret)
		goto out_unref;

	strcpy(info->fix.id, "inteldrmfb");

	info->flags = FBINFO_DEFAULT;

	info->fbops = &intelfb_ops;


	/* setup aperture base/size for vesafb takeover */
	info->aperture_base = dev->mode_config.fb_base;
	if (IS_I9XX(dev))
		info->aperture_size = pci_resource_len(dev->pdev, 2);
	else
		info->aperture_size = pci_resource_len(dev->pdev, 0);

	info->fix.smem_start = dev->mode_config.fb_base + obj_priv->gtt_offset;
	info->fix.smem_len = size;

	info->flags = FBINFO_DEFAULT;

	info->screen_base = ioremap_wc(dev->agp->base + obj_priv->gtt_offset,
				       size);
	if (!info->screen_base) {
		ret = -ENOSPC;
		goto out_unpin;
	}
	info->screen_size = size;

//	memset(info->screen_base, 0, size);

	drm_fb_helper_fill_fix(info, fb->pitch);
	drm_fb_helper_fill_var(info, fb, fb_width, fb_height);

	/* FIXME: we really shouldn't expose mmio space at all */
	info->fix.mmio_start = pci_resource_start(dev->pdev, mmio_bar);
	info->fix.mmio_len = pci_resource_len(dev->pdev, mmio_bar);

	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	fb->fbdev = info;

	par->intel_fb = intel_fb;

	/* To allow resizeing without swapping buffers */
	DRM_DEBUG("allocated %dx%d fb: 0x%08x, bo %p\n", intel_fb->base.width,
		  intel_fb->base.height, obj_priv->gtt_offset, fbo);

	mutex_unlock(&dev->struct_mutex);
	return 0;

out_unpin:
	i915_gem_object_unpin(fbo);
out_unref:
	drm_gem_object_unreference(fbo);
	mutex_unlock(&dev->struct_mutex);
out:
	return ret;
}

int intelfb_probe(struct drm_device *dev)
{
	int ret;

	DRM_DEBUG("\n");
	ret = drm_fb_helper_single_fb_probe(dev, intelfb_create);
	return ret;
}
EXPORT_SYMBOL(intelfb_probe);

int intelfb_remove(struct drm_device *dev, struct drm_framebuffer *fb)
{
	struct fb_info *info;

	if (!fb)
		return -EINVAL;

	info = fb->fbdev;

	if (info) {
		struct intelfb_par *par = info->par;
		unregister_framebuffer(info);
		iounmap(info->screen_base);
		if (info->par)
			drm_fb_helper_free(&par->helper);
		framebuffer_release(info);
	}

	return 0;
}
EXPORT_SYMBOL(intelfb_remove);
MODULE_LICENSE("GPL and additional rights");
