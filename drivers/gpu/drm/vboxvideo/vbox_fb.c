// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_fb.c
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/tty.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>

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
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
};

int vboxfb_create(struct drm_fb_helper *helper,
		  struct drm_fb_helper_surface_size *sizes)
{
	struct vbox_private *vbox =
		container_of(helper, struct vbox_private, fb_helper);
	struct pci_dev *pdev = vbox->ddev.pdev;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	struct drm_gem_object *gobj;
	struct vbox_bo *bo;
	int size, ret;
	u64 gpu_addr;
	u32 pitch;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	pitch = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	mode_cmd.pitches[0] = pitch;

	size = pitch * mode_cmd.height;

	ret = vbox_gem_create(vbox, size, true, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	ret = vbox_framebuffer_init(vbox, &vbox->afb, &mode_cmd, gobj);
	if (ret)
		return ret;

	bo = gem_to_vbox_bo(gobj);

	ret = vbox_bo_pin(bo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->screen_size = size;
	info->screen_base = (char __iomem *)vbox_bo_kmap(bo);
	if (IS_ERR(info->screen_base))
		return PTR_ERR(info->screen_base);

	info->par = helper;

	fb = &vbox->afb.base;
	helper->fb = fb;

	strcpy(info->fix.id, "vboxdrmfb");

	info->fbops = &vboxfb_ops;

	/*
	 * This seems to be done for safety checking that the framebuffer
	 * is not registered twice by different drivers.
	 */
	info->apertures->ranges[0].base = pci_resource_start(pdev, 0);
	info->apertures->ranges[0].size = pci_resource_len(pdev, 0);

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, helper, sizes->fb_width,
			       sizes->fb_height);

	gpu_addr = vbox_bo_gpu_offset(bo);
	info->fix.smem_start = info->apertures->ranges[0].base + gpu_addr;
	info->fix.smem_len = vbox->available_vram_size - gpu_addr;

#ifdef CONFIG_DRM_KMS_FB_HELPER
	info->fbdefio = &vbox_defio;
	fb_deferred_io_init(info);
#endif

	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n", fb->width, fb->height);

	return 0;
}

void vbox_fbdev_fini(struct vbox_private *vbox)
{
	struct vbox_framebuffer *afb = &vbox->afb;

#ifdef CONFIG_DRM_KMS_FB_HELPER
	if (vbox->fb_helper.fbdev && vbox->fb_helper.fbdev->fbdefio)
		fb_deferred_io_cleanup(vbox->fb_helper.fbdev);
#endif

	drm_fb_helper_unregister_fbi(&vbox->fb_helper);

	if (afb->obj) {
		struct vbox_bo *bo = gem_to_vbox_bo(afb->obj);

		vbox_bo_kunmap(bo);

		if (bo->pin_count)
			vbox_bo_unpin(bo);

		drm_gem_object_put_unlocked(afb->obj);
		afb->obj = NULL;
	}
	drm_fb_helper_fini(&vbox->fb_helper);

	drm_framebuffer_unregister_private(&afb->base);
	drm_framebuffer_cleanup(&afb->base);
}
