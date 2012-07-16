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
#include <linux/console.h>

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

static void
nouveau_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&dev_priv->channel->mutex)) {
		if (dev_priv->card_type < NV_50)
			ret = nv04_fbcon_fillrect(info, rect);
		else
		if (dev_priv->card_type < NV_C0)
			ret = nv50_fbcon_fillrect(info, rect);
		else
			ret = nvc0_fbcon_fillrect(info, rect);
		mutex_unlock(&dev_priv->channel->mutex);
	}

	if (ret == 0)
		return;

	if (ret != -ENODEV)
		nouveau_fbcon_gpu_lockup(info);
	cfb_fillrect(info, rect);
}

static void
nouveau_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&dev_priv->channel->mutex)) {
		if (dev_priv->card_type < NV_50)
			ret = nv04_fbcon_copyarea(info, image);
		else
		if (dev_priv->card_type < NV_C0)
			ret = nv50_fbcon_copyarea(info, image);
		else
			ret = nvc0_fbcon_copyarea(info, image);
		mutex_unlock(&dev_priv->channel->mutex);
	}

	if (ret == 0)
		return;

	if (ret != -ENODEV)
		nouveau_fbcon_gpu_lockup(info);
	cfb_copyarea(info, image);
}

static void
nouveau_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&dev_priv->channel->mutex)) {
		if (dev_priv->card_type < NV_50)
			ret = nv04_fbcon_imageblit(info, image);
		else
		if (dev_priv->card_type < NV_C0)
			ret = nv50_fbcon_imageblit(info, image);
		else
			ret = nvc0_fbcon_imageblit(info, image);
		mutex_unlock(&dev_priv->channel->mutex);
	}

	if (ret == 0)
		return;

	if (ret != -ENODEV)
		nouveau_fbcon_gpu_lockup(info);
	cfb_imageblit(info, image);
}

static int
nouveau_fbcon_sync(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channel;
	int ret, i;

	if (!chan || !chan->accel_done || in_interrupt() ||
	    info->state != FBINFO_STATE_RUNNING ||
	    info->flags & FBINFO_HWACCEL_DISABLED)
		return 0;

	if (!mutex_trylock(&chan->mutex))
		return 0;

	ret = RING_SPACE(chan, 4);
	if (ret) {
		mutex_unlock(&chan->mutex);
		nouveau_fbcon_gpu_lockup(info);
		return 0;
	}

	if (dev_priv->card_type >= NV_C0) {
		BEGIN_NVC0(chan, 2, NvSub2D, 0x010c, 1);
		OUT_RING  (chan, 0);
		BEGIN_NVC0(chan, 2, NvSub2D, 0x0100, 1);
		OUT_RING  (chan, 0);
	} else {
		BEGIN_RING(chan, 0, 0x0104, 1);
		OUT_RING  (chan, 0);
		BEGIN_RING(chan, 0, 0x0100, 1);
		OUT_RING  (chan, 0);
	}

	nouveau_bo_wr32(chan->notifier_bo, chan->m2mf_ntfy/4 + 3, 0xffffffff);
	FIRE_RING(chan);
	mutex_unlock(&chan->mutex);

	ret = -EBUSY;
	for (i = 0; i < 100000; i++) {
		if (!nouveau_bo_rd32(chan->notifier_bo, chan->m2mf_ntfy/4 + 3)) {
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
	.fb_fillrect = nouveau_fbcon_fillrect,
	.fb_copyarea = nouveau_fbcon_copyarea,
	.fb_imageblit = nouveau_fbcon_imageblit,
	.fb_sync = nouveau_fbcon_sync,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static struct fb_ops nouveau_fbcon_sw_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
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
	struct nouveau_channel *chan;
	struct nouveau_bo *nvbo;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct pci_dev *pdev = dev->pdev;
	struct device *device = &pdev->dev;
	int size, ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = mode_cmd.width * (sizes->surface_bpp >> 3);
	mode_cmd.pitches[0] = roundup(mode_cmd.pitches[0], 256);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = roundup(size, PAGE_SIZE);

	ret = nouveau_gem_new(dev, size, 0, NOUVEAU_GEM_DOMAIN_VRAM,
			      0, 0x0000, &nvbo);
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

	chan = nouveau_nofbaccel ? NULL : dev_priv->channel;
	if (chan && dev_priv->card_type >= NV_50) {
		ret = nouveau_bo_vma_add(nvbo, chan->vm, &nfbdev->nouveau_fb.vma);
		if (ret) {
			NV_ERROR(dev, "failed to map fb into chan: %d\n", ret);
			chan = NULL;
		}
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
	info->fbops = &nouveau_fbcon_sw_ops;
	info->fix.smem_start = nvbo->bo.mem.bus.base +
			       nvbo->bo.mem.bus.offset;
	info->fix.smem_len = size;

	info->screen_base = nvbo_kmap_obj_iovirtual(nouveau_fb->nvbo);
	info->screen_size = size;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &nfbdev->helper, sizes->fb_width, sizes->fb_height);

	/* Set aperture base/size for vesafb takeover */
	info->apertures = dev_priv->apertures;
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out_unref;
	}

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	mutex_unlock(&dev->struct_mutex);

	if (dev_priv->channel && !nouveau_nofbaccel) {
		ret = -ENODEV;
		if (dev_priv->card_type < NV_50)
			ret = nv04_fbcon_accel_init(info);
		else
		if (dev_priv->card_type < NV_C0)
			ret = nv50_fbcon_accel_init(info);
		else
			ret = nvc0_fbcon_accel_init(info);

		if (ret == 0)
			info->fbops = &nouveau_fbcon_ops;
	}

	nouveau_fbcon_zfill(dev, nfbdev);

	/* To allow resizeing without swapping buffers */
	NV_INFO(dev, "allocated %dx%d fb: 0x%lx, bo %p\n",
						nouveau_fb->base.width,
						nouveau_fb->base.height,
						nvbo->bo.offset, nvbo);

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
		nouveau_bo_vma_del(nouveau_fb->nvbo, &nouveau_fb->vma);
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
	int preferred_bpp;
	int ret;

	nfbdev = kzalloc(sizeof(struct nouveau_fbdev), GFP_KERNEL);
	if (!nfbdev)
		return -ENOMEM;

	nfbdev->dev = dev;
	dev_priv->nfbdev = nfbdev;
	nfbdev->helper.funcs = &nouveau_fbcon_helper_funcs;

	ret = drm_fb_helper_init(dev, &nfbdev->helper,
				 dev->mode_config.num_crtc, 4);
	if (ret) {
		kfree(nfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&nfbdev->helper);

	if (dev_priv->vram_size <= 32 * 1024 * 1024)
		preferred_bpp = 8;
	else if (dev_priv->vram_size <= 64 * 1024 * 1024)
		preferred_bpp = 16;
	else
		preferred_bpp = 32;

	drm_fb_helper_initial_config(&nfbdev->helper, preferred_bpp);
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
	console_lock();
	if (state == 0)
		nouveau_fbcon_save_disable_accel(dev);
	fb_set_suspend(dev_priv->nfbdev->helper.fbdev, state);
	if (state == 1)
		nouveau_fbcon_restore_accel(dev);
	console_unlock();
}

void nouveau_fbcon_zfill_all(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	nouveau_fbcon_zfill(dev, dev_priv->nfbdev);
}
