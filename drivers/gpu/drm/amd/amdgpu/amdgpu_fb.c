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
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "cikd.h"

#include <drm/drm_fb_helper.h>

#include <linux/vga_switcheroo.h>

/* object hierarchy -
   this contains a helper + a amdgpu fb
   the helper contains a pointer to amdgpu framebuffer baseclass.
*/
struct amdgpu_fbdev {
	struct drm_fb_helper helper;
	struct amdgpu_framebuffer rfb;
	struct amdgpu_device *adev;
};

static int
amdgpufb_open(struct fb_info *info, int user)
{
	struct amdgpu_fbdev *rfbdev = info->par;
	struct amdgpu_device *adev = rfbdev->adev;
	int ret = pm_runtime_get_sync(adev->ddev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_mark_last_busy(adev->ddev->dev);
		pm_runtime_put_autosuspend(adev->ddev->dev);
		return ret;
	}
	return 0;
}

static int
amdgpufb_release(struct fb_info *info, int user)
{
	struct amdgpu_fbdev *rfbdev = info->par;
	struct amdgpu_device *adev = rfbdev->adev;

	pm_runtime_mark_last_busy(adev->ddev->dev);
	pm_runtime_put_autosuspend(adev->ddev->dev);
	return 0;
}

static struct fb_ops amdgpufb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_open = amdgpufb_open,
	.fb_release = amdgpufb_release,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
};


int amdgpu_align_pitch(struct amdgpu_device *adev, int width, int cpp, bool tiled)
{
	int aligned = width;
	int pitch_mask = 0;

	switch (cpp) {
	case 1:
		pitch_mask = 255;
		break;
	case 2:
		pitch_mask = 127;
		break;
	case 3:
	case 4:
		pitch_mask = 63;
		break;
	}

	aligned += pitch_mask;
	aligned &= ~pitch_mask;
	return aligned * cpp;
}

static void amdgpufb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct amdgpu_bo *abo = gem_to_amdgpu_bo(gobj);
	int ret;

	ret = amdgpu_bo_reserve(abo, false);
	if (likely(ret == 0)) {
		amdgpu_bo_kunmap(abo);
		amdgpu_bo_unpin(abo);
		amdgpu_bo_unreserve(abo);
	}
	drm_gem_object_unreference_unlocked(gobj);
}

static int amdgpufb_create_pinned_object(struct amdgpu_fbdev *rfbdev,
					 struct drm_mode_fb_cmd2 *mode_cmd,
					 struct drm_gem_object **gobj_p)
{
	struct amdgpu_device *adev = rfbdev->adev;
	struct drm_gem_object *gobj = NULL;
	struct amdgpu_bo *abo = NULL;
	bool fb_tiled = false; /* useful for testing */
	u32 tiling_flags = 0;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;
	u32 cpp;

	cpp = drm_format_plane_cpp(mode_cmd->pixel_format, 0);

	/* need to align pitch with crtc limits */
	mode_cmd->pitches[0] = amdgpu_align_pitch(adev, mode_cmd->width, cpp,
						  fb_tiled);

	height = ALIGN(mode_cmd->height, 8);
	size = mode_cmd->pitches[0] * height;
	aligned_size = ALIGN(size, PAGE_SIZE);
	ret = amdgpu_gem_object_create(adev, aligned_size, 0,
				       AMDGPU_GEM_DOMAIN_VRAM,
				       AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED |
				       AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS,
				       true, &gobj);
	if (ret) {
		printk(KERN_ERR "failed to allocate framebuffer (%d)\n",
		       aligned_size);
		return -ENOMEM;
	}
	abo = gem_to_amdgpu_bo(gobj);

	if (fb_tiled)
		tiling_flags = AMDGPU_TILING_SET(ARRAY_MODE, GRPH_ARRAY_2D_TILED_THIN1);

	ret = amdgpu_bo_reserve(abo, false);
	if (unlikely(ret != 0))
		goto out_unref;

