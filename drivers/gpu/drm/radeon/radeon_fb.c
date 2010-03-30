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
    /*
     *  Modularization
     */

#include <linux/module.h>
#include <linux/fb.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "radeon_drm.h"
#include "radeon.h"

#include "drm_fb_helper.h"

#include <linux/vga_switcheroo.h>

/* object hierarchy -
   this contains a helper + a radeon fb
   the helper contains a pointer to radeon framebuffer baseclass.
*/
struct radeon_kernel_fbdev {
	struct drm_fb_helper helper;
	struct radeon_framebuffer rfb;
	struct list_head fbdev_list;
	struct radeon_device *rdev;
};

static struct fb_ops radeonfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_setcolreg = drm_fb_helper_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
};


static int radeon_align_pitch(struct radeon_device *rdev, int width, int bpp, bool tiled)
{
	int aligned = width;
	int align_large = (ASIC_IS_AVIVO(rdev)) || tiled;
	int pitch_mask = 0;

	switch (bpp / 8) {
	case 1:
		pitch_mask = align_large ? 255 : 127;
		break;
	case 2:
		pitch_mask = align_large ? 127 : 31;
		break;
	case 3:
	case 4:
		pitch_mask = align_large ? 63 : 15;
		break;
	}

	aligned += pitch_mask;
	aligned &= ~pitch_mask;
	return aligned;
}

static struct drm_fb_helper_funcs radeon_fb_helper_funcs = {
	.gamma_set = radeon_crtc_fb_gamma_set,
	.gamma_get = radeon_crtc_fb_gamma_get,
};

static int radeonfb_create(struct drm_device *dev,
			   struct drm_fb_helper_surface_size *sizes,
			   struct radeon_kernel_fbdev **rfbdev_p)
{
	struct radeon_device *rdev = dev->dev_private;
	struct fb_info *info;
	struct radeon_kernel_fbdev *rfbdev;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct radeon_bo *rbo = NULL;
	struct device *device = &rdev->pdev->dev;
	int size, aligned_size, ret;
	u64 fb_gpuaddr;
	void *fbptr = NULL;
	unsigned long tmp;
	bool fb_tiled = false; /* useful for testing */
	u32 tiling_flags = 0;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	/* avivo can't scanout real 24bpp */
	if ((sizes->surface_bpp == 24) && ASIC_IS_AVIVO(rdev))
		sizes->surface_bpp = 32;

	mode_cmd.bpp = sizes->surface_bpp;
	/* need to align pitch with crtc limits */
	mode_cmd.pitch = radeon_align_pitch(rdev, mode_cmd.width, mode_cmd.bpp, fb_tiled) * ((mode_cmd.bpp + 1) / 8);
	mode_cmd.depth = sizes->surface_depth;

	size = mode_cmd.pitch * mode_cmd.height;
	aligned_size = ALIGN(size, PAGE_SIZE);

	ret = radeon_gem_object_create(rdev, aligned_size, 0,
			RADEON_GEM_DOMAIN_VRAM,
			false, ttm_bo_type_kernel,
			&gobj);
	if (ret) {
		printk(KERN_ERR "failed to allocate framebuffer (%d %d)\n",
		       sizes->surface_width, sizes->surface_height);
		ret = -ENOMEM;
		goto out;
	}
	rbo = gobj->driver_private;

	if (fb_tiled)
		tiling_flags = RADEON_TILING_MACRO;

#ifdef __BIG_ENDIAN
	switch (mode_cmd.bpp) {
	case 32:
		tiling_flags |= RADEON_TILING_SWAP_32BIT;
		break;
	case 16:
		tiling_flags |= RADEON_TILING_SWAP_16BIT;
	default:
		break;
	}
#endif

	if (tiling_flags) {
		ret = radeon_bo_set_tiling_flags(rbo,
					tiling_flags | RADEON_TILING_SURFACE,
					mode_cmd.pitch);
		if (ret)
			dev_err(rdev->dev, "FB failed to set tiling flags\n");
	}
	mutex_lock(&rdev->ddev->struct_mutex);

