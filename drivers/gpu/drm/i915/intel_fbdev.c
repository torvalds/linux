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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/vga_switcheroo.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

static int intel_fbdev_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev =
		container_of(fb_helper, struct intel_fbdev, helper);
	int ret;

	ret = drm_fb_helper_set_par(info);

	if (ret == 0) {
		mutex_lock(&fb_helper->dev->struct_mutex);
		intel_fb_obj_invalidate(ifbdev->fb->obj, ORIGIN_GTT);
		mutex_unlock(&fb_helper->dev->struct_mutex);
	}

	return ret;
}

static int intel_fbdev_blank(int blank, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev =
		container_of(fb_helper, struct intel_fbdev, helper);
	int ret;

	ret = drm_fb_helper_blank(blank, info);

	if (ret == 0) {
		mutex_lock(&fb_helper->dev->struct_mutex);
		intel_fb_obj_invalidate(ifbdev->fb->obj, ORIGIN_GTT);
		mutex_unlock(&fb_helper->dev->struct_mutex);
	}

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

	if (ret == 0) {
		mutex_lock(&fb_helper->dev->struct_mutex);
		intel_fb_obj_invalidate(ifbdev->fb->obj, ORIGIN_GTT);
		mutex_unlock(&fb_helper->dev->struct_mutex);
	}

	return ret;
}

static struct fb_ops intelfb_ops = {
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
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct drm_i915_gem_object *obj = NULL;
	int size, ret;

	/* we don't do packed 24bpp */
	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width *
				    DIV_ROUND_UP(sizes->surface_bpp, 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	mutex_lock(&dev->struct_mutex);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = PAGE_ALIGN(size);

	/* If the FB is too big, just don't use it since fbdev is not very
	 * important and we should probably use that space with FBC or other
	 * features. */
	if (size * 2 < ggtt->stolen_usable_size)
		obj = i915_gem_object_create_stolen(dev, size);
	if (obj == NULL)
		obj = i915_gem_object_create(dev, size);
	if (IS_ERR(obj)) {
		DRM_ERROR("failed to allocate framebuffer\n");
		ret = PTR_ERR(obj);
		goto out;
	}

	fb = __intel_framebuffer_create(dev, &mode_cmd, obj);
	if (IS_ERR(fb)) {
		i915_gem_object_put(obj);
		ret = PTR_ERR(fb);
		goto out;
	}

	mutex_unlock(&dev->struct_mutex);

	ifbdev->fb = to_intel_framebuffer(fb);

	return 0;

out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int intelfb_create(struct drm_fb_helper *helper,
			  struct drm_fb_helper_surface_size *sizes)
{
	struct intel_fbdev *ifbdev =
		container_of(helper, struct intel_fbdev, helper);
	struct intel_framebuffer *intel_fb = ifbdev->fb;
	struct drm_device *dev = helper->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct i915_vma *vma;
	bool prealloc = false;
	void __iomem *vaddr;
	int ret;

	if (intel_fb &&
	    (sizes->fb_width > intel_fb->base.width ||
	     sizes->fb_height > intel_fb->base.height)) {
		DRM_DEBUG_KMS("BIOS fb too small (%dx%d), we require (%dx%d),"
			      " releasing it\n",
			      intel_fb->base.width, intel_fb->base.height,
			      sizes->fb_width, sizes->fb_height);
		drm_framebuffer_unreference(&intel_fb->base);
		intel_fb = ifbdev->fb = NULL;
	}
	if (!intel_fb || WARN_ON(!intel_fb->obj)) {
		DRM_DEBUG_KMS("no BIOS fb, allocating a new one\n");
		ret = intelfb_alloc(helper, sizes);
		if (ret)
			return ret;
		intel_fb = ifbdev->fb;
	} else {
		DRM_DEBUG_KMS("re-using BIOS fb\n");
		prealloc = true;
		sizes->fb_width = intel_fb->base.width;
		sizes->fb_height = intel_fb->base.height;
	}

	mutex_lock(&dev->struct_mutex);

	/* Pin the GGTT vma for our access via info->screen_base.
	 * This also validates that any existing fb inherited from the
	 * BIOS is suitable for own access.
	 */
	vma = intel_pin_and_fence_fb_obj(&ifbdev->fb->base, DRM_ROTATE_0);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_unlock;
	}

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		DRM_ERROR("Failed to allocate fb_info\n");
		ret = PTR_ERR(info);
		goto out_unpin;
	}