	if (tiling_flags) {
		ret = amdgpu_bo_set_tiling_flags(abo,
						 tiling_flags);
		if (ret)
			dev_err(adev->dev, "FB failed to set tiling flags\n");
	}


	ret = amdgpu_bo_pin(abo, AMDGPU_GEM_DOMAIN_VRAM, NULL);
	if (ret) {
		amdgpu_bo_unreserve(abo);
		goto out_unref;
	}
	ret = amdgpu_bo_kmap(abo, NULL);
	amdgpu_bo_unreserve(abo);
	if (ret) {
		goto out_unref;
	}

	*gobj_p = gobj;
	return 0;
out_unref:
	amdgpufb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

static int amdgpufb_create(struct drm_fb_helper *helper,
			   struct drm_fb_helper_surface_size *sizes)
{
	struct amdgpu_fbdev *rfbdev = (struct amdgpu_fbdev *)helper;
	struct amdgpu_device *adev = rfbdev->adev;
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct amdgpu_bo *abo = NULL;
	int ret;
	unsigned long tmp;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	ret = amdgpufb_create_pinned_object(rfbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon object %d\n", ret);
		return ret;
	}

	abo = gem_to_amdgpu_bo(gobj);

	/* okay we have an object now allocate the framebuffer */
	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}

	info->par = rfbdev;
	info->skip_vt_switch = true;

	ret = amdgpu_framebuffer_init(adev->ddev, &rfbdev->rfb, &mode_cmd, gobj);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer %d\n", ret);
		goto out;
	}

	fb = &rfbdev->rfb.base;

	/* setup helper */
	rfbdev->helper.fb = fb;

	memset_io(abo->kptr, 0x0, amdgpu_bo_size(abo));

	strcpy(info->fix.id, "amdgpudrmfb");

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &amdgpufb_ops;

	tmp = amdgpu_bo_gpu_offset(abo) - adev->mc.vram_start;
	info->fix.smem_start = adev->mc.aper_base + tmp;
	info->fix.smem_len = amdgpu_bo_size(abo);
	info->screen_base = abo->kptr;
	info->screen_size = amdgpu_bo_size(abo);

	drm_fb_helper_fill_var(info, &rfbdev->helper, sizes->fb_width, sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->apertures->ranges[0].base = adev->ddev->mode_config.fb_base;
	info->apertures->ranges[0].size = adev->mc.aper_size;

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	if (info->screen_base == NULL) {
		ret = -ENOSPC;
		goto out;
	}

	DRM_INFO("fb mappable at 0x%lX\n",  info->fix.smem_start);
	DRM_INFO("vram apper at 0x%lX\n",  (unsigned long)adev->mc.aper_base);
	DRM_INFO("size %lu\n", (unsigned long)amdgpu_bo_size(abo));
	DRM_INFO("fb depth is %d\n", fb->format->depth);
	DRM_INFO("   pitch is %d\n", fb->pitches[0]);

	vga_switcheroo_client_fb_set(adev->ddev->pdev, info);
	return 0;

out:
	if (abo) {

	}
	if (fb && ret) {
		drm_gem_object_unreference_unlocked(gobj);
		drm_framebuffer_unregister_private(fb);
		drm_framebuffer_cleanup(fb);
		kfree(fb);
	}
	return ret;
}

void amdgpu_fb_output_poll_changed(struct amdgpu_device *adev)
{
	if (adev->mode_info.rfbdev)
		drm_fb_helper_hotplug_event(&adev->mode_info.rfbdev->helper);
}

static int amdgpu_fbdev_destroy(struct drm_device *dev, struct amdgpu_fbdev *rfbdev)
{
	struct amdgpu_framebuffer *rfb = &rfbdev->rfb;

	drm_fb_helper_unregister_fbi(&rfbdev->helper);

	if (rfb->obj) {
		amdgpufb_destroy_pinned_object(rfb->obj);
		rfb->obj = NULL;
	}
	drm_fb_helper_fini(&rfbdev->helper);
	drm_framebuffer_unregister_private(&rfb->base);
	drm_framebuffer_cleanup(&rfb->base);

	return 0;
}

