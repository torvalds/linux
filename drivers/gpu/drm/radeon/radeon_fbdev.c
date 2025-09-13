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

#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "radeon.h"

static void radeon_fbdev_destroy_pinned_object(struct drm_gem_object *gobj)
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

static int radeon_fbdev_create_pinned_object(struct drm_fb_helper *fb_helper,
					     const struct drm_format_info *info,
					     struct drm_mode_fb_cmd2 *mode_cmd,
					     struct drm_gem_object **gobj_p)
{
	struct radeon_device *rdev = fb_helper->dev->dev_private;
	struct drm_gem_object *gobj = NULL;
	struct radeon_bo *rbo = NULL;
	bool fb_tiled = false; /* useful for testing */
	u32 tiling_flags = 0;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;
	u32 cpp;

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
		goto err_radeon_fbdev_destroy_pinned_object;
	/* Only 27 bit offset for legacy CRTC */
	ret = radeon_bo_pin_restricted(rbo, RADEON_GEM_DOMAIN_VRAM,
				       ASIC_IS_AVIVO(rdev) ? 0 : 1 << 27,
				       NULL);
	if (ret) {
		radeon_bo_unreserve(rbo);
		goto err_radeon_fbdev_destroy_pinned_object;
	}
	if (fb_tiled)
		radeon_bo_check_tiling(rbo, 0, 0);
	ret = radeon_bo_kmap(rbo, NULL);
	radeon_bo_unreserve(rbo);
	if (ret)
		goto err_radeon_fbdev_destroy_pinned_object;

	*gobj_p = gobj;
	return 0;

err_radeon_fbdev_destroy_pinned_object:
	radeon_fbdev_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

/*
 * Fbdev ops and struct fb_ops
 */

static int radeon_fbdev_fb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct radeon_device *rdev = fb_helper->dev->dev_private;
	int ret;

	ret = pm_runtime_get_sync(rdev_to_drm(rdev)->dev);
	if (ret < 0 && ret != -EACCES)
		goto err_pm_runtime_mark_last_busy;

	return 0;

err_pm_runtime_mark_last_busy:
	pm_runtime_mark_last_busy(rdev_to_drm(rdev)->dev);
	pm_runtime_put_autosuspend(rdev_to_drm(rdev)->dev);
	return ret;
}

static int radeon_fbdev_fb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct radeon_device *rdev = fb_helper->dev->dev_private;

	pm_runtime_mark_last_busy(rdev_to_drm(rdev)->dev);
	pm_runtime_put_autosuspend(rdev_to_drm(rdev)->dev);

	return 0;
}

static void radeon_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_gem_object *gobj = drm_gem_fb_get_obj(fb, 0);

	drm_fb_helper_fini(fb_helper);

	drm_framebuffer_unregister_private(fb);
	drm_framebuffer_cleanup(fb);
	kfree(fb);
	radeon_fbdev_destroy_pinned_object(gobj);

	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

static const struct fb_ops radeon_fbdev_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = radeon_fbdev_fb_open,
	.fb_release = radeon_fbdev_fb_release,
	FB_DEFAULT_IOMEM_OPS,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_destroy = radeon_fbdev_fb_destroy,
};

static const struct drm_fb_helper_funcs radeon_fbdev_fb_helper_funcs = {
};

int radeon_fbdev_driver_fbdev_probe(struct drm_fb_helper *fb_helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct radeon_device *rdev = fb_helper->dev->dev_private;
	const struct drm_format_info *format_info;
	struct drm_mode_fb_cmd2 mode_cmd = { };
	struct fb_info *info;
	struct drm_gem_object *gobj;
	struct radeon_bo *rbo;
	struct drm_framebuffer *fb;
	int ret;
	unsigned long tmp;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	/* avivo can't scanout real 24bpp */
	if ((sizes->surface_bpp == 24) && ASIC_IS_AVIVO(rdev))
		sizes->surface_bpp = 32;

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	format_info = drm_get_format_info(rdev_to_drm(rdev), mode_cmd.pixel_format,
					  mode_cmd.modifier[0]);
	ret = radeon_fbdev_create_pinned_object(fb_helper, format_info, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon object %d\n", ret);
		return ret;
	}
	rbo = gem_to_radeon_bo(gobj);

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb) {
		ret = -ENOMEM;
		goto err_radeon_fbdev_destroy_pinned_object;
	}
	ret = radeon_framebuffer_init(rdev_to_drm(rdev), fb, format_info, &mode_cmd, gobj);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer %d\n", ret);
		goto err_kfree;
	}

	/* setup helper */
	fb_helper->funcs = &radeon_fbdev_fb_helper_funcs;
	fb_helper->fb = fb;

	/* okay we have an object now allocate the framebuffer */
	info = drm_fb_helper_alloc_info(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_framebuffer_unregister_private;
	}

	info->fbops = &radeon_fbdev_fb_ops;

	/* radeon resume is fragile and needs a vt switch to help it along */
	info->skip_vt_switch = false;

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	tmp = radeon_bo_gpu_offset(rbo) - rdev->mc.vram_start;
	info->fix.smem_start = rdev->mc.aper_base + tmp;
	info->fix.smem_len = radeon_bo_size(rbo);
	info->screen_base = (__force void __iomem *)rbo->kptr;
	info->screen_size = radeon_bo_size(rbo);

	memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	DRM_INFO("fb mappable at 0x%lX\n",  info->fix.smem_start);
	DRM_INFO("vram apper at 0x%lX\n",  (unsigned long)rdev->mc.aper_base);
	DRM_INFO("size %lu\n", (unsigned long)radeon_bo_size(rbo));
	DRM_INFO("fb depth is %d\n", fb->format->depth);
	DRM_INFO("   pitch is %d\n", fb->pitches[0]);

	return 0;

err_drm_framebuffer_unregister_private:
	fb_helper->fb = NULL;
	drm_framebuffer_unregister_private(fb);
	drm_framebuffer_cleanup(fb);
err_kfree:
	kfree(fb);
err_radeon_fbdev_destroy_pinned_object:
	radeon_fbdev_destroy_pinned_object(gobj);
	return ret;
}

bool radeon_fbdev_robj_is_fb(struct radeon_device *rdev, struct radeon_bo *robj)
{
	struct drm_fb_helper *fb_helper = rdev_to_drm(rdev)->fb_helper;
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