	info->par = helper;

	fb = &ifbdev->fb->base;

	ifbdev->helper.fb = fb;

	strcpy(info->fix.id, "inteldrmfb");

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &intelfb_ops;

	/* setup aperture base/size for vesafb takeover */
	info->apertures->ranges[0].base = dev->mode_config.fb_base;
	info->apertures->ranges[0].size = ggtt->mappable_end;

	info->fix.smem_start = dev->mode_config.fb_base + i915_ggtt_offset(vma);
	info->fix.smem_len = vma->node.size;

	vaddr = i915_vma_pin_iomap(vma);
	if (IS_ERR(vaddr)) {
		DRM_ERROR("Failed to remap framebuffer into virtual memory\n");
		ret = PTR_ERR(vaddr);
		goto out_destroy_fbi;
	}
	info->screen_base = vaddr;
	info->screen_size = vma->node.size;

	/* This driver doesn't need a VT switch to restore the mode on resume */
	info->skip_vt_switch = true;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &ifbdev->helper, sizes->fb_width, sizes->fb_height);

	/* If the object is shmemfs backed, it will have given us zeroed pages.
	 * If the object is stolen however, it will be full of whatever
	 * garbage was left in there.
	 */
	if (intel_fb->obj->stolen && !prealloc)
		memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	DRM_DEBUG_KMS("allocated %dx%d fb: 0x%08x\n",
		      fb->width, fb->height, i915_ggtt_offset(vma));
	ifbdev->vma = vma;

	mutex_unlock(&dev->struct_mutex);
	vga_switcheroo_client_fb_set(pdev, info);
	return 0;

out_destroy_fbi:
	drm_fb_helper_release_fbi(helper);
out_unpin:
	intel_unpin_fb_obj(&ifbdev->fb->base, DRM_ROTATE_0);
out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/** Sets the color ramps on behalf of RandR */
static void intel_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				    u16 blue, int regno)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_crtc->lut_r[regno] = red >> 8;
	intel_crtc->lut_g[regno] = green >> 8;
	intel_crtc->lut_b[regno] = blue >> 8;
}

static void intel_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, int regno)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	*red = intel_crtc->lut_r[regno] << 8;
	*green = intel_crtc->lut_g[regno] << 8;
	*blue = intel_crtc->lut_b[regno] << 8;
}

static struct drm_fb_helper_crtc *
intel_fb_helper_crtc(struct drm_fb_helper *fb_helper, struct drm_crtc *crtc)
{
	int i;

	for (i = 0; i < fb_helper->crtc_count; i++)
		if (fb_helper->crtc_info[i].mode_set.crtc == crtc)
			return &fb_helper->crtc_info[i];

	return NULL;
}

/*
 * Try to read the BIOS display configuration and use it for the initial
 * fb configuration.
 *
 * The BIOS or boot loader will generally create an initial display
 * configuration for us that includes some set of active pipes and displays.
 * This routine tries to figure out which pipes and connectors are active
 * and stuffs them into the crtcs and modes array given to us by the
 * drm_fb_helper code.
 *
 * The overall sequence is:
 *   intel_fbdev_init - from driver load
 *     intel_fbdev_init_bios - initialize the intel_fbdev using BIOS data
 *     drm_fb_helper_init - build fb helper structs
 *     drm_fb_helper_single_add_all_connectors - more fb helper structs
 *   intel_fbdev_initial_config - apply the config
 *     drm_fb_helper_initial_config - call ->probe then register_framebuffer()
 *         drm_setup_crtcs - build crtc config for fbdev
 *           intel_fb_initial_config - find active connectors etc
 *         drm_fb_helper_single_fb_probe - set up fbdev
 *           intelfb_create - re-use or alloc fb, build out fbdev structs
 *
 * Note that we don't make special consideration whether we could actually
 * switch to the selected modes without a full modeset. E.g. when the display
 * is in VGA mode we need to recalculate watermarks and set a new high-res
 * framebuffer anyway.
 */
