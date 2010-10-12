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
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/screen_info.h>
#include <linux/vga_switcheroo.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_fb_helper.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_crtc.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_dma.h"

static int
nouveau_fbcon_sync(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	int ret, i;

	if (!chan || !chan->accel_done ||
	    info->state != FBINFO_STATE_RUNNING ||
	    info->flags & FBINFO_HWACCEL_DISABLED)
		return 0;

	if (RING_SPACE(chan, 4)) {
		nouveau_fbcon_gpu_lockup(info);
		return 0;
	}

	BEGIN_RING(chan, 0, 0x0104, 1);
	OUT_RING(chan, 0);
	BEGIN_RING(chan, 0, 0x0100, 1);
	OUT_RING(chan, 0);
	nouveau_bo_wr32(chan->notifier_bo, chan->m2mf_ntfy + 3, 0xffffffff);
	FIRE_RING(chan);

	ret = -EBUSY;
	for (i = 0; i < 100000; i++) {
		if (!nouveau_bo_rd32(chan->notifier_bo, chan->m2mf_ntfy + 3)) {
			ret = 0;
			break;
		}
		DRM_UDELAY(1);
	}

	if (ret) {
		nouveau_fbcon_gpu_lockup(info);
		return 0;
	}

	chan->accel_done = false;
	return 0;
}

static struct fb_ops nouveau_fbcon_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_sync = nouveau_fbcon_sync,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static struct fb_ops nv04_fbcon_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = nv04_fbcon_fillrect,
	.fb_copyarea = nv04_fbcon_copyarea,
	.fb_imageblit = nv04_fbcon_imageblit,
	.fb_sync = nouveau_fbcon_sync,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static struct fb_ops nv50_fbcon_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = nv50_fbcon_fillrect,
	.fb_copyarea = nv50_fbcon_copyarea,
	.fb_imageblit = nv50_fbcon_imageblit,
	.fb_sync = nouveau_fbcon_sync,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static void nouveau_fbcon_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				    u16 blue, int regno)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	nv_crtc->lut.r[regno] = red;
	nv_crtc->lut.g[regno] = green;
	nv_crtc->lut.b[regno] = blue;
}

static void nouveau_fbcon_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, int regno)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

	*red = nv_crtc->lut.r[regno];
	*green = nv_crtc->lut.g[regno];
	*blue = nv_crtc->lut.b[regno];
}

static void
nouveau_fbcon_zfill(struct drm_device *dev, struct nouveau_fbdev *nfbdev)
{
	struct fb_info *info = nfbdev->helper.fbdev;
	struct fb_fillrect rect;

	/* Clear the entire fbcon.  The drm will program every connector
	 * with it's preferred mode.  If the sizes differ, one display will
	 * quite likely have garbage around the console.
	 */
	rect.dx = rect.dy = 0;
	rect.width = info->var.xres_virtual;
	rect.height = info->var.yres_virtual;
	rect.color = 0;
	rect.rop = ROP_COPY;
	info->fbops->fb_fillrect(info, &rect);
}

static int
nouveau_fbcon_create(struct nouveau_fbdev *nfbdev,
		     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct nouveau_framebuffer *nouveau_fb;
	struct nouveau_bo *nvbo;
	struct drm_mode_fb_cmd mode_cmd;
	struct pci_dev *pdev = dev->pdev;
	struct device *device = &pdev->dev;
	int size, ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.pitch = mode_cmd.width * (mode_cmd.bpp >> 3);
	mode_cmd.pitch = roundup(mode_cmd.pitch, 256);
	mode_cmd.depth = sizes->surface_depth;

	size = mode_cmd.pitch * mode_cmd.height;
	size = roundup(size, PAGE_SIZE);

	ret = nouveau_gem_new(dev, dev_priv->channel, size, 0, TTM_PL_FLAG_VRAM,
			      0, 0x0000, false, true, &nvbo);
	if (ret) {
		NV_ERROR(dev, "failed to allocate framebuffer\n");
		goto out;
	}

	ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "failed to pin fb: %d\n", ret);
		nouveau_bo_ref(NULL, &nvbo);
		goto out;
	}

	ret = nouveau_bo_map(nvbo);
	if (ret) {
		NV_ERROR(dev, "failed to map fb: %d\n", ret);
		nouveau_bo_unpin(nvbo);
		nouveau_bo_ref(NULL, &nvbo);
		goto out;
	}

	mutex_lock(&dev->struct_mutex);

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out_unref;
	}

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->par = nfbdev;

	nouveau_framebuffer_init(dev, &nfbdev->nouveau_fb, &mode_cmd, nvbo);

	nouveau_fb = &nfbdev->nouveau_fb;
	fb = &nouveau_fb->base;

	/* setup helper */
	nfbdev->helper.fb = fb;
	nfbdev->helper.fbdev = info;

	strcpy(info->fix.id, "nouveaufb");
	if (nouveau_nofbaccel)
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_DISABLED;
	else
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_COPYAREA |
			      FBINFO_HWACCEL_FILLRECT |
			      FBINFO_HWACCEL_IMAGEBLIT;
	info->flags |= FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &nouveau_fbcon_ops;
	info->fix.smem_start = dev->mode_config.fb_base + nvbo->bo.offset -
			       dev_priv->vm_vram_base;
	info->fix.smem_len = size;

	info->screen_base = nvbo_kmap_obj_iovirtual(nouveau_fb->nvbo);
	info->screen_size = size;

	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(info, &nfbdev->helper, sizes->fb_width, sizes->fb_height);

	/* FIXME: we really shouldn't expose mmio space at all */
	info->fix.mmio_start = pci_resource_start(pdev, 1);
	info->fix.mmio_len = pci_resource_len(pdev, 1);

	/* Set aperture base/size for vesafb takeover */
	info->apertures = dev_priv->apertures;
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	if (dev_priv->channel && !nouveau_nofbaccel) {
		switch (dev_priv->card_type) {
		case NV_C0:
			break;
		case NV_50:
			nv50_fbcon_accel_init(info);
			info->fbops = &nv50_fbcon_ops;
			break;
		default:
			nv04_fbcon_accel_init(info);
			info->fbops = &nv04_fbcon_ops;
			break;
		};
	}

	nouveau_fbcon_zfill(dev, nfbdev);

	/* To allow resizeing without swapping buffers */
	NV_INFO(dev, "allocated %dx%d fb: 0x%lx, bo %p\n",
						nouveau_fb->base.width,
						nouveau_fb->base.height,
						nvbo->bo.offset, nvbo);

	mutex_unlock(&dev->struct_mutex);
	vga_switcheroo_client_fb_set(dev->pdev, info);
	return 0;

