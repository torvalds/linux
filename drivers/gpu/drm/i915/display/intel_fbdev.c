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

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/vga_switcheroo.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "i915_vma.h"
#include "intel_bo.h"
#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_fbdev.h"
#include "intel_fbdev_fb.h"
#include "intel_frontbuffer.h"

struct intel_fbdev {
	struct intel_framebuffer *fb;
	struct i915_vma *vma;
	unsigned long vma_flags;
};

static struct intel_fbdev *to_intel_fbdev(struct drm_fb_helper *fb_helper)
{
	struct intel_display *display = to_intel_display(fb_helper->client.dev);

	return display->fbdev.fbdev;
}

static struct intel_frontbuffer *to_frontbuffer(struct intel_fbdev *ifbdev)
{
	return ifbdev->fb->frontbuffer;
}

static void intel_fbdev_invalidate(struct intel_fbdev *ifbdev)
{
	intel_frontbuffer_invalidate(to_frontbuffer(ifbdev), ORIGIN_CPU);
}

FB_GEN_DEFAULT_DEFERRED_IOMEM_OPS(intel_fbdev,
				  drm_fb_helper_damage_range,
				  drm_fb_helper_damage_area)

static int intel_fbdev_set_par(struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_set_par(info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_blank(int blank, struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_blank(blank, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_pan_display(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_pan_display(var, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb_helper->fb, 0);

	return intel_bo_fb_mmap(obj, vma);
}

static void intel_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev = to_intel_fbdev(fb_helper);

	drm_fb_helper_fini(fb_helper);

	/*
	 * We rely on the object-free to release the VMA pinning for
	 * the info->screen_base mmaping. Leaking the VMA is simpler than
	 * trying to rectify all the possible error paths leading here.
	 */
	intel_fb_unpin_vma(ifbdev->vma, ifbdev->vma_flags);
	drm_framebuffer_remove(fb_helper->fb);

	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field initialization overrides for fb ops");

static const struct fb_ops intelfb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_DEFERRED_OPS_RDWR(intel_fbdev),
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_set_par = intel_fbdev_set_par,
	.fb_blank = intel_fbdev_blank,
	.fb_pan_display = intel_fbdev_pan_display,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(intel_fbdev),
	.fb_mmap = intel_fbdev_mmap,
	.fb_destroy = intel_fbdev_fb_destroy,
};

__diag_pop();

static int intelfb_dirty(struct drm_fb_helper *helper, struct drm_clip_rect *clip)
{
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty)
		return helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);

	return 0;
}

static void intelfb_restore(struct drm_fb_helper *fb_helper)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(fb_helper);

	intel_fbdev_invalidate(ifbdev);
}

static void intelfb_set_suspend(struct drm_fb_helper *fb_helper, bool suspend)
{
	struct fb_info *info = fb_helper->info;

	/*
	 * When resuming from hibernation, Linux restores the object's
	 * content from swap if the buffer is backed by shmemfs. If the
	 * object is stolen however, it will be full of whatever garbage
	 * was left in there. Clear it to zero in this case.
	 */
	if (!suspend && !intel_bo_is_shmem(intel_fb_bo(fb_helper->fb)))
		memset_io(info->screen_base, 0, info->screen_size);

	fb_set_suspend(info, suspend);
}

static const struct drm_fb_helper_funcs intel_fb_helper_funcs = {
	.fb_dirty = intelfb_dirty,
	.fb_restore = intelfb_restore,
	.fb_set_suspend = intelfb_set_suspend,
};