static bool intel_fb_initial_config(struct drm_fb_helper *fb_helper,
				    struct drm_fb_helper_crtc **crtcs,
				    struct drm_display_mode **modes,
				    struct drm_fb_offset *offsets,
				    bool *enabled, int width, int height)
{
	struct drm_i915_private *dev_priv = to_i915(fb_helper->dev);
	unsigned long conn_configured, mask;
	unsigned int count = min(fb_helper->connector_count, BITS_PER_LONG);
	int i, j;
	bool *save_enabled;
	bool fallback = true;
	int num_connectors_enabled = 0;
	int num_connectors_detected = 0;
	int pass = 0;

	save_enabled = kcalloc(count, sizeof(bool), GFP_KERNEL);
	if (!save_enabled)
		return false;

	memcpy(save_enabled, enabled, count);
	mask = BIT(count) - 1;
	conn_configured = 0;
retry:
	for (i = 0; i < count; i++) {
		struct drm_fb_helper_connector *fb_conn;
		struct drm_connector *connector;
		struct drm_encoder *encoder;
		struct drm_fb_helper_crtc *new_crtc;
		struct intel_crtc *intel_crtc;

		fb_conn = fb_helper->connector_info[i];
		connector = fb_conn->connector;

		if (conn_configured & BIT(i))
			continue;

		if (pass == 0 && !connector->has_tile)
			continue;

		if (connector->status == connector_status_connected)
			num_connectors_detected++;

		if (!enabled[i]) {
			DRM_DEBUG_KMS("connector %s not enabled, skipping\n",
				      connector->name);
			conn_configured |= BIT(i);
			continue;
		}

		if (connector->force == DRM_FORCE_OFF) {
			DRM_DEBUG_KMS("connector %s is disabled by user, skipping\n",
				      connector->name);
			enabled[i] = false;
			continue;
		}

		encoder = connector->state->best_encoder;
		if (!encoder || WARN_ON(!connector->state->crtc)) {
			if (connector->force > DRM_FORCE_OFF)
				goto bail;

			DRM_DEBUG_KMS("connector %s has no encoder or crtc, skipping\n",
				      connector->name);
			enabled[i] = false;
			conn_configured |= BIT(i);
			continue;
		}

		num_connectors_enabled++;

		intel_crtc = to_intel_crtc(connector->state->crtc);
		for (j = 0; j < 256; j++) {
			intel_crtc->lut_r[j] = j;
			intel_crtc->lut_g[j] = j;
			intel_crtc->lut_b[j] = j;
		}

		new_crtc = intel_fb_helper_crtc(fb_helper,
						connector->state->crtc);

		/*
		 * Make sure we're not trying to drive multiple connectors
		 * with a single CRTC, since our cloning support may not
		 * match the BIOS.
		 */
		for (j = 0; j < count; j++) {
			if (crtcs[j] == new_crtc) {
				DRM_DEBUG_KMS("fallback: cloned configuration\n");
				goto bail;
			}
		}

		DRM_DEBUG_KMS("looking for cmdline mode on connector %s\n",
			      connector->name);

		/* go for command line mode first */
		modes[i] = drm_pick_cmdline_mode(fb_conn, width, height);

		/* try for preferred next */
		if (!modes[i]) {
			DRM_DEBUG_KMS("looking for preferred mode on connector %s %d\n",
				      connector->name, connector->has_tile);
			modes[i] = drm_has_preferred_mode(fb_conn, width,
							  height);
		}

		/* No preferred mode marked by the EDID? Are there any modes? */
		if (!modes[i] && !list_empty(&connector->modes)) {
			DRM_DEBUG_KMS("using first mode listed on connector %s\n",
				      connector->name);
			modes[i] = list_first_entry(&connector->modes,
						    struct drm_display_mode,
						    head);
		}

		/* last resort: use current mode */
		if (!modes[i]) {
			/*
			 * IMPORTANT: We want to use the adjusted mode (i.e.
			 * after the panel fitter upscaling) as the initial
			 * config, not the input mode, which is what crtc->mode
			 * usually contains. But since our current
			 * code puts a mode derived from the post-pfit timings
			 * into crtc->mode this works out correctly.
			 *
			 * This is crtc->mode and not crtc->state->mode for the
			 * fastboot check to work correctly. crtc_state->mode has
			 * I915_MODE_FLAG_INHERITED, which we clear to force check
			 * state.
			 */
			DRM_DEBUG_KMS("looking for current mode on connector %s\n",
				      connector->name);
			modes[i] = &connector->state->crtc->mode;
		}
		crtcs[i] = new_crtc;

		DRM_DEBUG_KMS("connector %s on [CRTC:%d:%s]: %dx%d%s\n",
			      connector->name,
			      connector->state->crtc->base.id,
			      connector->state->crtc->name,
			      modes[i]->hdisplay, modes[i]->vdisplay,
			      modes[i]->flags & DRM_MODE_FLAG_INTERLACE ? "i" :"");

		fallback = false;
		conn_configured |= BIT(i);
	}

	if ((conn_configured & mask) != mask) {
		pass++;
		goto retry;
	}

	/*
	 * If the BIOS didn't enable everything it could, fall back to have the
	 * same user experiencing of lighting up as much as possible like the
	 * fbdev helper library.
	 */
	if (num_connectors_enabled != num_connectors_detected &&
	    num_connectors_enabled < INTEL_INFO(dev_priv)->num_pipes) {
		DRM_DEBUG_KMS("fallback: Not all outputs enabled\n");
		DRM_DEBUG_KMS("Enabled: %i, detected: %i\n", num_connectors_enabled,
			      num_connectors_detected);
		fallback = true;
	}

	if (fallback) {
bail:
		DRM_DEBUG_KMS("Not using firmware configuration\n");
		memcpy(enabled, save_enabled, count);
		kfree(save_enabled);
		return false;
	}

	kfree(save_enabled);
	return true;
}

