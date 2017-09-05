/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_fb.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#include "vbox_drv.h"
#include "vboxvideo.h"

#ifdef CONFIG_DRM_KMS_FB_HELPER
static struct fb_deferred_io vbox_defio = {
	.delay = HZ / 30,
	.deferred_io = drm_fb_helper_deferred_io,
};
#endif

static struct fb_ops vboxfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static int vboxfb_create_object(struct vbox_fbdev *fbdev,
				struct DRM_MODE_FB_CMD *mode_cmd,
				struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = fbdev->helper.dev;
	u32 size;
	struct drm_gem_object *gobj;
	u32 pitch = mode_cmd->pitches[0];
	int ret;

	size = pitch * mode_cmd->height;
	ret = vbox_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;

	return 0;
}

static int vboxfb_create(struct drm_fb_helper *helper,
			 struct drm_fb_helper_surface_size *sizes)
{
	struct vbox_fbdev *fbdev =
	    container_of(helper, struct vbox_fbdev, helper);
	struct drm_device *dev = fbdev->helper.dev;
	struct DRM_MODE_FB_CMD mode_cmd;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	struct drm_gem_object *gobj;
	struct vbox_bo *bo;
	int size, ret;
	u32 pitch;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	pitch = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	mode_cmd.pitches[0] = pitch;

	size = pitch * mode_cmd.height;

	ret = vboxfb_create_object(fbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	ret = vbox_framebuffer_init(dev, &fbdev->afb, &mode_cmd, gobj);
	if (ret)
		return ret;

	bo = gem_to_vbox_bo(gobj);

	ret = vbox_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = vbox_bo_pin(bo, TTM_PL_FLAG_VRAM, NULL);
	if (ret) {
		vbox_bo_unreserve(bo);
		return ret;
	}

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
	vbox_bo_unreserve(bo);
	if (ret) {
		DRM_ERROR("failed to kmap fbcon\n");
		return ret;
	}

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info))
		return -PTR_ERR(info);

	info->par = fbdev;

	fbdev->size = size;

	fb = &fbdev->afb.base;
	fbdev->helper.fb = fb;

	strcpy(info->fix.id, "vboxdrmfb");

	/*
	 * The last flag forces a mode set on VT switches even if the kernel
	 * does not think it is needed.
	 */
	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT |
		      FBINFO_MISC_ALWAYS_SETPAR;
	info->fbops = &vboxfb_ops;

	/*
	 * This seems to be done for safety checking that the framebuffer
	 * is not registered twice by different drivers.
	 */
	info->apertures->ranges[0].base = pci_resource_start(dev->pdev, 0);
	info->apertures->ranges[0].size = pci_resource_len(dev->pdev, 0);

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, &fbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

#ifdef CONFIG_DRM_KMS_FB_HELPER
	info->fbdefio = &vbox_defio;
	fb_deferred_io_init(info);
#endif

	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n", fb->width, fb->height);

	return 0;
}

static struct drm_fb_helper_funcs vbox_fb_helper_funcs = {
	.fb_probe = vboxfb_create,
};

void vbox_fbdev_fini(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_fbdev *fbdev = vbox->fbdev;
	struct vbox_framebuffer *afb = &fbdev->afb;

#ifdef CONFIG_DRM_KMS_FB_HELPER
	if (fbdev->helper.fbdev && fbdev->helper.fbdev->fbdefio)
		fb_deferred_io_cleanup(fbdev->helper.fbdev);
#endif

	drm_fb_helper_unregister_fbi(&fbdev->helper);

	if (afb->obj) {
		struct vbox_bo *bo = gem_to_vbox_bo(afb->obj);

		if (!vbox_bo_reserve(bo, false)) {
			if (bo->kmap.virtual)
				ttm_bo_kunmap(&bo->kmap);
			/*
			 * QXL does this, but is it really needed before
			 * freeing?
			 */
			if (bo->pin_count)
				vbox_bo_unpin(bo);
			vbox_bo_unreserve(bo);
		}
		drm_gem_object_put_unlocked(afb->obj);
		afb->obj = NULL;
	}
	drm_fb_helper_fini(&fbdev->helper);

	drm_framebuffer_unregister_private(&afb->base);
	drm_framebuffer_cleanup(&afb->base);
}

int vbox_fbdev_init(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_fbdev *fbdev;
	int ret;

	fbdev = devm_kzalloc(dev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	vbox->fbdev = fbdev;
	spin_lock_init(&fbdev->dirty_lock);

	drm_fb_helper_prepare(dev, &fbdev->helper, &vbox_fb_helper_funcs);
	ret = drm_fb_helper_init(dev, &fbdev->helper, vbox->num_crtcs);
	if (ret)
		return ret;

	ret = drm_fb_helper_single_add_all_connectors(&fbdev->helper);
	if (ret)
		goto err_fini;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(&fbdev->helper, 32);
	if (ret)
		goto err_fini;

	return 0;

err_fini:
	drm_fb_helper_fini(&fbdev->helper);
	return ret;
}

void vbox_fbdev_set_base(struct vbox_private *vbox, unsigned long gpu_addr)
{
	struct fb_info *fbdev = vbox->fbdev->helper.fbdev;

	fbdev->fix.smem_start = fbdev->apertures->ranges[0].base + gpu_addr;
	fbdev->fix.smem_len = vbox->available_vram_size - gpu_addr;
}