int intel_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes)
{
	struct intel_display *display = to_intel_display(helper->dev);
	struct intel_fbdev *ifbdev = to_intel_fbdev(helper);
	struct intel_framebuffer *fb = ifbdev->fb;
	struct ref_tracker *wakeref;
	struct fb_info *info;
	struct i915_vma *vma;
	unsigned long flags = 0;
	bool prealloc = false;
	struct drm_gem_object *obj;
	int ret;

	ifbdev->fb = NULL;

	if (fb &&
	    (sizes->fb_width > fb->base.width ||
	     sizes->fb_height > fb->base.height)) {
		drm_dbg_kms(display->drm,
			    "BIOS fb too small (%dx%d), we require (%dx%d),"
			    " releasing it\n",
			    fb->base.width, fb->base.height,
			    sizes->fb_width, sizes->fb_height);
		drm_framebuffer_put(&fb->base);
		fb = NULL;
	}
	if (!fb || drm_WARN_ON(display->drm, !intel_fb_bo(&fb->base))) {
		drm_dbg_kms(display->drm,
			    "no BIOS fb, allocating a new one\n");
		fb = intel_fbdev_fb_alloc(helper, sizes);
		if (IS_ERR(fb))
			return PTR_ERR(fb);
	} else {
		drm_dbg_kms(display->drm, "re-using BIOS fb\n");
		prealloc = true;
		sizes->fb_width = fb->base.width;
		sizes->fb_height = fb->base.height;
	}

	wakeref = intel_display_rpm_get(display);

	/* Pin the GGTT vma for our access via info->screen_base.
	 * This also validates that any existing fb inherited from the
	 * BIOS is suitable for own access.
	 */
	vma = intel_fb_pin_to_ggtt(&fb->base, &fb->normal_view.gtt,
				   fb->min_alignment, 0,
				   intel_fb_view_vtd_guard(&fb->base, &fb->normal_view,
							   DRM_MODE_ROTATE_0),
				   false, &flags);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_unlock;
	}

	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		drm_err(display->drm, "Failed to allocate fb_info (%pe)\n", info);
		ret = PTR_ERR(info);
		goto out_unpin;
	}

	helper->funcs = &intel_fb_helper_funcs;
	helper->fb = &fb->base;

	info->fbops = &intelfb_ops;

	obj = intel_fb_bo(&fb->base);

	ret = intel_fbdev_fb_fill_info(display, info, obj, vma);
	if (ret)
		goto out_unpin;

	drm_fb_helper_fill_info(info, display->drm->fb_helper, sizes);

	/* If the object is shmemfs backed, it will have given us zeroed pages.
	 * If the object is stolen however, it will be full of whatever
	 * garbage was left in there.
	 */
	if (!intel_bo_is_shmem(obj) && !prealloc)
		memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	drm_dbg_kms(display->drm, "allocated %dx%d fb: 0x%08x\n",
		    fb->base.width, fb->base.height,
		    i915_ggtt_offset(vma));
	ifbdev->fb = fb;
	ifbdev->vma = vma;
	ifbdev->vma_flags = flags;

	intel_display_rpm_put(display, wakeref);

	return 0;

out_unpin:
	intel_fb_unpin_vma(vma, flags);
out_unlock:
	intel_display_rpm_put(display, wakeref);

	return ret;
}

/*
 * Build an intel_fbdev struct using a BIOS allocated framebuffer, if possible.
 * The core display code will have read out the current plane configuration,
 * so we use that to figure out if there's an object for us to use as the
 * fb, and if so, we re-use it for the fbdev configuration.
 *
 * Note we only support a single fb shared across pipes for boot (mostly for
 * fbcon), so we just find the biggest and use that.
 */
static bool intel_fbdev_init_bios(struct intel_display *display,
				  struct intel_fbdev *ifbdev)
{
	struct intel_framebuffer *fb = NULL;
	struct intel_crtc *crtc;
	unsigned int max_size = 0;