static const struct drm_fb_helper_funcs intel_fb_helper_funcs = {
	.initial_config = intel_fb_initial_config,
	.gamma_set = intel_crtc_fb_gamma_set,
	.gamma_get = intel_crtc_fb_gamma_get,
	.fb_probe = intelfb_create,
};

static void intel_fbdev_destroy(struct intel_fbdev *ifbdev)
{
	/* We rely on the object-free to release the VMA pinning for
	 * the info->screen_base mmaping. Leaking the VMA is simpler than
	 * trying to rectify all the possible error paths leading here.
	 */

	drm_fb_helper_unregister_fbi(&ifbdev->helper);
	drm_fb_helper_release_fbi(&ifbdev->helper);

	drm_fb_helper_fini(&ifbdev->helper);

	if (ifbdev->fb) {
		mutex_lock(&ifbdev->helper.dev->struct_mutex);
		intel_unpin_fb_obj(&ifbdev->fb->base, DRM_ROTATE_0);
		mutex_unlock(&ifbdev->helper.dev->struct_mutex);

		drm_framebuffer_remove(&ifbdev->fb->base);
	}

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
	struct intel_framebuffer *fb = NULL;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;
	unsigned int max_size = 0;

	/* Find the largest fb */
	for_each_crtc(dev, crtc) {
		struct drm_i915_gem_object *obj =
			intel_fb_obj(crtc->primary->state->fb);
		intel_crtc = to_intel_crtc(crtc);

		if (!crtc->state->active || !obj) {
			DRM_DEBUG_KMS("pipe %c not active or no fb, skipping\n",
				      pipe_name(intel_crtc->pipe));
			continue;
		}

		if (obj->base.size > max_size) {
			DRM_DEBUG_KMS("found possible fb from plane %c\n",
				      pipe_name(intel_crtc->pipe));
			fb = to_intel_framebuffer(crtc->primary->state->fb);
			max_size = obj->base.size;
		}
	}

	if (!fb) {
		DRM_DEBUG_KMS("no active fbs found, not using BIOS config\n");
		goto out;
	}

	/* Now make sure all the pipes will fit into it */
	for_each_crtc(dev, crtc) {
		unsigned int cur_size;

		intel_crtc = to_intel_crtc(crtc);

		if (!crtc->state->active) {
			DRM_DEBUG_KMS("pipe %c not active, skipping\n",
				      pipe_name(intel_crtc->pipe));
			continue;
		}

		DRM_DEBUG_KMS("checking plane %c for BIOS fb\n",
			      pipe_name(intel_crtc->pipe));

		/*
		 * See if the plane fb we found above will fit on this
		 * pipe.  Note we need to use the selected fb's pitch and bpp
		 * rather than the current pipe's, since they differ.
		 */
		cur_size = intel_crtc->config->base.adjusted_mode.crtc_hdisplay;
		cur_size = cur_size * fb->base.bits_per_pixel / 8;
		if (fb->base.pitches[0] < cur_size) {
			DRM_DEBUG_KMS("fb not wide enough for plane %c (%d vs %d)\n",
				      pipe_name(intel_crtc->pipe),
				      cur_size, fb->base.pitches[0]);
			fb = NULL;
			break;
		}

		cur_size = intel_crtc->config->base.adjusted_mode.crtc_vdisplay;
		cur_size = intel_fb_align_height(dev, cur_size,
						 fb->base.pixel_format,
						 fb->base.modifier);
		cur_size *= fb->base.pitches[0];
		DRM_DEBUG_KMS("pipe %c area: %dx%d, bpp: %d, size: %d\n",
			      pipe_name(intel_crtc->pipe),
			      intel_crtc->config->base.adjusted_mode.crtc_hdisplay,
			      intel_crtc->config->base.adjusted_mode.crtc_vdisplay,
			      fb->base.bits_per_pixel,
			      cur_size);

		if (cur_size > max_size) {
			DRM_DEBUG_KMS("fb not big enough for plane %c (%d vs %d)\n",
				      pipe_name(intel_crtc->pipe),
				      cur_size, max_size);
			fb = NULL;
			break;
		}

		DRM_DEBUG_KMS("fb big enough for plane %c (%d >= %d)\n",
			      pipe_name(intel_crtc->pipe),
			      max_size, cur_size);
	}

	if (!fb) {
		DRM_DEBUG_KMS("BIOS fb not suitable for all pipes, not using\n");
		goto out;
	}

	ifbdev->preferred_bpp = fb->base.bits_per_pixel;
	ifbdev->fb = fb;

	drm_framebuffer_reference(&ifbdev->fb->base);

	/* Final pass to check if any active pipes don't have fbs */
	for_each_crtc(dev, crtc) {
		intel_crtc = to_intel_crtc(crtc);

		if (!crtc->state->active)
			continue;

		WARN(!crtc->primary->fb,
		     "re-used BIOS config but lost an fb on crtc %d\n",
		     crtc->base.id);
	}


	DRM_DEBUG_KMS("using BIOS fb for initial console\n");
	return true;

out:

	return false;
}

