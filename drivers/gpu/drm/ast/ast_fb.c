/*
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
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
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
#include "ast_drv.h"

static void ast_dirty_update(struct ast_fbdev *afbdev,
			     int x, int y, int width, int height)
{
	int i;
	struct drm_gem_object *obj;
	struct ast_bo *bo;
	int src_offset, dst_offset;
	int bpp = (afbdev->afb.base.bits_per_pixel + 7)/8;
	int ret;
	bool unmap = false;
	bool store_for_later = false;
	int x2, y2;
	unsigned long flags;

	obj = afbdev->afb.obj;
	bo = gem_to_ast_bo(obj);

	/*
	 * try and reserve the BO, if we fail with busy
	 * then the BO is being moved and we should
	 * store up the damage until later.
	 */
	ret = ast_bo_reserve(bo, true);
	if (ret) {
		if (ret != -EBUSY)
			return;

		store_for_later = true;
	}

	x2 = x + width - 1;
	y2 = y + height - 1;
	spin_lock_irqsave(&afbdev->dirty_lock, flags);

	if (afbdev->y1 < y)
		y = afbdev->y1;
	if (afbdev->y2 > y2)
		y2 = afbdev->y2;
	if (afbdev->x1 < x)
		x = afbdev->x1;
	if (afbdev->x2 > x2)
		x2 = afbdev->x2;

	if (store_for_later) {
		afbdev->x1 = x;
		afbdev->x2 = x2;
		afbdev->y1 = y;
		afbdev->y2 = y2;
		spin_unlock_irqrestore(&afbdev->dirty_lock, flags);
		return;
	}

	afbdev->x1 = afbdev->y1 = INT_MAX;
	afbdev->x2 = afbdev->y2 = 0;
	spin_unlock_irqrestore(&afbdev->dirty_lock, flags);

	if (!bo->kmap.virtual) {
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret) {
			DRM_ERROR("failed to kmap fb updates\n");
			ast_bo_unreserve(bo);
			return;
		}
		unmap = true;
	}
	for (i = y; i <= y2; i++) {
		/* assume equal stride for now */
		src_offset = dst_offset = i * afbdev->afb.base.pitches[0] + (x * bpp);
		memcpy_toio(bo->kmap.virtual + src_offset, afbdev->sysram + src_offset, (x2 - x + 1) * bpp);

	}
	if (unmap)
		ttm_bo_kunmap(&bo->kmap);

	ast_bo_unreserve(bo);
}

static void ast_fillrect(struct fb_info *info,
			 const struct fb_fillrect *rect)
{
	struct ast_fbdev *afbdev = info->par;
	sys_fillrect(info, rect);
	ast_dirty_update(afbdev, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void ast_copyarea(struct fb_info *info,
			 const struct fb_copyarea *area)
{
	struct ast_fbdev *afbdev = info->par;
	sys_copyarea(info, area);
	ast_dirty_update(afbdev, area->dx, area->dy, area->width,
			 area->height);
}

static void ast_imageblit(struct fb_info *info,
			  const struct fb_image *image)
{
	struct ast_fbdev *afbdev = info->par;
	sys_imageblit(info, image);
	ast_dirty_update(afbdev, image->dx, image->dy, image->width,
			 image->height);
}

static struct fb_ops astfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = ast_fillrect,
	.fb_copyarea = ast_copyarea,
	.fb_imageblit = ast_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static int astfb_create_object(struct ast_fbdev *afbdev,
			       struct drm_mode_fb_cmd2 *mode_cmd,
			       struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = afbdev->helper.dev;
	u32 bpp, depth;
	u32 size;
	struct drm_gem_object *gobj;

	int ret = 0;
	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &depth, &bpp);

	size = mode_cmd->pitches[0] * mode_cmd->height;
	ret = ast_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static int astfb_create(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes)
{
	struct ast_fbdev *afbdev = (struct ast_fbdev *)helper;
	struct drm_device *dev = afbdev->helper.dev;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	int size, ret;
	struct device *device = &dev->pdev->dev;
	void *sysram;
	struct drm_gem_object *gobj = NULL;
	struct ast_bo *bo = NULL;
	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7)/8);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	ret = astfb_create_object(afbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}
	bo = gem_to_ast_bo(gobj);

	sysram = vmalloc(size);
	if (!sysram)
		return -ENOMEM;

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out;
	}
	info->par = afbdev;

	ret = ast_framebuffer_init(dev, &afbdev->afb, &mode_cmd, gobj);
	if (ret)
		goto out;

	afbdev->sysram = sysram;
	afbdev->size = size;

	fb = &afbdev->afb.base;
	afbdev->helper.fb = fb;
	afbdev->helper.fbdev = info;

	strcpy(info->fix.id, "astdrmfb");

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &astfb_ops;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out;
	}

	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out;
	}
	info->apertures->ranges[0].base = pci_resource_start(dev->pdev, 0);
	info->apertures->ranges[0].size = pci_resource_len(dev->pdev, 0);

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &afbdev->helper, sizes->fb_width, sizes->fb_height);

	info->screen_base = sysram;
	info->screen_size = size;

	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n",
		      fb->width, fb->height);

	return 0;
