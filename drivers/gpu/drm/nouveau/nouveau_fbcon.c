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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include "nouveau_drm.h"
#include "nouveau_gem.h"
#include "nouveau_bo.h"
#include "nouveau_fbcon.h"
#include "nouveau_chan.h"

#include "nouveau_crtc.h"

#include <core/client.h>
#include <core/device.h>

#include <subdev/fb.h>

MODULE_PARM_DESC(nofbaccel, "Disable fbcon acceleration");
static int nouveau_nofbaccel = 0;
module_param_named(nofbaccel, nouveau_nofbaccel, int, 0400);

static void
nouveau_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *fbcon = info->par;
	struct nouveau_drm *drm = nouveau_drm(fbcon->dev);
	struct nouveau_device *device = nv_device(drm->device);
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&drm->client.mutex)) {
		if (device->card_type < NV_50)
			ret = nv04_fbcon_fillrect(info, rect);
		else
		if (device->card_type < NV_C0)
			ret = nv50_fbcon_fillrect(info, rect);
		else
			ret = nvc0_fbcon_fillrect(info, rect);
		mutex_unlock(&drm->client.mutex);
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
	struct nouveau_fbdev *fbcon = info->par;
	struct nouveau_drm *drm = nouveau_drm(fbcon->dev);
	struct nouveau_device *device = nv_device(drm->device);
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&drm->client.mutex)) {
		if (device->card_type < NV_50)
			ret = nv04_fbcon_copyarea(info, image);
		else
		if (device->card_type < NV_C0)
			ret = nv50_fbcon_copyarea(info, image);
		else
			ret = nvc0_fbcon_copyarea(info, image);
		mutex_unlock(&drm->client.mutex);
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
	struct nouveau_fbdev *fbcon = info->par;
	struct nouveau_drm *drm = nouveau_drm(fbcon->dev);
	struct nouveau_device *device = nv_device(drm->device);
	int ret;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	ret = -ENODEV;
	if (!in_interrupt() && !(info->flags & FBINFO_HWACCEL_DISABLED) &&
	    mutex_trylock(&drm->client.mutex)) {
		if (device->card_type < NV_50)
			ret = nv04_fbcon_imageblit(info, image);
		else
		if (device->card_type < NV_C0)
			ret = nv50_fbcon_imageblit(info, image);
		else
			ret = nvc0_fbcon_imageblit(info, image);
		mutex_unlock(&drm->client.mutex);
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
	struct nouveau_fbdev *fbcon = info->par;
	struct nouveau_drm *drm = nouveau_drm(fbcon->dev);
	struct nouveau_channel *chan = drm->channel;
	int ret;

	if (!chan || !chan->accel_done || in_interrupt() ||
	    info->state != FBINFO_STATE_RUNNING ||
	    info->flags & FBINFO_HWACCEL_DISABLED)
		return 0;

	if (!mutex_trylock(&drm->client.mutex))
		return 0;

	ret = nouveau_channel_idle(chan);
	mutex_unlock(&drm->client.mutex);
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
nouveau_fbcon_zfill(struct drm_device *dev, struct nouveau_fbdev *fbcon)
{
	struct fb_info *info = fbcon->helper.fbdev;
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
nouveau_fbcon_create(struct drm_fb_helper *helper,
		     struct drm_fb_helper_surface_size *sizes)
{
	struct nouveau_fbdev *fbcon = (struct nouveau_fbdev *)helper;
	struct drm_device *dev = fbcon->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nv_device(drm->device);
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct nouveau_framebuffer *nouveau_fb;
	struct nouveau_channel *chan;
	struct nouveau_bo *nvbo;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct pci_dev *pdev = dev->pdev;
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
		NV_ERROR(drm, "failed to allocate framebuffer\n");
		goto out;
	}

	ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(drm, "failed to pin fb: %d\n", ret);
		nouveau_bo_ref(NULL, &nvbo);
		goto out;
	}

	ret = nouveau_bo_map(nvbo);
	if (ret) {
		NV_ERROR(drm, "failed to map fb: %d\n", ret);
		nouveau_bo_unpin(nvbo);
		nouveau_bo_ref(NULL, &nvbo);
		goto out;
	}

	chan = nouveau_nofbaccel ? NULL : drm->channel;
	if (chan && device->card_type >= NV_50) {
		ret = nouveau_bo_vma_add(nvbo, nv_client(chan->cli)->vm,
					&fbcon->nouveau_fb.vma);
		if (ret) {
			NV_ERROR(drm, "failed to map fb into chan: %d\n", ret);
			chan = NULL;
		}
	}

	mutex_lock(&dev->struct_mutex);

	info = framebuffer_alloc(0, &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto out_unref;
	}

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->par = fbcon;

	nouveau_framebuffer_init(dev, &fbcon->nouveau_fb, &mode_cmd, nvbo);

	nouveau_fb = &fbcon->nouveau_fb;
	fb = &nouveau_fb->base;

	/* setup helper */
	fbcon->helper.fb = fb;
	fbcon->helper.fbdev = info;

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
	drm_fb_helper_fill_var(info, &fbcon->helper, sizes->fb_width, sizes->fb_height);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	mutex_unlock(&dev->struct_mutex);

	if (chan) {
		ret = -ENODEV;
		if (device->card_type < NV_50)
			ret = nv04_fbcon_accel_init(info);
		else
		if (device->card_type < NV_C0)
			ret = nv50_fbcon_accel_init(info);
		else
			ret = nvc0_fbcon_accel_init(info);

		if (ret == 0)
			info->fbops = &nouveau_fbcon_ops;
	}

	nouveau_fbcon_zfill(dev, fbcon);

	/* To allow resizeing without swapping buffers */
	NV_INFO(drm, "allocated %dx%d fb: 0x%lx, bo %p\n",
		nouveau_fb->base.width, nouveau_fb->base.height,
		nvbo->bo.offset, nvbo);

	vga_switcheroo_client_fb_set(dev->pdev, info);
	return 0;

out_unref:
	mutex_unlock(&dev->struct_mutex);
out:
	return ret;
}

void
nouveau_fbcon_output_poll_changed(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	drm_fb_helper_hotplug_event(&drm->fbcon->helper);
}

static int
nouveau_fbcon_destroy(struct drm_device *dev, struct nouveau_fbdev *fbcon)
{
	struct nouveau_framebuffer *nouveau_fb = &fbcon->nouveau_fb;
	struct fb_info *info;

	if (fbcon->helper.fbdev) {
		info = fbcon->helper.fbdev;
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
	drm_fb_helper_fini(&fbcon->helper);
	drm_framebuffer_unregister_private(&nouveau_fb->base);
	drm_framebuffer_cleanup(&nouveau_fb->base);
	return 0;
}

void nouveau_fbcon_gpu_lockup(struct fb_info *info)
{
	struct nouveau_fbdev *fbcon = info->par;
	struct nouveau_drm *drm = nouveau_drm(fbcon->dev);

	NV_ERROR(drm, "GPU lockup - switching to software fbcon\n");
	info->flags |= FBINFO_HWACCEL_DISABLED;
}

static struct drm_fb_helper_funcs nouveau_fbcon_helper_funcs = {
	.gamma_set = nouveau_fbcon_gamma_set,
	.gamma_get = nouveau_fbcon_gamma_get,
	.fb_probe = nouveau_fbcon_create,
};


int
nouveau_fbcon_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	struct nouveau_fbdev *fbcon;
	int preferred_bpp;
	int ret;

	if (!dev->mode_config.num_crtc)
		return 0;

	fbcon = kzalloc(sizeof(struct nouveau_fbdev), GFP_KERNEL);
	if (!fbcon)
		return -ENOMEM;

	fbcon->dev = dev;
	drm->fbcon = fbcon;
	fbcon->helper.funcs = &nouveau_fbcon_helper_funcs;

	ret = drm_fb_helper_init(dev, &fbcon->helper,
				 dev->mode_config.num_crtc, 4);
	if (ret) {
		kfree(fbcon);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&fbcon->helper);

	if (pfb->ram->size <= 32 * 1024 * 1024)
		preferred_bpp = 8;
	else
	if (pfb->ram->size <= 64 * 1024 * 1024)
		preferred_bpp = 16;
	else
		preferred_bpp = 32;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	drm_fb_helper_initial_config(&fbcon->helper, preferred_bpp);
	return 0;
}

void
nouveau_fbcon_fini(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (!drm->fbcon)
		return;

	nouveau_fbcon_destroy(dev, drm->fbcon);
	kfree(drm->fbcon);
	drm->fbcon = NULL;
}

void nouveau_fbcon_save_disable_accel(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	drm->fbcon->saved_flags = drm->fbcon->helper.fbdev->flags;
	drm->fbcon->helper.fbdev->flags |= FBINFO_HWACCEL_DISABLED;
}

void nouveau_fbcon_restore_accel(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	drm->fbcon->helper.fbdev->flags = drm->fbcon->saved_flags;
}

void nouveau_fbcon_set_suspend(struct drm_device *dev, int state)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	console_lock();
	if (state == 0)
		nouveau_fbcon_save_disable_accel(dev);
	fb_set_suspend(drm->fbcon->helper.fbdev, state);
	if (state == 1)
		nouveau_fbcon_restore_accel(dev);
	console_unlock();
}

void nouveau_fbcon_zfill_all(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	nouveau_fbcon_zfill(dev, drm->fbcon);
}