	ret = radeon_bo_reserve(rbo, false);
	if (unlikely(ret != 0))
		goto out_unref;
	ret = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_VRAM, &fb_gpuaddr);
	if (ret) {
		radeon_bo_unreserve(rbo);
		goto out_unref;
	}
	if (fb_tiled)
		radeon_bo_check_tiling(rbo, 0, 0);
	ret = radeon_bo_kmap(rbo, &fbptr);
	radeon_bo_unreserve(rbo);
	if (ret) {
		goto out_unref;
	}

	info = framebuffer_alloc(sizeof(struct radeon_kernel_fbdev), device);
	if (info == NULL) {
		ret = -ENOMEM;
		goto out_unref;
	}

	rfbdev = info->par;
	rfbdev->rdev = rdev;
	radeon_framebuffer_init(dev, &rfbdev->rfb, &mode_cmd, gobj);
	fb = &rfbdev->rfb.base;

	/* setup helper */
	rfbdev->helper.fb = fb;
	rfbdev->helper.fbdev = info;
	rfbdev->helper.funcs = &radeon_fb_helper_funcs;
	rfbdev->helper.dev = dev;

	*rfbdev_p = rfbdev;

	ret = drm_fb_helper_init_crtc_count(&rfbdev->helper, rdev->num_crtc,
					    RADEONFB_CONN_LIMIT);
	if (ret)
		goto out_unref;

	memset_io(fbptr, 0x0, aligned_size);

	strcpy(info->fix.id, "radeondrmfb");

	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);

	info->flags = FBINFO_DEFAULT;
	info->fbops = &radeonfb_ops;

	tmp = fb_gpuaddr - rdev->mc.vram_start;
	info->fix.smem_start = rdev->mc.aper_base + tmp;
	info->fix.smem_len = size;
	info->screen_base = fbptr;
	info->screen_size = size;

	drm_fb_helper_fill_var(info, &rfbdev->helper, sizes->fb_width, sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->aperture_base = rdev->ddev->mode_config.fb_base;
	info->aperture_size = rdev->mc.real_vram_size;

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;
	if (info->screen_base == NULL) {
		ret = -ENOSPC;
		goto out_unref;
	}
	DRM_INFO("fb mappable at 0x%lX\n",  info->fix.smem_start);
	DRM_INFO("vram apper at 0x%lX\n",  (unsigned long)rdev->mc.aper_base);
	DRM_INFO("size %lu\n", (unsigned long)size);
	DRM_INFO("fb depth is %d\n", fb->depth);
	DRM_INFO("   pitch is %d\n", fb->pitch);


	mutex_unlock(&rdev->ddev->struct_mutex);
	vga_switcheroo_client_fb_set(rdev->ddev->pdev, info);
	return 0;

out_unref:
	if (rbo) {
		ret = radeon_bo_reserve(rbo, false);
		if (likely(ret == 0)) {
			radeon_bo_kunmap(rbo);
			radeon_bo_unreserve(rbo);
		}
	}
	if (fb && ret) {
		drm_gem_object_unreference(gobj);
		drm_framebuffer_cleanup(fb);
		kfree(fb);
	}
	drm_gem_object_unreference(gobj);
	mutex_unlock(&rdev->ddev->struct_mutex);
out:
	return ret;
}

static int radeon_fb_find_or_create_single(struct drm_device *dev,
					   struct drm_fb_helper_surface_size *sizes,
					   struct drm_fb_helper **fb_ptr)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_kernel_fbdev *rfbdev = NULL;
	int new_fb = 0;
	int ret;

	if (!rdev->mode_info.rfbdev) {
		ret = radeonfb_create(dev, sizes,
				      &rfbdev);
		if (ret)
			return ret;
		rdev->mode_info.rfbdev = rfbdev;
		new_fb = 1;
	} else {
		rfbdev = rdev->mode_info.rfbdev;
		if (rfbdev->rfb.base.width < sizes->surface_width ||
		    rfbdev->rfb.base.height < sizes->surface_height) {
			DRM_ERROR("Framebuffer not large enough to scale console onto.\n");
			return -EINVAL;
		}
	}

	*fb_ptr = &rfbdev->helper;
	return new_fb;
}

static char *mode_option;
int radeon_parse_options(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		mode_option = this_opt;
	}
	return 0;
}

static int radeonfb_probe(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int bpp_sel = 32;

	/* select 8 bpp console on RN50 or 16MB cards */
	if (ASIC_IS_RN50(rdev) || rdev->mc.real_vram_size <= (32*1024*1024))
		bpp_sel = 8;

	return drm_fb_helper_single_fb_probe(dev, bpp_sel, &radeon_fb_find_or_create_single);
}

void radeonfb_hotplug(struct drm_device *dev)
{
	drm_helper_fb_hotplug_event(dev);

	radeonfb_probe(dev);
}

static int radeon_fbdev_destroy(struct drm_device *dev, struct radeon_kernel_fbdev *rfbdev)
{
	struct fb_info *info;
	struct radeon_framebuffer *rfb = &rfbdev->rfb;
	struct radeon_bo *rbo;
	int r;

	rbo = rfb->obj->driver_private;
	info = rfbdev->helper.fbdev;
	unregister_framebuffer(info);
	r = radeon_bo_reserve(rbo, false);
	if (likely(r == 0)) {
		radeon_bo_kunmap(rbo);
		radeon_bo_unpin(rbo);
		radeon_bo_unreserve(rbo);
	}

	drm_fb_helper_free(&rfbdev->helper);
	drm_framebuffer_cleanup(&rfb->base);
	if (rfb->obj)
		drm_gem_object_unreference_unlocked(rfb->obj);

	framebuffer_release(info);

	return 0;
}
MODULE_LICENSE("GPL");

int radeon_fbdev_init(struct radeon_device *rdev)
{
	drm_helper_initial_config(rdev->ddev);
	radeonfb_probe(rdev->ddev);
	return 0;
}

void radeon_fbdev_fini(struct radeon_device *rdev)
{
	radeon_fbdev_destroy(rdev->ddev, rdev->mode_info.rfbdev);
	rdev->mode_info.rfbdev = NULL;
}

void radeon_fbdev_set_suspend(struct radeon_device *rdev, int state)
{
	fb_set_suspend(rdev->mode_info.rfbdev->helper.fbdev, state);
}

int radeon_fbdev_total_size(struct radeon_device *rdev)
{
	struct radeon_bo *robj;
	int size = 0;

	robj = rdev->mode_info.rfbdev->rfb.obj->driver_private;
	size += radeon_bo_size(robj);
	return size;
}

bool radeon_fbdev_robj_is_fb(struct radeon_device *rdev, struct radeon_bo *robj)
{
	if (robj == rdev->mode_info.rfbdev->rfb.obj->driver_private)
		return true;
	return false;
}