	/* Find the largest fb */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct drm_gem_object *obj = intel_fb_bo(plane_state->uapi.fb);

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		if (!obj) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] no fb, skipping\n",
				    plane->base.base.id, plane->base.name);
			continue;
		}

		if (obj->size > max_size) {
			drm_dbg_kms(display->drm,
				    "found possible fb from [PLANE:%d:%s]\n",
				    plane->base.base.id, plane->base.name);
			fb = to_intel_framebuffer(plane_state->uapi.fb);
			max_size = obj->size;
		}
	}

	if (!fb) {
		drm_dbg_kms(display->drm,
			    "no active fbs found, not using BIOS config\n");
		goto out;
	}

	/* Now make sure all the pipes will fit into it */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		unsigned int cur_size;

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		drm_dbg_kms(display->drm, "checking [PLANE:%d:%s] for BIOS fb\n",
			    plane->base.base.id, plane->base.name);

		/*
		 * See if the plane fb we found above will fit on this
		 * pipe.  Note we need to use the selected fb's pitch and bpp
		 * rather than the current pipe's, since they differ.
		 */
		cur_size = crtc_state->uapi.adjusted_mode.crtc_hdisplay;
		cur_size = cur_size * fb->base.format->cpp[0];
		if (fb->base.pitches[0] < cur_size) {
			drm_dbg_kms(display->drm,
				    "fb not wide enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, fb->base.pitches[0]);
			fb = NULL;
			break;
		}

		cur_size = crtc_state->uapi.adjusted_mode.crtc_vdisplay;
		cur_size = intel_fb_align_height(&fb->base, 0, cur_size);
		cur_size *= fb->base.pitches[0];
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] area: %dx%d, bpp: %d, size: %d\n",
			    crtc->base.base.id, crtc->base.name,
			    crtc_state->uapi.adjusted_mode.crtc_hdisplay,
			    crtc_state->uapi.adjusted_mode.crtc_vdisplay,
			    fb->base.format->cpp[0] * 8,
			    cur_size);

		if (cur_size > max_size) {
			drm_dbg_kms(display->drm,
				    "fb not big enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, max_size);
			fb = NULL;
			break;
		}

		drm_dbg_kms(display->drm,
			    "fb big enough [PLANE:%d:%s] (%d >= %d)\n",
			    plane->base.base.id, plane->base.name,
			    max_size, cur_size);
	}

	if (!fb) {
		drm_dbg_kms(display->drm,
			    "BIOS fb not suitable for all pipes, not using\n");
		goto out;
	}

	ifbdev->fb = fb;

	drm_framebuffer_get(&ifbdev->fb->base);

	/* Final pass to check if any active pipes don't have fbs */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (!crtc_state->uapi.active)
			continue;

		drm_WARN(display->drm, !plane_state->uapi.fb,
			 "re-used BIOS config but lost an fb on [PLANE:%d:%s]\n",
			 plane->base.base.id, plane->base.name);
	}


	drm_dbg_kms(display->drm, "using BIOS fb for initial console\n");
	return true;

out:

	return false;
}

static unsigned int intel_fbdev_color_mode(const struct drm_format_info *info)
{
	unsigned int bpp;

	if (!info->depth || info->num_planes != 1 || info->has_alpha || info->is_yuv)
		return 0;

	bpp = drm_format_info_bpp(info, 0);

	switch (bpp) {
	case 16:
		return info->depth; // 15 or 16
	default:
		return bpp;
	}
}

void intel_fbdev_setup(struct intel_display *display)
{
	struct intel_fbdev *ifbdev;
	unsigned int preferred_bpp = 0;

	if (!HAS_DISPLAY(display))
		return;

	ifbdev = drmm_kzalloc(display->drm, sizeof(*ifbdev), GFP_KERNEL);
	if (!ifbdev)
		return;

	display->fbdev.fbdev = ifbdev;
	if (intel_fbdev_init_bios(display, ifbdev))
		preferred_bpp = intel_fbdev_color_mode(ifbdev->fb->base.format);
	if (!preferred_bpp)
		preferred_bpp = 32;

	drm_client_setup_with_color_mode(display->drm, preferred_bpp);
}

struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	if (!fbdev)
		return NULL;

	return fbdev->fb;
}

struct i915_vma *intel_fbdev_vma_pointer(struct intel_fbdev *fbdev)
{
	return fbdev ? fbdev->vma : NULL;
}

void intel_fbdev_get_map(struct intel_fbdev *fbdev, struct iosys_map *map)
{
	intel_fb_get_map(fbdev->vma, map);
}
