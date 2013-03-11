/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Matt Turner
 *          Dave Airlie
 */
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

#include <linux/fb.h>

#include "mgag200_drv.h"

static void mga_dirty_update(struct mga_fbdev *mfbdev,
			     int x, int y, int width, int height)
{
	int i;
	struct drm_gem_object *obj;
	struct mgag200_bo *bo;
	int src_offset, dst_offset;
	int bpp = (mfbdev->mfb.base.bits_per_pixel + 7)/8;
	int ret;
	bool unmap = false;

	obj = mfbdev->mfb.obj;
	bo = gem_to_mga_bo(obj);

	ret = mgag200_bo_reserve(bo, true);
	if (ret) {
		DRM_ERROR("failed to reserve fb bo\n");
		return;
	}

	if (!bo->kmap.virtual) {
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret) {
			DRM_ERROR("failed to kmap fb updates\n");
			mgag200_bo_unreserve(bo);
			return;
		}
		unmap = true;
	}
	for (i = y; i < y + height; i++) {
		/* assume equal stride for now */
		src_offset = dst_offset = i * mfbdev->mfb.base.pitches[0] + (x * bpp);
		memcpy_toio(bo->kmap.virtual + src_offset, mfbdev->sysram + src_offset, width * bpp);

	}
	if (unmap)
		ttm_bo_kunmap(&bo->kmap);

	mgag200_bo_unreserve(bo);
}

static void mga_fillrect(struct fb_info *info,
			 const struct fb_fillrect *rect)
{
	struct mga_fbdev *mfbdev = info->par;
	sys_fillrect(info, rect);
	mga_dirty_update(mfbdev, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void mga_copyarea(struct fb_info *info,
			 const struct fb_copyarea *area)
{
	struct mga_fbdev *mfbdev = info->par;
	sys_copyarea(info, area);
	mga_dirty_update(mfbdev, area->dx, area->dy, area->width,
			 area->height);
}

static void mga_imageblit(struct fb_info *info,
			  const struct fb_image *image)
{
	struct mga_fbdev *mfbdev = info->par;
	sys_imageblit(info, image);
	mga_dirty_update(mfbdev, image->dx, image->dy, image->width,
			 image->height);
}


static struct fb_ops mgag200fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = mga_fillrect,
	.fb_copyarea = mga_copyarea,
	.fb_imageblit = mga_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int mgag200fb_create_object(struct mga_fbdev *afbdev,
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
	ret = mgag200_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static int mgag200fb_create(struct drm_fb_helper *helper,
			   struct drm_fb_helper_surface_size *sizes)
{
	struct mga_fbdev *mfbdev = (struct mga_fbdev *)helper;
	struct drm_device *dev = mfbdev->helper.dev;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct mga_device *mdev = dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_gem_object *gobj = NULL;
	struct device *device = &dev->pdev->dev;
	struct mgag200_bo *bo;
	int ret;
	void *sysram;
	int size;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	size = mode_cmd.pitches[0] * mode_cmd.height;

	ret = mgag200fb_create_object(mfbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}
	bo = gem_to_mga_bo(gobj);

	sysram = vmalloc(size);
	if (!sysram)
		return -ENOMEM;

	info = framebuffer_alloc(0, device);
	if (info == NULL)
		return -ENOMEM;

	info->par = mfbdev;

	ret = mgag200_framebuffer_init(dev, &mfbdev->mfb, &mode_cmd, gobj);
	if (ret)
		return ret;

	mfbdev->sysram = sysram;
	mfbdev->size = size;

	fb = &mfbdev->mfb.base;

	/* setup helper */
	mfbdev->helper.fb = fb;
	mfbdev->helper.fbdev = info;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("%s: can't allocate color map\n", info->fix.id);
		ret = -ENOMEM;
		goto out;
	}

	strcpy(info->fix.id, "mgadrmfb");

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &mgag200fb_ops;

	/* setup aperture base/size for vesafb takeover */
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out;
	}
	info->apertures->ranges[0].base = mdev->dev->mode_config.fb_base;
	info->apertures->ranges[0].size = mdev->mc.vram_size;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &mfbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = sysram;
	info->screen_size = size;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n",
		      fb->width, fb->height);
	return 0;
out:
	return ret;
}

static int mga_fbdev_destroy(struct drm_device *dev,
				struct mga_fbdev *mfbdev)
{
	struct fb_info *info;
	struct mga_framebuffer *mfb = &mfbdev->mfb;

	if (mfbdev->helper.fbdev) {
		info = mfbdev->helper.fbdev;

		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (mfb->obj) {
		drm_gem_object_unreference_unlocked(mfb->obj);
		mfb->obj = NULL;
	}
	drm_fb_helper_fini(&mfbdev->helper);
	vfree(mfbdev->sysram);
	drm_framebuffer_unregister_private(&mfb->base);
	drm_framebuffer_cleanup(&mfb->base);

	return 0;
}

static struct drm_fb_helper_funcs mga_fb_helper_funcs = {
	.gamma_set = mga_crtc_fb_gamma_set,
	.gamma_get = mga_crtc_fb_gamma_get,
	.fb_probe = mgag200fb_create,
};

int mgag200_fbdev_init(struct mga_device *mdev)
{
	struct mga_fbdev *mfbdev;
	int ret;

	mfbdev = kzalloc(sizeof(struct mga_fbdev), GFP_KERNEL);
	if (!mfbdev)
		return -ENOMEM;

	mdev->mfbdev = mfbdev;
	mfbdev->helper.funcs = &mga_fb_helper_funcs;

	ret = drm_fb_helper_init(mdev->dev, &mfbdev->helper,
				 mdev->num_crtc, MGAG200FB_CONN_LIMIT);
	if (ret) {
		kfree(mfbdev);
		return ret;
	}
	drm_fb_helper_single_add_all_connectors(&mfbdev->helper);

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(mdev->dev);

	drm_fb_helper_initial_config(&mfbdev->helper, 32);

	return 0;
}

void mgag200_fbdev_fini(struct mga_device *mdev)
{
	if (!mdev->mfbdev)
		return;

	mga_fbdev_destroy(mdev->dev, mdev->mfbdev);
	kfree(mdev->mfbdev);
	mdev->mfbdev = NULL;
}
