/*
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#include <linux/fb.h>

#include "cirrus_drv.h"

static void cirrus_dirty_update(struct cirrus_fbdev *afbdev,
			     int x, int y, int width, int height)
{
	int i;
	struct drm_gem_object *obj;
	struct cirrus_bo *bo;
	int src_offset, dst_offset;
	int bpp = (afbdev->gfb.base.bits_per_pixel + 7)/8;
	int ret = -EBUSY;
	bool unmap = false;
	bool store_for_later = false;
	int x2, y2;
	unsigned long flags;

	obj = afbdev->gfb.obj;
	bo = gem_to_cirrus_bo(obj);

	/*
	 * try and reserve the BO, if we fail with busy
	 * then the BO is being moved and we should
	 * store up the damage until later.
	 */
	if (drm_can_sleep())
		ret = cirrus_bo_reserve(bo, true);
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
			cirrus_bo_unreserve(bo);
			return;
		}
		unmap = true;
	}
	for (i = y; i < y + height; i++) {
		/* assume equal stride for now */
		src_offset = dst_offset = i * afbdev->gfb.base.pitches[0] + (x * bpp);
		memcpy_toio(bo->kmap.virtual + src_offset, afbdev->sysram + src_offset, width * bpp);

	}
	if (unmap)
		ttm_bo_kunmap(&bo->kmap);

	cirrus_bo_unreserve(bo);
}

static void cirrus_fillrect(struct fb_info *info,
			 const struct fb_fillrect *rect)
{
	struct cirrus_fbdev *afbdev = info->par;
	sys_fillrect(info, rect);
	cirrus_dirty_update(afbdev, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void cirrus_copyarea(struct fb_info *info,
			 const struct fb_copyarea *area)
{
	struct cirrus_fbdev *afbdev = info->par;
	sys_copyarea(info, area);
	cirrus_dirty_update(afbdev, area->dx, area->dy, area->width,
			 area->height);
}

static void cirrus_imageblit(struct fb_info *info,
			  const struct fb_image *image)
{
	struct cirrus_fbdev *afbdev = info->par;
	sys_imageblit(info, image);
	cirrus_dirty_update(afbdev, image->dx, image->dy, image->width,
			 image->height);
}


static struct fb_ops cirrusfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = cirrus_fillrect,
	.fb_copyarea = cirrus_copyarea,
	.fb_imageblit = cirrus_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int cirrusfb_create_object(struct cirrus_fbdev *afbdev,
			       struct drm_mode_fb_cmd2 *mode_cmd,
			       struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = afbdev->helper.dev;
	u32 bpp, depth;
	u32 size;
	struct drm_gem_object *gobj;

	int ret = 0;
	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &depth, &bpp);

	if (bpp > 24)
		return -EINVAL;
	size = mode_cmd->pitches[0] * mode_cmd->height;
	ret = cirrus_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static int cirrusfb_create(struct drm_fb_helper *helper,
			   struct drm_fb_helper_surface_size *sizes)
{
	struct cirrus_fbdev *gfbdev = (struct cirrus_fbdev *)helper;
	struct drm_device *dev = gfbdev->helper.dev;
	struct cirrus_device *cdev = gfbdev->helper.dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct device *device = &dev->pdev->dev;
	void *sysram;
	struct drm_gem_object *gobj = NULL;
	struct cirrus_bo *bo = NULL;
	int size, ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	size = mode_cmd.pitches[0] * mode_cmd.height;

	ret = cirrusfb_create_object(gfbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	bo = gem_to_cirrus_bo(gobj);

	sysram = vmalloc(size);
	if (!sysram)
		return -ENOMEM;

	info = framebuffer_alloc(0, device);
	if (info == NULL)
		return -ENOMEM;

	info->par = gfbdev;

	ret = cirrus_framebuffer_init(cdev->dev, &gfbdev->gfb, &mode_cmd, gobj);
	if (ret)
		return ret;

	gfbdev->sysram = sysram;
	gfbdev->size = size;

	fb = &gfbdev->gfb.base;
	if (!fb) {
		DRM_INFO("fb is NULL\n");
		return -EINVAL;
	}

	/* setup helper */
	gfbdev->helper.fb = fb;
	gfbdev->helper.fbdev = info;

	strcpy(info->fix.id, "cirrusdrmfb");


	info->flags = FBINFO_DEFAULT;
	info->fbops = &cirrusfb_ops;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &gfbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out_iounmap;
	}
	info->apertures->ranges[0].base = cdev->dev->mode_config.fb_base;
	info->apertures->ranges[0].size = cdev->mc.vram_size;

	info->fix.smem_start = cdev->dev->mode_config.fb_base;
	info->fix.smem_len = cdev->mc.vram_size;

	info->screen_base = sysram;
	info->screen_size = size;

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("%s: can't allocate color map\n", info->fix.id);
		ret = -ENOMEM;
		goto out_iounmap;
	}

	DRM_INFO("fb mappable at 0x%lX\n", info->fix.smem_start);
	DRM_INFO("vram aper at 0x%lX\n", (unsigned long)info->fix.smem_start);
	DRM_INFO("size %lu\n", (unsigned long)info->fix.smem_len);
	DRM_INFO("fb depth is %d\n", fb->depth);
	DRM_INFO("   pitch is %d\n", fb->pitches[0]);

	return 0;
out_iounmap:
	return ret;
}

static int cirrus_fbdev_destroy(struct drm_device *dev,
				struct cirrus_fbdev *gfbdev)
{
	struct fb_info *info;
	struct cirrus_framebuffer *gfb = &gfbdev->gfb;

	if (gfbdev->helper.fbdev) {
		info = gfbdev->helper.fbdev;

		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (gfb->obj) {
		drm_gem_object_unreference_unlocked(gfb->obj);
		gfb->obj = NULL;
	}

	vfree(gfbdev->sysram);
	drm_fb_helper_fini(&gfbdev->helper);
	drm_framebuffer_unregister_private(&gfb->base);
	drm_framebuffer_cleanup(&gfb->base);

	return 0;
}

static struct drm_fb_helper_funcs cirrus_fb_helper_funcs = {
	.gamma_set = cirrus_crtc_fb_gamma_set,
	.gamma_get = cirrus_crtc_fb_gamma_get,
	.fb_probe = cirrusfb_create,
};

int cirrus_fbdev_init(struct cirrus_device *cdev)
{
	struct cirrus_fbdev *gfbdev;
	int ret;
	int bpp_sel = 24;

	/*bpp_sel = 8;*/
	gfbdev = kzalloc(sizeof(struct cirrus_fbdev), GFP_KERNEL);
	if (!gfbdev)
		return -ENOMEM;

	cdev->mode_info.gfbdev = gfbdev;
	gfbdev->helper.funcs = &cirrus_fb_helper_funcs;
	spin_lock_init(&gfbdev->dirty_lock);

	ret = drm_fb_helper_init(cdev->dev, &gfbdev->helper,
				 cdev->num_crtc, CIRRUSFB_CONN_LIMIT);
	if (ret) {
		kfree(gfbdev);
		return ret;
	}
	drm_fb_helper_single_add_all_connectors(&gfbdev->helper);

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(cdev->dev);
	drm_fb_helper_initial_config(&gfbdev->helper, bpp_sel);

	return 0;
}

void cirrus_fbdev_fini(struct cirrus_device *cdev)
{
	if (!cdev->mode_info.gfbdev)
		return;

	cirrus_fbdev_destroy(cdev->dev, cdev->mode_info.gfbdev);
	kfree(cdev->mode_info.gfbdev);
	cdev->mode_info.gfbdev = NULL;
}
