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
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;
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
	.fb_setcolreg = drm_fb_helper_setcolreg,
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
	.fb_setcolreg = drm_fb_helper_setcolreg,
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
	.fb_setcolreg = drm_fb_helper_setcolreg,
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

static struct drm_fb_helper_funcs nouveau_fbcon_helper_funcs = {
	.gamma_set = nouveau_fbcon_gamma_set,
	.gamma_get = nouveau_fbcon_gamma_get
};

#if defined(__i386__) || defined(__x86_64__)
static bool
nouveau_fbcon_has_vesafb_or_efifb(struct drm_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int ramin;

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_VLFB &&
	    screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return false;

	if (screen_info.lfb_base < pci_resource_start(pdev, 1))
		goto not_fb;

	if (screen_info.lfb_base + screen_info.lfb_size >=
	    pci_resource_start(pdev, 1) + pci_resource_len(pdev, 1))
		goto not_fb;

	return true;
not_fb:
	ramin = 2;
	if (pci_resource_len(pdev, ramin) == 0) {
		ramin = 3;
		if (pci_resource_len(pdev, ramin) == 0)
			return false;
	}

	if (screen_info.lfb_base < pci_resource_start(pdev, ramin))
		return false;

	if (screen_info.lfb_base + screen_info.lfb_size >=
	    pci_resource_start(pdev, ramin) + pci_resource_len(pdev, ramin))
		return false;

	return true;
}
#endif

void
nouveau_fbcon_zfill(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct fb_info *info = dev_priv->fbdev_info;
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
nouveau_fbcon_create(struct drm_device *dev, uint32_t fb_width,
		     uint32_t fb_height, uint32_t surface_width,
		     uint32_t surface_height, uint32_t surface_depth,
		     uint32_t surface_bpp, struct drm_framebuffer **pfb)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct fb_info *info;
	struct nouveau_fbcon_par *par;
	struct drm_framebuffer *fb;
	struct nouveau_framebuffer *nouveau_fb;
	struct nouveau_bo *nvbo;
	struct drm_mode_fb_cmd mode_cmd;
	struct device *device = &dev->pdev->dev;
	int size, ret;

	mode_cmd.width = surface_width;
	mode_cmd.height = surface_height;

	mode_cmd.bpp = surface_bpp;
	mode_cmd.pitch = mode_cmd.width * (mode_cmd.bpp >> 3);
	mode_cmd.pitch = roundup(mode_cmd.pitch, 256);
	mode_cmd.depth = surface_depth;

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

	fb = nouveau_framebuffer_create(dev, nvbo, &mode_cmd);
	if (!fb) {
		ret = -ENOMEM;
		NV_ERROR(dev, "failed to allocate fb.\n");
		goto out_unref;
	}

	list_add(&fb->filp_head, &dev->mode_config.fb_kernel_list);

	nouveau_fb = nouveau_framebuffer(fb);
	*pfb = fb;

	info = framebuffer_alloc(sizeof(struct nouveau_fbcon_par), device);
	if (!info) {
		ret = -ENOMEM;
		goto out_unref;
	}

	par = info->par;
	par->helper.funcs = &nouveau_fbcon_helper_funcs;
	par->helper.dev = dev;
	ret = drm_fb_helper_init_crtc_count(&par->helper, 2, 4);
	if (ret)
		goto out_unref;
	dev_priv->fbdev_info = info;

	strcpy(info->fix.id, "nouveaufb");
	if (nouveau_nofbaccel)
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_DISABLED;
	else
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_COPYAREA |
			      FBINFO_HWACCEL_FILLRECT |
			      FBINFO_HWACCEL_IMAGEBLIT;
	info->fbops = &nouveau_fbcon_ops;
	info->fix.smem_start = dev->mode_config.fb_base + nvbo->bo.offset -
			       dev_priv->vm_vram_base;
	info->fix.smem_len = size;

	info->screen_base = nvbo_kmap_obj_iovirtual(nouveau_fb->nvbo);
	info->screen_size = size;

	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(info, fb, fb_width, fb_height);

	/* FIXME: we really shouldn't expose mmio space at all */
	info->fix.mmio_start = pci_resource_start(dev->pdev, 1);
	info->fix.mmio_len = pci_resource_len(dev->pdev, 1);

	/* Set aperture base/size for vesafb takeover */
#if defined(__i386__) || defined(__x86_64__)
	if (nouveau_fbcon_has_vesafb_or_efifb(dev)) {
		/* Some NVIDIA VBIOS' are stupid and decide to put the
		 * framebuffer in the middle of the PRAMIN BAR for
		 * whatever reason.  We need to know the exact lfb_base
		 * to get vesafb kicked off, and the only reliable way
		 * we have left is to find out lfb_base the same way
		 * vesafb did.
		 */
		info->aperture_base = screen_info.lfb_base;
		info->aperture_size = screen_info.lfb_size;
		if (screen_info.orig_video_isVGA == VIDEO_TYPE_VLFB)
			info->aperture_size *= 65536;
	} else
#endif
	{
		info->aperture_base = info->fix.mmio_start;
		info->aperture_size = info->fix.mmio_len;
	}

	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	fb->fbdev = info;

	par->nouveau_fb = nouveau_fb;
	par->dev = dev;

	if (dev_priv->channel && !nouveau_nofbaccel) {
		switch (dev_priv->card_type) {
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

	nouveau_fbcon_zfill(dev);

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

int
nouveau_fbcon_probe(struct drm_device *dev)
{
	NV_DEBUG_KMS(dev, "\n");

	return drm_fb_helper_single_fb_probe(dev, 32, nouveau_fbcon_create);
}

int
nouveau_fbcon_remove(struct drm_device *dev, struct drm_framebuffer *fb)
{
	struct nouveau_framebuffer *nouveau_fb = nouveau_framebuffer(fb);
	struct fb_info *info;

	if (!fb)
		return -EINVAL;

	info = fb->fbdev;
	if (info) {
		struct nouveau_fbcon_par *par = info->par;

		unregister_framebuffer(info);
		nouveau_bo_unmap(nouveau_fb->nvbo);
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(nouveau_fb->nvbo->gem);
		nouveau_fb->nvbo = NULL;
		mutex_unlock(&dev->struct_mutex);
		if (par)
			drm_fb_helper_free(&par->helper);
		framebuffer_release(info);
	}

	return 0;
}

void nouveau_fbcon_gpu_lockup(struct fb_info *info)
{
	struct nouveau_fbcon_par *par = info->par;
	struct drm_device *dev = par->dev;

	NV_ERROR(dev, "GPU lockup - switching to software fbcon\n");
	info->flags |= FBINFO_HWACCEL_DISABLED;
}