static void intel_fbdev_suspend_worker(struct work_struct *work)
{
	intel_fbdev_set_suspend(&container_of(work,
					      struct drm_i915_private,
					      fbdev_suspend_work)->drm,
				FBINFO_STATE_RUNNING,
				true);
}

int intel_fbdev_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev;
	int ret;

	if (WARN_ON(INTEL_INFO(dev_priv)->num_pipes == 0))
		return -ENODEV;

	ifbdev = kzalloc(sizeof(struct intel_fbdev), GFP_KERNEL);
	if (ifbdev == NULL)
		return -ENOMEM;

	drm_fb_helper_prepare(dev, &ifbdev->helper, &intel_fb_helper_funcs);

	if (!intel_fbdev_init_bios(dev, ifbdev))
		ifbdev->preferred_bpp = 32;

	ret = drm_fb_helper_init(dev, &ifbdev->helper,
				 INTEL_INFO(dev_priv)->num_pipes, 4);
	if (ret) {
		kfree(ifbdev);
		return ret;
	}

	dev_priv->fbdev = ifbdev;
	INIT_WORK(&dev_priv->fbdev_suspend_work, intel_fbdev_suspend_worker);

	drm_fb_helper_single_add_all_connectors(&ifbdev->helper);

	return 0;
}