out_unref:
	mutex_unlock(&dev->struct_mutex);
out:
	return ret;
}

static int
nouveau_fbcon_find_or_create_single(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct nouveau_fbdev *nfbdev = (struct nouveau_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = nouveau_fbcon_create(nfbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

void
nouveau_fbcon_output_poll_changed(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	drm_fb_helper_hotplug_event(&dev_priv->nfbdev->helper);
}

static int
nouveau_fbcon_destroy(struct drm_device *dev, struct nouveau_fbdev *nfbdev)
{
	struct nouveau_framebuffer *nouveau_fb = &nfbdev->nouveau_fb;
	struct fb_info *info;

	if (nfbdev->helper.fbdev) {
		info = nfbdev->helper.fbdev;
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (nouveau_fb->nvbo) {
		nouveau_bo_unmap(nouveau_fb->nvbo);
		drm_gem_object_unreference_unlocked(nouveau_fb->nvbo->gem);
		nouveau_fb->nvbo = NULL;
	}
	drm_fb_helper_fini(&nfbdev->helper);
	drm_framebuffer_cleanup(&nouveau_fb->base);
	return 0;
}

void nouveau_fbcon_gpu_lockup(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;

	NV_ERROR(dev, "GPU lockup - switching to software fbcon\n");
	info->flags |= FBINFO_HWACCEL_DISABLED;
}

static struct drm_fb_helper_funcs nouveau_fbcon_helper_funcs = {
	.gamma_set = nouveau_fbcon_gamma_set,
	.gamma_get = nouveau_fbcon_gamma_get,
	.fb_probe = nouveau_fbcon_find_or_create_single,
};


int nouveau_fbcon_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fbdev *nfbdev;
	int ret;

	nfbdev = kzalloc(sizeof(struct nouveau_fbdev), GFP_KERNEL);
	if (!nfbdev)
		return -ENOMEM;

	nfbdev->dev = dev;
	dev_priv->nfbdev = nfbdev;
	nfbdev->helper.funcs = &nouveau_fbcon_helper_funcs;

	ret = drm_fb_helper_init(dev, &nfbdev->helper,
				 nv_two_heads(dev) ? 2 : 1, 4);
	if (ret) {
		kfree(nfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&nfbdev->helper);
	drm_fb_helper_initial_config(&nfbdev->helper, 32);
	return 0;
}

void nouveau_fbcon_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (!dev_priv->nfbdev)
		return;

	nouveau_fbcon_destroy(dev, dev_priv->nfbdev);
	kfree(dev_priv->nfbdev);
	dev_priv->nfbdev = NULL;
}

void nouveau_fbcon_save_disable_accel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->nfbdev->saved_flags = dev_priv->nfbdev->helper.fbdev->flags;
	dev_priv->nfbdev->helper.fbdev->flags |= FBINFO_HWACCEL_DISABLED;
}

void nouveau_fbcon_restore_accel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	dev_priv->nfbdev->helper.fbdev->flags = dev_priv->nfbdev->saved_flags;
}

void nouveau_fbcon_set_suspend(struct drm_device *dev, int state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	fb_set_suspend(dev_priv->nfbdev->helper.fbdev, state);
}

void nouveau_fbcon_zfill_all(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	nouveau_fbcon_zfill(dev, dev_priv->nfbdev);
}
