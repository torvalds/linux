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
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/radeon_drm.h>

#include "radeon.h"

static int
radeonfb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct radeon_device *rdev = fb_helper->dev->dev_private;
	int ret = pm_runtime_get_sync(rdev->ddev->dev);

	if (ret < 0 && ret != -EACCES) {
		pm_runtime_mark_last_busy(rdev->ddev->dev);
		pm_runtime_put_autosuspend(rdev->ddev->dev);
		return ret;
	}
	return 0;
}

static int
radeonfb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct radeon_device *rdev = fb_helper->dev->dev_private;

	pm_runtime_mark_last_busy(rdev->ddev->dev);
	pm_runtime_put_autosuspend(rdev->ddev->dev);
	return 0;
}

static const struct fb_ops radeonfb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_open = radeonfb_open,
	.fb_release = radeonfb_release,
	.fb_read = drm_fb_helper_cfb_read,
	.fb_write = drm_fb_helper_cfb_write,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
};

static void radeonfb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct radeon_bo *rbo = gem_to_radeon_bo(gobj);
	int ret;

	ret = radeon_bo_reserve(rbo, false);
	if (likely(ret == 0)) {
		radeon_bo_kunmap(rbo);
		radeon_bo_unpin(rbo);
		radeon_bo_unreserve(rbo);
	}
	drm_gem_object_put(gobj);
}

static int radeonfb_create_pinned_object(struct drm_fb_helper *fb_helper,
					 struct drm_mode_fb_cmd2 *mode_cmd,
					 struct drm_gem_object **gobj_p)
{
	const struct drm_format_info *info;
	struct radeon_device *rdev = fb_helper->dev->dev_private;
	struct drm_gem_object *gobj = NULL;
	struct radeon_bo *rbo = NULL;
	bool fb_tiled = false; /* useful for testing */
	u32 tiling_flags = 0;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;
	u32 cpp;

	info = drm_get_format_info(rdev->ddev, mode_cmd);
	cpp = info->cpp[0];

	/* need to align pitch with crtc limits */
	mode_cmd->pitches[0] = radeon_align_pitch(rdev, mode_cmd->width, cpp,
						  fb_tiled);

	if (rdev->family >= CHIP_R600)
		height = ALIGN(mode_cmd->height, 8);
	size = mode_cmd->pitches[0] * height;
	aligned_size = ALIGN(size, PAGE_SIZE);
	ret = radeon_gem_object_create(rdev, aligned_size, 0,
				       RADEON_GEM_DOMAIN_VRAM,
				       0, true, &gobj);
	if (ret) {
		pr_err("failed to allocate framebuffer (%d)\n", aligned_size);
		return -ENOMEM;
	}
	rbo = gem_to_radeon_bo(gobj);

	if (fb_tiled)
		tiling_flags = RADEON_TILING_MACRO;

#ifdef __BIG_ENDIAN
	switch (cpp) {
	case 4:
		tiling_flags |= RADEON_TILING_SWAP_32BIT;
		break;
	case 2:
		tiling_flags |= RADEON_TILING_SWAP_16BIT;
		break;
	default:
		break;
	}
#endif

	if (tiling_flags) {
		ret = radeon_bo_set_tiling_flags(rbo,
						 tiling_flags | RADEON_TILING_SURFACE,
						 mode_cmd->pitches[0]);
		if (ret)
			dev_err(rdev->dev, "FB failed to set tiling flags\n");
	}


	ret = radeon_bo_reserve(rbo, false);
	if (unlikely(ret != 0))
		goto out_unref;
	/* Only 27 bit offset for legacy CRTC */
	ret = radeon_bo_pin_restricted(rbo, RADEON_GEM_DOMAIN_VRAM,
				       ASIC_IS_AVIVO(rdev) ? 0 : 1 << 27,
				       NULL);
	if (ret) {
		radeon_bo_unreserve(rbo);
		goto out_unref;
	}
	if (fb_tiled)
		radeon_bo_check_tiling(rbo, 0, 0);
	ret = radeon_bo_kmap(rbo, NULL);
	radeon_bo_unreserve(rbo);
	if (ret)
		goto out_unref;

	*gobj_p = gobj;
	return 0;
out_unref:
	radeonfb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

static int radeonfb_create(struct drm_fb_helper *helper,
			   struct drm_fb_helper_surface_size *sizes)
{
	struct radeon_device *rdev = helper->dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct radeon_bo *rbo = NULL;
	int ret;
	unsigned long tmp;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	/* avivo can't scanout real 24bpp */
	if ((sizes->surface_bpp == 24) && ASIC_IS_AVIVO(rdev))
		sizes->surface_bpp = 32;

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	ret = radeonfb_create_pinned_object(helper, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon object %d\n", ret);
		return ret;
	}