out:
	return ret;
}

static void ast_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			       u16 blue, int regno)
{
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	ast_crtc->lut_r[regno] = red >> 8;
	ast_crtc->lut_g[regno] = green >> 8;
	ast_crtc->lut_b[regno] = blue >> 8;
}

static void ast_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			       u16 *blue, int regno)
{
	struct ast_crtc *ast_crtc = to_ast_crtc(crtc);
	*red = ast_crtc->lut_r[regno] << 8;
	*green = ast_crtc->lut_g[regno] << 8;
	*blue = ast_crtc->lut_b[regno] << 8;
}

static struct drm_fb_helper_funcs ast_fb_helper_funcs = {
	.gamma_set = ast_fb_gamma_set,
	.gamma_get = ast_fb_gamma_get,
	.fb_probe = astfb_create,
};

static void ast_fbdev_destroy(struct drm_device *dev,
			      struct ast_fbdev *afbdev)
{
	struct fb_info *info;
	struct ast_framebuffer *afb = &afbdev->afb;
	if (afbdev->helper.fbdev) {
		info = afbdev->helper.fbdev;
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (afb->obj) {
		drm_gem_object_unreference_unlocked(afb->obj);
		afb->obj = NULL;
	}
	drm_fb_helper_fini(&afbdev->helper);

	vfree(afbdev->sysram);
	drm_framebuffer_unregister_private(&afb->base);
	drm_framebuffer_cleanup(&afb->base);
}

int ast_fbdev_init(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	struct ast_fbdev *afbdev;
	int ret;

	afbdev = kzalloc(sizeof(struct ast_fbdev), GFP_KERNEL);
	if (!afbdev)
		return -ENOMEM;

	ast->fbdev = afbdev;
	afbdev->helper.funcs = &ast_fb_helper_funcs;
	spin_lock_init(&afbdev->dirty_lock);
	ret = drm_fb_helper_init(dev, &afbdev->helper,
				 1, 1);
	if (ret) {
		kfree(afbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&afbdev->helper);

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	drm_fb_helper_initial_config(&afbdev->helper, 32);
	return 0;
}

void ast_fbdev_fini(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;

	if (!ast->fbdev)
		return;

	ast_fbdev_destroy(dev, ast->fbdev);
	kfree(ast->fbdev);
	ast->fbdev = NULL;
}

void ast_fbdev_set_suspend(struct drm_device *dev, int state)
{
	struct ast_private *ast = dev->dev_private;

	if (!ast->fbdev)
		return;

	fb_set_suspend(ast->fbdev->helper.fbdev, state);
}
