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

#include <linux/async.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>

#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_fbdev.h"
#include "intel_frontbuffer.h"

struct intel_fbdev {
	struct drm_fb_helper helper;
	struct intel_framebuffer *fb;
	struct i915_vma *vma;
	unsigned long vma_flags;
	async_cookie_t cookie;
	int preferred_bpp;

	/* Whether or not fbdev hpd processing is temporarily suspended */
	bool hpd_suspended: 1;
	/* Set when a hotplug was received while HPD processing was suspended */
	bool hpd_waiting: 1;

	/* Protects hpd_suspended */
	struct mutex hpd_lock;
};

static struct intel_frontbuffer *to_frontbuffer(struct intel_fbdev *ifbdev)
{
	return ifbdev->fb->frontbuffer;
}

static void intel_fbdev_invalidate(struct intel_fbdev *ifbdev)
{
	intel_frontbuffer_invalidate(to_frontbuffer(ifbdev), ORIGIN_CPU);
}

static int intel_fbdev_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev =
		container_of(fb_helper, struct intel_fbdev, helper);
	int ret;

	ret = drm_fb_helper_set_par(info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_blank(int blank, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev =
		container_of(fb_helper, struct intel_fbdev, helper);
	int ret;

	ret = drm_fb_helper_blank(blank, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_pan_display(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev =
		container_of(fb_helper, struct intel_fbdev, helper);
	int ret;

	ret = drm_fb_helper_pan_display(var, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static const struct fb_ops intelfb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_set_par = intel_fbdev_set_par,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
	.fb_pan_display = intel_fbdev_pan_display,
	.fb_blank = intel_fbdev_blank,
};

static int intelfb_alloc(struct drm_fb_helper *helper,
			 struct drm_fb_helper_surface_size *sizes)
{
	struct intel_fbdev *ifbdev =
		container_of(helper, struct intel_fbdev, helper);
	struct drm_framebuffer *fb;
	struct drm_device *dev = helper->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct drm_i915_gem_object *obj;
	int size;

	/* we don't do packed 24bpp */
	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width *
				    DIV_ROUND_UP(sizes->surface_bpp, 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = PAGE_ALIGN(size);

	obj = ERR_PTR(-ENODEV);
	if (HAS_LMEM(dev_priv)) {
		obj = i915_gem_object_create_lmem(dev_priv, size,
						  I915_BO_ALLOC_CONTIGUOUS);
	} else {
		/*
		 * If the FB is too big, just don't use it since fbdev is not very
		 * important and we should probably use that space with FBC or other
		 * features.
		 */
		if (size * 2 < dev_priv->stolen_usable_size)
			obj = i915_gem_object_create_stolen(dev_priv, size);
		if (IS_ERR(obj))
			obj = i915_gem_object_create_shmem(dev_priv, size);
	}

	if (IS_ERR(obj)) {
		drm_err(&dev_priv->drm, "failed to allocate framebuffer (%pe)\n", obj);
		return PTR_ERR(obj);
	}

	fb = intel_framebuffer_create(obj, &mode_cmd);
	i915_gem_object_put(obj);
	if (IS_ERR(fb))
		return PTR_ERR(fb);

	ifbdev->fb = to_intel_framebuffer(fb);
	return 0;
}

static int intelfb_create(struct drm_fb_helper *helper,
			  struct drm_fb_helper_surface_size *sizes)
{
	struct intel_fbdev *ifbdev =
		container_of(helper, struct intel_fbdev, helper);
	struct intel_framebuffer *intel_fb = ifbdev->fb;
	struct drm_device *dev = helper->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct i915_ggtt *ggtt = to_gt(dev_priv)->ggtt;
	const struct i915_gtt_view view = {
		.type = I915_GTT_VIEW_NORMAL,
	};
	intel_wakeref_t wakeref;
	struct fb_info *info;
	struct i915_vma *vma;
	unsigned long flags = 0;
	bool prealloc = false;
	void __iomem *vaddr;
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	int ret;

	mutex_lock(&ifbdev->hpd_lock);
	ret = ifbdev->hpd_suspended ? -EAGAIN : 0;
	mutex_unlock(&ifbdev->hpd_lock);
	if (ret)
		return ret;

	if (intel_fb &&
	    (sizes->fb_width > intel_fb->base.width ||
	     sizes->fb_height > intel_fb->base.height)) {
		drm_dbg_kms(&dev_priv->drm,
			    "BIOS fb too small (%dx%d), we require (%dx%d),"
			    " releasing it\n",
			    intel_fb->base.width, intel_fb->base.height,
			    sizes->fb_width, sizes->fb_height);
		drm_framebuffer_put(&intel_fb->base);
		intel_fb = ifbdev->fb = NULL;
	}
	if (!intel_fb || drm_WARN_ON(dev, !intel_fb_obj(&intel_fb->base))) {
		drm_dbg_kms(&dev_priv->drm,
			    "no BIOS fb, allocating a new one\n");
		ret = intelfb_alloc(helper, sizes);
		if (ret)
			return ret;
		intel_fb = ifbdev->fb;
	} else {
		drm_dbg_kms(&dev_priv->drm, "re-using BIOS fb\n");
		prealloc = true;
		sizes->fb_width = intel_fb->base.width;
		sizes->fb_height = intel_fb->base.height;
	}

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	/* Pin the GGTT vma for our access via info->screen_base.
	 * This also validates that any existing fb inherited from the
	 * BIOS is suitable for own access.
	 */
	vma = intel_pin_and_fence_fb_obj(&ifbdev->fb->base, false,
					 &view, false, &flags);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_unlock;
	}

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		drm_err(&dev_priv->drm, "Failed to allocate fb_info (%pe)\n", info);
		ret = PTR_ERR(info);
		goto out_unpin;
	}

	ifbdev->helper.fb = &ifbdev->fb->base;

	info->fbops = &intelfb_ops;

	/* setup aperture base/size for vesafb takeover */
	obj = intel_fb_obj(&intel_fb->base);
	if (i915_gem_object_is_lmem(obj)) {
		struct intel_memory_region *mem = obj->mm.region;

		info->apertures->ranges[0].base = mem->io_start;
		info->apertures->ranges[0].size = mem->io_size;

		/* Use fbdev's framebuffer from lmem for discrete */
		info->fix.smem_start =
			(unsigned long)(mem->io_start +
					i915_gem_object_get_dma_address(obj, 0));
		info->fix.smem_len = obj->base.size;
	} else {
		info->apertures->ranges[0].base = ggtt->gmadr.start;
		info->apertures->ranges[0].size = ggtt->mappable_end;

		/* Our framebuffer is the entirety of fbdev's system memory */
		info->fix.smem_start =
			(unsigned long)(ggtt->gmadr.start + vma->node.start);
		info->fix.smem_len = vma->size;
	}

	for_i915_gem_ww(&ww, ret, false) {
		ret = i915_gem_object_lock(vma->obj, &ww);

		if (ret)
			continue;

		vaddr = i915_vma_pin_iomap(vma);
		if (IS_ERR(vaddr)) {
			drm_err(&dev_priv->drm,
				"Failed to remap framebuffer into virtual memory (%pe)\n", vaddr);
			ret = PTR_ERR(vaddr);
			continue;
		}
	}

	if (ret)
		goto out_unpin;

	info->screen_base = vaddr;
	info->screen_size = vma->size;

	drm_fb_helper_fill_info(info, &ifbdev->helper, sizes);

	/* If the object is shmemfs backed, it will have given us zeroed pages.
	 * If the object is stolen however, it will be full of whatever
	 * garbage was left in there.
	 */
	if (!i915_gem_object_is_shmem(vma->obj) && !prealloc)
		memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	drm_dbg_kms(&dev_priv->drm, "allocated %dx%d fb: 0x%08x\n",
		    ifbdev->fb->base.width, ifbdev->fb->base.height,
		    i915_ggtt_offset(vma));
	ifbdev->vma = vma;
	ifbdev->vma_flags = flags;

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	vga_switcheroo_client_fb_set(pdev, info);
	return 0;

out_unpin:
	intel_unpin_fb_vma(vma, flags);
out_unlock:
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	return ret;
}

static const struct drm_fb_helper_funcs intel_fb_helper_funcs = {
	.fb_probe = intelfb_create,
};

static void intel_fbdev_destroy(struct intel_fbdev *ifbdev)
{
	/* We rely on the object-free to release the VMA pinning for
	 * the info->screen_base mmaping. Leaking the VMA is simpler than
	 * trying to rectify all the possible error paths leading here.
	 */

	drm_fb_helper_fini(&ifbdev->helper);

	if (ifbdev->vma)
		intel_unpin_fb_vma(ifbdev->vma, ifbdev->vma_flags);

	if (ifbdev->fb)
		drm_framebuffer_remove(&ifbdev->fb->base);

	kfree(ifbdev);
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
static bool intel_fbdev_init_bios(struct drm_device *dev,
				  struct intel_fbdev *ifbdev)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_framebuffer *fb = NULL;
	struct intel_crtc *crtc;
	unsigned int max_size = 0;

	/* Find the largest fb */
	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct drm_i915_gem_object *obj =
			intel_fb_obj(plane_state->uapi.fb);

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(&i915->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		if (!obj) {
			drm_dbg_kms(&i915->drm,
				    "[PLANE:%d:%s] no fb, skipping\n",
				    plane->base.base.id, plane->base.name);
			continue;
		}

		if (obj->base.size > max_size) {
			drm_dbg_kms(&i915->drm,
				    "found possible fb from [PLANE:%d:%s]\n",
				    plane->base.base.id, plane->base.name);
			fb = to_intel_framebuffer(plane_state->uapi.fb);
			max_size = obj->base.size;
		}
	}

	if (!fb) {
		drm_dbg_kms(&i915->drm,
			    "no active fbs found, not using BIOS config\n");
		goto out;
	}

	/* Now make sure all the pipes will fit into it */
	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		unsigned int cur_size;

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(&i915->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		drm_dbg_kms(&i915->drm, "checking [PLANE:%d:%s] for BIOS fb\n",
			    plane->base.base.id, plane->base.name);

		/*
		 * See if the plane fb we found above will fit on this
		 * pipe.  Note we need to use the selected fb's pitch and bpp
		 * rather than the current pipe's, since they differ.
		 */
		cur_size = crtc_state->uapi.adjusted_mode.crtc_hdisplay;
		cur_size = cur_size * fb->base.format->cpp[0];
		if (fb->base.pitches[0] < cur_size) {
			drm_dbg_kms(&i915->drm,
				    "fb not wide enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, fb->base.pitches[0]);
			fb = NULL;
			break;
		}

		cur_size = crtc_state->uapi.adjusted_mode.crtc_vdisplay;
		cur_size = intel_fb_align_height(&fb->base, 0, cur_size);
		cur_size *= fb->base.pitches[0];
		drm_dbg_kms(&i915->drm,
			    "[CRTC:%d:%s] area: %dx%d, bpp: %d, size: %d\n",
			    crtc->base.base.id, crtc->base.name,
			    crtc_state->uapi.adjusted_mode.crtc_hdisplay,
			    crtc_state->uapi.adjusted_mode.crtc_vdisplay,
			    fb->base.format->cpp[0] * 8,
			    cur_size);

		if (cur_size > max_size) {
			drm_dbg_kms(&i915->drm,
				    "fb not big enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, max_size);
			fb = NULL;
			break;
		}

		drm_dbg_kms(&i915->drm,
			    "fb big enough [PLANE:%d:%s] (%d >= %d)\n",
			    plane->base.base.id, plane->base.name,
			    max_size, cur_size);
	}

	if (!fb) {
		drm_dbg_kms(&i915->drm,
			    "BIOS fb not suitable for all pipes, not using\n");
		goto out;
	}

	ifbdev->preferred_bpp = fb->base.format->cpp[0] * 8;
	ifbdev->fb = fb;

	drm_framebuffer_get(&ifbdev->fb->base);

	/* Final pass to check if any active pipes don't have fbs */
	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (!crtc_state->uapi.active)
			continue;

		drm_WARN(dev, !plane_state->uapi.fb,
			 "re-used BIOS config but lost an fb on [PLANE:%d:%s]\n",
			 plane->base.base.id, plane->base.name);
	}


	drm_dbg_kms(&i915->drm, "using BIOS fb for initial console\n");
	return true;

out:

	return false;
}

static void intel_fbdev_suspend_worker(struct work_struct *work)
{
	intel_fbdev_set_suspend(&container_of(work,
					      struct drm_i915_private,
					      display.fbdev.suspend_work)->drm,
				FBINFO_STATE_RUNNING,
				true);
}

int intel_fbdev_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev;
	int ret;

	if (drm_WARN_ON(dev, !HAS_DISPLAY(dev_priv)))
		return -ENODEV;

	ifbdev = kzalloc(sizeof(struct intel_fbdev), GFP_KERNEL);
	if (ifbdev == NULL)
		return -ENOMEM;

	mutex_init(&ifbdev->hpd_lock);
	drm_fb_helper_prepare(dev, &ifbdev->helper, &intel_fb_helper_funcs);

	if (!intel_fbdev_init_bios(dev, ifbdev))
		ifbdev->preferred_bpp = 32;

	ret = drm_fb_helper_init(dev, &ifbdev->helper);
	if (ret) {
		kfree(ifbdev);
		return ret;
	}

	dev_priv->display.fbdev.fbdev = ifbdev;
	INIT_WORK(&dev_priv->display.fbdev.suspend_work, intel_fbdev_suspend_worker);

	return 0;
}

static void intel_fbdev_initial_config(void *data, async_cookie_t cookie)
{
	struct intel_fbdev *ifbdev = data;

	/* Due to peculiar init order wrt to hpd handling this is separate. */
	if (drm_fb_helper_initial_config(&ifbdev->helper,
					 ifbdev->preferred_bpp))
		intel_fbdev_unregister(to_i915(ifbdev->helper.dev));
}

void intel_fbdev_initial_config_async(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->display.fbdev.fbdev;

	if (!ifbdev)
		return;

	ifbdev->cookie = async_schedule(intel_fbdev_initial_config, ifbdev);
}

static void intel_fbdev_sync(struct intel_fbdev *ifbdev)
{
	if (!ifbdev->cookie)
		return;

	/* Only serialises with all preceding async calls, hence +1 */
	async_synchronize_cookie(ifbdev->cookie + 1);
	ifbdev->cookie = 0;
}

void intel_fbdev_unregister(struct drm_i915_private *dev_priv)
{
	struct intel_fbdev *ifbdev = dev_priv->display.fbdev.fbdev;

	if (!ifbdev)
		return;

	intel_fbdev_set_suspend(&dev_priv->drm, FBINFO_STATE_SUSPENDED, true);

	if (!current_is_async())
		intel_fbdev_sync(ifbdev);

	drm_fb_helper_unregister_fbi(&ifbdev->helper);
}

void intel_fbdev_fini(struct drm_i915_private *dev_priv)
{
	struct intel_fbdev *ifbdev = fetch_and_zero(&dev_priv->display.fbdev.fbdev);

	if (!ifbdev)
		return;

	intel_fbdev_destroy(ifbdev);
}

/* Suspends/resumes fbdev processing of incoming HPD events. When resuming HPD
 * processing, fbdev will perform a full connector reprobe if a hotplug event
 * was received while HPD was suspended.
 */
static void intel_fbdev_hpd_set_suspend(struct drm_i915_private *i915, int state)
{
	struct intel_fbdev *ifbdev = i915->display.fbdev.fbdev;
	bool send_hpd = false;

	mutex_lock(&ifbdev->hpd_lock);
	ifbdev->hpd_suspended = state == FBINFO_STATE_SUSPENDED;
	send_hpd = !ifbdev->hpd_suspended && ifbdev->hpd_waiting;
	ifbdev->hpd_waiting = false;
	mutex_unlock(&ifbdev->hpd_lock);

	if (send_hpd) {
		drm_dbg_kms(&i915->drm, "Handling delayed fbcon HPD event\n");
		drm_fb_helper_hotplug_event(&ifbdev->helper);
	}
}

void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev = dev_priv->display.fbdev.fbdev;
	struct fb_info *info;

	if (!ifbdev)
		return;

	if (drm_WARN_ON(&dev_priv->drm, !HAS_DISPLAY(dev_priv)))
		return;

	if (!ifbdev->vma)
		goto set_suspend;

	info = ifbdev->helper.fbdev;

	if (synchronous) {
		/* Flush any pending work to turn the console on, and then
		 * wait to turn it off. It must be synchronous as we are
		 * about to suspend or unload the driver.
		 *
		 * Note that from within the work-handler, we cannot flush
		 * ourselves, so only flush outstanding work upon suspend!
		 */
		if (state != FBINFO_STATE_RUNNING)
			flush_work(&dev_priv->display.fbdev.suspend_work);

		console_lock();
	} else {
		/*
		 * The console lock can be pretty contented on resume due
		 * to all the printk activity.  Try to keep it out of the hot
		 * path of resume if possible.
		 */
		drm_WARN_ON(dev, state != FBINFO_STATE_RUNNING);
		if (!console_trylock()) {
			/* Don't block our own workqueue as this can
			 * be run in parallel with other i915.ko tasks.
			 */
			schedule_work(&dev_priv->display.fbdev.suspend_work);
			return;
		}
	}

	/* On resume from hibernation: If the object is shmemfs backed, it has
	 * been restored from swap. If the object is stolen however, it will be
	 * full of whatever garbage was left in there.
	 */
	if (state == FBINFO_STATE_RUNNING &&
	    !i915_gem_object_is_shmem(intel_fb_obj(&ifbdev->fb->base)))
		memset_io(info->screen_base, 0, info->screen_size);

	drm_fb_helper_set_suspend(&ifbdev->helper, state);
	console_unlock();

set_suspend:
	intel_fbdev_hpd_set_suspend(dev_priv, state);
}

void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->display.fbdev.fbdev;
	bool send_hpd;

	if (!ifbdev)
		return;

	intel_fbdev_sync(ifbdev);

	mutex_lock(&ifbdev->hpd_lock);
	send_hpd = !ifbdev->hpd_suspended;
	ifbdev->hpd_waiting = true;
	mutex_unlock(&ifbdev->hpd_lock);

	if (send_hpd && (ifbdev->vma || ifbdev->helper.deferred_setup))
		drm_fb_helper_hotplug_event(&ifbdev->helper);
}

void intel_fbdev_restore_mode(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->display.fbdev.fbdev;

	if (!ifbdev)
		return;

	intel_fbdev_sync(ifbdev);
	if (!ifbdev->vma)
		return;

	if (drm_fb_helper_restore_fbdev_mode_unlocked(&ifbdev->helper) == 0)
		intel_fbdev_invalidate(ifbdev);
}

struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	if (!fbdev || !fbdev->helper.fb)
		return NULL;

	return to_intel_framebuffer(fbdev->helper.fb);
}