	rbo = gem_to_radeon_bo(gobj);

	/* okay we have an object now allocate the framebuffer */
	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}

	/* radeon resume is fragile and needs a vt switch to help it along */
	info->skip_vt_switch = false;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = radeon_framebuffer_init(rdev->ddev, fb, &mode_cmd, gobj);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer %d\n", ret);
		goto out;
	}

	/* setup helper */
	helper->fb = fb;

	memset_io(rbo->kptr, 0x0, radeon_bo_size(rbo));

	info->fbops = &radeonfb_ops;

	tmp = radeon_bo_gpu_offset(rbo) - rdev->mc.vram_start;
	info->fix.smem_start = rdev->mc.aper_base + tmp;
	info->fix.smem_len = radeon_bo_size(rbo);
	info->screen_base = rbo->kptr;
	info->screen_size = radeon_bo_size(rbo);

	drm_fb_helper_fill_info(info, helper, sizes);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	DRM_INFO("fb mappable at 0x%lX\n",  info->fix.smem_start);
	DRM_INFO("vram apper at 0x%lX\n",  (unsigned long)rdev->mc.aper_base);
	DRM_INFO("size %lu\n", (unsigned long)radeon_bo_size(rbo));
	DRM_INFO("fb depth is %d\n", fb->format->depth);
	DRM_INFO("   pitch is %d\n", fb->pitches[0]);

	vga_switcheroo_client_fb_set(rdev->pdev, info);
	return 0;

out:
	if (fb && ret) {
		drm_gem_object_put(gobj);
		drm_framebuffer_unregister_private(fb);
		drm_framebuffer_cleanup(fb);
		kfree(fb);
	}
	return ret;
}

static int radeon_fbdev_destroy(struct drm_device *dev, struct drm_fb_helper *fb_helper)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_unregister_info(fb_helper);

	if (fb) {
		if (fb->obj[0]) {
			radeonfb_destroy_pinned_object(fb->obj[0]);
			fb->obj[0] = NULL;
			drm_framebuffer_unregister_private(fb);
			drm_framebuffer_cleanup(fb);
		}
		kfree(fb);
		fb_helper->fb = NULL;
	}
	drm_fb_helper_fini(fb_helper);

	return 0;
}

static const struct drm_fb_helper_funcs radeon_fb_helper_funcs = {
	.fb_probe = radeonfb_create,
};

int radeon_fbdev_init(struct radeon_device *rdev)
{
	struct drm_fb_helper *fb_helper;
	int bpp_sel = 32;
	int ret;

	/* don't enable fbdev if no connectors */
	if (list_empty(&rdev->ddev->mode_config.connector_list))
		return 0;

	/* select 8 bpp console on 8MB cards, or 16 bpp on RN50 or 32MB */
	if (rdev->mc.real_vram_size <= (8*1024*1024))
		bpp_sel = 8;
	else if (ASIC_IS_RN50(rdev) ||
		 rdev->mc.real_vram_size <= (32*1024*1024))
		bpp_sel = 16;

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper)
		return -ENOMEM;

	drm_fb_helper_prepare(rdev->ddev, fb_helper, bpp_sel, &radeon_fb_helper_funcs);

	ret = drm_fb_helper_init(rdev->ddev, fb_helper);
	if (ret)
		goto free;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(rdev->ddev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(fb_helper);
free:
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
	return ret;
}

void radeon_fbdev_fini(struct radeon_device *rdev)
{
	if (!rdev->ddev->fb_helper)
		return;

	radeon_fbdev_destroy(rdev->ddev, rdev->ddev->fb_helper);
	drm_fb_helper_unprepare(rdev->ddev->fb_helper);
	kfree(rdev->ddev->fb_helper);
	rdev->ddev->fb_helper = NULL;
}

void radeon_fbdev_set_suspend(struct radeon_device *rdev, int state)
{
	if (rdev->ddev->fb_helper)
		drm_fb_helper_set_suspend(rdev->ddev->fb_helper, state);
}

bool radeon_fbdev_robj_is_fb(struct radeon_device *rdev, struct radeon_bo *robj)
{
	struct drm_fb_helper *fb_helper = rdev->ddev->fb_helper;
	struct drm_gem_object *gobj;

	if (!fb_helper)
		return false;

	gobj = drm_gem_fb_get_obj(fb_helper->fb, 0);
	if (!gobj)
		return false;
	if (gobj != &robj->tbo.base)
		return false;

	return true;
}