/** Sets the color ramps on behalf of fbcon */
static void amdgpu_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				      u16 blue, int regno)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	amdgpu_crtc->lut_r[regno] = red >> 6;
	amdgpu_crtc->lut_g[regno] = green >> 6;
	amdgpu_crtc->lut_b[regno] = blue >> 6;
}

/** Gets the color ramps on behalf of fbcon */
static void amdgpu_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				      u16 *blue, int regno)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	*red = amdgpu_crtc->lut_r[regno] << 6;
	*green = amdgpu_crtc->lut_g[regno] << 6;
	*blue = amdgpu_crtc->lut_b[regno] << 6;
}

static const struct drm_fb_helper_funcs amdgpu_fb_helper_funcs = {
	.gamma_set = amdgpu_crtc_fb_gamma_set,
	.gamma_get = amdgpu_crtc_fb_gamma_get,
	.fb_probe = amdgpufb_create,
};

int amdgpu_fbdev_init(struct amdgpu_device *adev)
{
	struct amdgpu_fbdev *rfbdev;
	int bpp_sel = 32;
	int ret;

	/* don't init fbdev on hw without DCE */
	if (!adev->mode_info.mode_config_initialized)
		return 0;

	/* don't init fbdev if there are no connectors */
	if (list_empty(&adev->ddev->mode_config.connector_list))
		return 0;

	/* select 8 bpp console on low vram cards */
	if (adev->mc.real_vram_size <= (32*1024*1024))
		bpp_sel = 8;

	rfbdev = kzalloc(sizeof(struct amdgpu_fbdev), GFP_KERNEL);
	if (!rfbdev)
		return -ENOMEM;

	rfbdev->adev = adev;
	adev->mode_info.rfbdev = rfbdev;

	drm_fb_helper_prepare(adev->ddev, &rfbdev->helper,
			&amdgpu_fb_helper_funcs);

	ret = drm_fb_helper_init(adev->ddev, &rfbdev->helper,
				 AMDGPUFB_CONN_LIMIT);
	if (ret) {
		kfree(rfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&rfbdev->helper);

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(adev->ddev);

	drm_fb_helper_initial_config(&rfbdev->helper, bpp_sel);
	return 0;
}

void amdgpu_fbdev_fini(struct amdgpu_device *adev)
{
	if (!adev->mode_info.rfbdev)
		return;

	amdgpu_fbdev_destroy(adev->ddev, adev->mode_info.rfbdev);
	kfree(adev->mode_info.rfbdev);
	adev->mode_info.rfbdev = NULL;
}

void amdgpu_fbdev_set_suspend(struct amdgpu_device *adev, int state)
{
	if (adev->mode_info.rfbdev)
		drm_fb_helper_set_suspend(&adev->mode_info.rfbdev->helper,
			state);
}

int amdgpu_fbdev_total_size(struct amdgpu_device *adev)
{
	struct amdgpu_bo *robj;
	int size = 0;

	if (!adev->mode_info.rfbdev)
		return 0;

	robj = gem_to_amdgpu_bo(adev->mode_info.rfbdev->rfb.obj);
	size += amdgpu_bo_size(robj);
	return size;
}

bool amdgpu_fbdev_robj_is_fb(struct amdgpu_device *adev, struct amdgpu_bo *robj)
{
	if (!adev->mode_info.rfbdev)
		return false;
	if (robj == gem_to_amdgpu_bo(adev->mode_info.rfbdev->rfb.obj))
		return true;
	return false;
}

void amdgpu_fbdev_restore_mode(struct amdgpu_device *adev)
{
	struct amdgpu_fbdev *afbdev = adev->mode_info.rfbdev;
	struct drm_fb_helper *fb_helper;
	int ret;

	if (!afbdev)
		return;

	fb_helper = &afbdev->helper;

	ret = drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);
	if (ret)
		DRM_DEBUG("failed to restore crtc mode\n");
}