static void intel_fbdev_initial_config(void *data, async_cookie_t cookie)
{
	struct intel_fbdev *ifbdev = data;

	/* Due to peculiar init order wrt to hpd handling this is separate. */
	if (drm_fb_helper_initial_config(&ifbdev->helper,
					 ifbdev->preferred_bpp))
		intel_fbdev_fini(ifbdev->helper.dev);
}

void intel_fbdev_initial_config_async(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->fbdev;

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

void intel_fbdev_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev = dev_priv->fbdev;

	if (!ifbdev)
		return;

	cancel_work_sync(&dev_priv->fbdev_suspend_work);
	if (!current_is_async())
		intel_fbdev_sync(ifbdev);

	intel_fbdev_destroy(ifbdev);
	dev_priv->fbdev = NULL;
}

void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev = dev_priv->fbdev;
	struct fb_info *info;

	if (!ifbdev || !ifbdev->fb)
		return;

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
			flush_work(&dev_priv->fbdev_suspend_work);
		console_lock();
	} else {
		/*
		 * The console lock can be pretty contented on resume due
		 * to all the printk activity.  Try to keep it out of the hot
		 * path of resume if possible.
		 */
		WARN_ON(state != FBINFO_STATE_RUNNING);
		if (!console_trylock()) {
			/* Don't block our own workqueue as this can
			 * be run in parallel with other i915.ko tasks.
			 */
			schedule_work(&dev_priv->fbdev_suspend_work);
			return;
		}
	}

	/* On resume from hibernation: If the object is shmemfs backed, it has
	 * been restored from swap. If the object is stolen however, it will be
	 * full of whatever garbage was left in there.
	 */
	if (state == FBINFO_STATE_RUNNING && ifbdev->fb->obj->stolen)
		memset_io(info->screen_base, 0, info->screen_size);

	drm_fb_helper_set_suspend(&ifbdev->helper, state);
	console_unlock();
}

void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->fbdev;

	if (ifbdev && ifbdev->fb)
		drm_fb_helper_hotplug_event(&ifbdev->helper);
}

void intel_fbdev_restore_mode(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->fbdev;

	if (!ifbdev)
		return;

	intel_fbdev_sync(ifbdev);
	if (!ifbdev->fb)
		return;

	if (drm_fb_helper_restore_fbdev_mode_unlocked(&ifbdev->helper)) {
		DRM_DEBUG("failed to restore crtc mode\n");
	} else {
		mutex_lock(&dev->struct_mutex);
		intel_fb_obj_invalidate(ifbdev->fb->obj, ORIGIN_GTT);
		mutex_unlock(&dev->struct_mutex);
	}
}
