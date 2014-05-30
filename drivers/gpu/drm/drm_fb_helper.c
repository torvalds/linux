/*
 * Copyright (c) 2006-2009 Red Hat Inc.
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM framebuffer helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

static LIST_HEAD(kernel_fb_helper_list);

/**
 * DOC: fbdev helpers
 *
 * The fb helper functions are useful to provide an fbdev on top of a drm kernel
 * mode setting driver. They can be used mostly independently from the crtc
 * helper functions used by many drivers to implement the kernel mode setting
 * interfaces.
 *
 * Initialization is done as a three-step process with drm_fb_helper_init(),
 * drm_fb_helper_single_add_all_connectors() and drm_fb_helper_initial_config().
 * Drivers with fancier requirements than the default behaviour can override the
 * second step with their own code.  Teardown is done with drm_fb_helper_fini().
 *
 * At runtime drivers should restore the fbdev console by calling
 * drm_fb_helper_restore_fbdev_mode() from their ->lastclose callback. They
 * should also notify the fb helper code from updates to the output
 * configuration by calling drm_fb_helper_hotplug_event(). For easier
 * integration with the output polling code in drm_crtc_helper.c the modeset
 * code provides a ->output_poll_changed callback.
 *
 * All other functions exported by the fb helper library can be used to
 * implement the fbdev driver interface by the driver.
 */

/**
 * drm_fb_helper_single_add_all_connectors() - add all connectors to fbdev
 * 					       emulation helper
 * @fb_helper: fbdev initialized with drm_fb_helper_init
 *
 * This functions adds all the available connectors for use with the given
 * fb_helper. This is a separate step to allow drivers to freely assign
 * connectors to the fbdev, e.g. if some are reserved for special purposes or
 * not adequate to be used for the fbcon.
 *
 * Since this is part of the initial setup before the fbdev is published, no
 * locking is required.
 */
int drm_fb_helper_single_add_all_connectors(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_connector *connector;
	int i;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_fb_helper_connector *fb_helper_connector;

		fb_helper_connector = kzalloc(sizeof(struct drm_fb_helper_connector), GFP_KERNEL);
		if (!fb_helper_connector)
			goto fail;

		fb_helper_connector->connector = connector;
		fb_helper->connector_info[fb_helper->connector_count++] = fb_helper_connector;
	}
	return 0;
fail:
	for (i = 0; i < fb_helper->connector_count; i++) {
		kfree(fb_helper->connector_info[i]);
		fb_helper->connector_info[i] = NULL;
	}
	fb_helper->connector_count = 0;
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_fb_helper_single_add_all_connectors);

static int drm_fb_helper_parse_command_line(struct drm_fb_helper *fb_helper)
{
	struct drm_fb_helper_connector *fb_helper_conn;
	int i;

	for (i = 0; i < fb_helper->connector_count; i++) {
		struct drm_cmdline_mode *mode;
		struct drm_connector *connector;
		char *option = NULL;

		fb_helper_conn = fb_helper->connector_info[i];
		connector = fb_helper_conn->connector;
		mode = &fb_helper_conn->cmdline_mode;

		/* do something on return - turn off connector maybe */
		if (fb_get_options(connector->name, &option))
			continue;

		if (drm_mode_parse_command_line_for_connector(option,
							      connector,
							      mode)) {
			if (mode->force) {
				const char *s;
				switch (mode->force) {
				case DRM_FORCE_OFF:
					s = "OFF";
					break;
				case DRM_FORCE_ON_DIGITAL:
					s = "ON - dig";
					break;
				default:
				case DRM_FORCE_ON:
					s = "ON";
					break;
				}

				DRM_INFO("forcing %s connector %s\n",
					 connector->name, s);
				connector->force = mode->force;
			}

			DRM_DEBUG_KMS("cmdline mode for connector %s %dx%d@%dHz%s%s%s\n",
				      connector->name,
				      mode->xres, mode->yres,
				      mode->refresh_specified ? mode->refresh : 60,
				      mode->rb ? " reduced blanking" : "",
				      mode->margins ? " with margins" : "",
				      mode->interlace ?  " interlaced" : "");
		}

	}
	return 0;
}

static void drm_fb_helper_save_lut_atomic(struct drm_crtc *crtc, struct drm_fb_helper *helper)
{
	uint16_t *r_base, *g_base, *b_base;
	int i;

	if (helper->funcs->gamma_get == NULL)
		return;

	r_base = crtc->gamma_store;
	g_base = r_base + crtc->gamma_size;
	b_base = g_base + crtc->gamma_size;

	for (i = 0; i < crtc->gamma_size; i++)
		helper->funcs->gamma_get(crtc, &r_base[i], &g_base[i], &b_base[i], i);
}

static void drm_fb_helper_restore_lut_atomic(struct drm_crtc *crtc)
{
	uint16_t *r_base, *g_base, *b_base;

	if (crtc->funcs->gamma_set == NULL)
		return;

	r_base = crtc->gamma_store;
	g_base = r_base + crtc->gamma_size;
	b_base = g_base + crtc->gamma_size;

	crtc->funcs->gamma_set(crtc, r_base, g_base, b_base, 0, crtc->gamma_size);
}

/**
 * drm_fb_helper_debug_enter - implementation for ->fb_debug_enter
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_debug_enter(struct fb_info *info)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_crtc_helper_funcs *funcs;
	int i;

	if (list_empty(&kernel_fb_helper_list))
		return false;

	list_for_each_entry(helper, &kernel_fb_helper_list, kernel_fb_list) {
		for (i = 0; i < helper->crtc_count; i++) {
			struct drm_mode_set *mode_set =
				&helper->crtc_info[i].mode_set;

			if (!mode_set->crtc->enabled)
				continue;

			funcs =	mode_set->crtc->helper_private;
			drm_fb_helper_save_lut_atomic(mode_set->crtc, helper);
			funcs->mode_set_base_atomic(mode_set->crtc,
						    mode_set->fb,
						    mode_set->x,
						    mode_set->y,
						    ENTER_ATOMIC_MODE_SET);
		}
	}

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_debug_enter);

/* Find the real fb for a given fb helper CRTC */
static struct drm_framebuffer *drm_mode_config_fb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *c;

	list_for_each_entry(c, &dev->mode_config.crtc_list, head) {
		if (crtc->base.id == c->base.id)
			return c->primary->fb;
	}

	return NULL;
}

/**
 * drm_fb_helper_debug_leave - implementation for ->fb_debug_leave
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_debug_leave(struct fb_info *info)
{
	struct drm_fb_helper *helper = info->par;
	struct drm_crtc *crtc;
	struct drm_crtc_helper_funcs *funcs;
	struct drm_framebuffer *fb;
	int i;

	for (i = 0; i < helper->crtc_count; i++) {
		struct drm_mode_set *mode_set = &helper->crtc_info[i].mode_set;
		crtc = mode_set->crtc;
		funcs = crtc->helper_private;
		fb = drm_mode_config_fb(crtc);

		if (!crtc->enabled)
			continue;

		if (!fb) {
			DRM_ERROR("no fb to restore??\n");
			continue;
		}

		drm_fb_helper_restore_lut_atomic(mode_set->crtc);
		funcs->mode_set_base_atomic(mode_set->crtc, fb, crtc->x,
					    crtc->y, LEAVE_ATOMIC_MODE_SET);
	}

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_debug_leave);

static bool restore_fbdev_mode(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_plane *plane;
	bool error = false;
	int i;

	drm_warn_on_modeset_not_all_locked(dev);

	list_for_each_entry(plane, &dev->mode_config.plane_list, head)
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			drm_plane_force_disable(plane);

	for (i = 0; i < fb_helper->crtc_count; i++) {
		struct drm_mode_set *mode_set = &fb_helper->crtc_info[i].mode_set;
		struct drm_crtc *crtc = mode_set->crtc;
		int ret;

		if (crtc->funcs->cursor_set) {
			ret = crtc->funcs->cursor_set(crtc, NULL, 0, 0, 0);
			if (ret)
				error = true;
		}

		ret = drm_mode_set_config_internal(mode_set);
		if (ret)
			error = true;
	}
	return error;
}
/**
 * drm_fb_helper_restore_fbdev_mode - restore fbdev configuration
 * @fb_helper: fbcon to restore
 *
 * This should be called from driver's drm ->lastclose callback
 * when implementing an fbcon on top of kms using this helper. This ensures that
 * the user isn't greeted with a black screen when e.g. X dies.
 *
 * Use this variant if you need to bypass locking (panic), or already
 * hold all modeset locks.  Otherwise use drm_fb_helper_restore_fbdev_mode_unlocked()
 */
static bool drm_fb_helper_restore_fbdev_mode(struct drm_fb_helper *fb_helper)
{
	return restore_fbdev_mode(fb_helper);
}

/**
 * drm_fb_helper_restore_fbdev_mode_unlocked - restore fbdev configuration
 * @fb_helper: fbcon to restore
 *
 * This should be called from driver's drm ->lastclose callback
 * when implementing an fbcon on top of kms using this helper. This ensures that
 * the user isn't greeted with a black screen when e.g. X dies.
 */
bool drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	bool ret;
	drm_modeset_lock_all(dev);
	ret = restore_fbdev_mode(fb_helper);
	drm_modeset_unlock_all(dev);
	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_restore_fbdev_mode_unlocked);

/*
 * restore fbcon display for all kms driver's using this helper, used for sysrq
 * and panic handling.
 */
static bool drm_fb_helper_force_kernel_mode(void)
{
	bool ret, error = false;
	struct drm_fb_helper *helper;

	if (list_empty(&kernel_fb_helper_list))
		return false;

	list_for_each_entry(helper, &kernel_fb_helper_list, kernel_fb_list) {
		struct drm_device *dev = helper->dev;

		if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
			continue;

		/* NOTE: we use lockless flag below to avoid grabbing other
		 * modeset locks.  So just trylock the underlying mutex
		 * directly:
		 */
		if (!mutex_trylock(&dev->mode_config.mutex)) {
			error = true;
			continue;
		}

		ret = drm_fb_helper_restore_fbdev_mode(helper);
		if (ret)
			error = true;

		mutex_unlock(&dev->mode_config.mutex);
	}
	return error;
}

static int drm_fb_helper_panic(struct notifier_block *n, unsigned long ununsed,
			void *panic_str)
{
	/*
	 * It's a waste of time and effort to switch back to text console
	 * if the kernel should reboot before panic messages can be seen.
	 */
	if (panic_timeout < 0)
		return 0;

	pr_err("panic occurred, switching back to text console\n");
	return drm_fb_helper_force_kernel_mode();
}

static struct notifier_block paniced = {
	.notifier_call = drm_fb_helper_panic,
};

static bool drm_fb_helper_is_bound(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	int bound = 0, crtcs_bound = 0;

	/* Sometimes user space wants everything disabled, so don't steal the
	 * display if there's a master. */
	if (dev->primary->master)
		return false;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (crtc->primary->fb)
			crtcs_bound++;
		if (crtc->primary->fb == fb_helper->fb)
			bound++;
	}

	if (bound < crtcs_bound)
		return false;

	return true;
}

#ifdef CONFIG_MAGIC_SYSRQ
static void drm_fb_helper_restore_work_fn(struct work_struct *ignored)
{
	bool ret;
	ret = drm_fb_helper_force_kernel_mode();
	if (ret == true)
		DRM_ERROR("Failed to restore crtc configuration\n");
}
static DECLARE_WORK(drm_fb_helper_restore_work, drm_fb_helper_restore_work_fn);

static void drm_fb_helper_sysrq(int dummy1)
{
	schedule_work(&drm_fb_helper_restore_work);
}

static struct sysrq_key_op sysrq_drm_fb_helper_restore_op = {
	.handler = drm_fb_helper_sysrq,
	.help_msg = "force-fb(V)",
	.action_msg = "Restore framebuffer console",
};
#else
static struct sysrq_key_op sysrq_drm_fb_helper_restore_op = { };
#endif

static void drm_fb_helper_dpms(struct fb_info *info, int dpms_mode)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int i, j;

	/*
	 * fbdev->blank can be called from irq context in case of a panic.
	 * Since we already have our own special panic handler which will
	 * restore the fbdev console mode completely, just bail out early.
	 */
	if (oops_in_progress)
		return;

	/*
	 * For each CRTC in this fb, turn the connectors on/off.
	 */
	drm_modeset_lock_all(dev);
	if (!drm_fb_helper_is_bound(fb_helper)) {
		drm_modeset_unlock_all(dev);
		return;
	}

	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;

		if (!crtc->enabled)
			continue;

		/* Walk the connectors & encoders on this fb turning them on/off */
		for (j = 0; j < fb_helper->connector_count; j++) {
			connector = fb_helper->connector_info[j]->connector;
			connector->funcs->dpms(connector, dpms_mode);
			drm_object_property_set_value(&connector->base,
				dev->mode_config.dpms_property, dpms_mode);
		}
	}
	drm_modeset_unlock_all(dev);
}

/**
 * drm_fb_helper_blank - implementation for ->fb_blank
 * @blank: desired blanking state
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_blank(int blank, struct fb_info *info)
{
	switch (blank) {
	/* Display: On; HSync: On, VSync: On */
	case FB_BLANK_UNBLANK:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_ON);
		break;
	/* Display: Off; HSync: On, VSync: On */
	case FB_BLANK_NORMAL:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: Off, VSync: On */
	case FB_BLANK_HSYNC_SUSPEND:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: On, VSync: Off */
	case FB_BLANK_VSYNC_SUSPEND:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_SUSPEND);
		break;
	/* Display: Off; HSync: Off, VSync: Off */
	case FB_BLANK_POWERDOWN:
		drm_fb_helper_dpms(info, DRM_MODE_DPMS_OFF);
		break;
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_blank);

static void drm_fb_helper_crtc_free(struct drm_fb_helper *helper)
{
	int i;

	for (i = 0; i < helper->connector_count; i++)
		kfree(helper->connector_info[i]);
	kfree(helper->connector_info);
	for (i = 0; i < helper->crtc_count; i++) {
		kfree(helper->crtc_info[i].mode_set.connectors);
		if (helper->crtc_info[i].mode_set.mode)
			drm_mode_destroy(helper->dev, helper->crtc_info[i].mode_set.mode);
	}
	kfree(helper->crtc_info);
}

/**
 * drm_fb_helper_init - initialize a drm_fb_helper structure
 * @dev: drm device
 * @fb_helper: driver-allocated fbdev helper structure to initialize
 * @crtc_count: maximum number of crtcs to support in this fbdev emulation
 * @max_conn_count: max connector count
 *
 * This allocates the structures for the fbdev helper with the given limits.
 * Note that this won't yet touch the hardware (through the driver interfaces)
 * nor register the fbdev. This is only done in drm_fb_helper_initial_config()
 * to allow driver writes more control over the exact init sequence.
 *
 * Drivers must set fb_helper->funcs before calling
 * drm_fb_helper_initial_config().
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
int drm_fb_helper_init(struct drm_device *dev,
		       struct drm_fb_helper *fb_helper,
		       int crtc_count, int max_conn_count)
{
	struct drm_crtc *crtc;
	int i;

	if (!max_conn_count)
		return -EINVAL;

	fb_helper->dev = dev;

	INIT_LIST_HEAD(&fb_helper->kernel_fb_list);

	fb_helper->crtc_info = kcalloc(crtc_count, sizeof(struct drm_fb_helper_crtc), GFP_KERNEL);
	if (!fb_helper->crtc_info)
		return -ENOMEM;

	fb_helper->crtc_count = crtc_count;
	fb_helper->connector_info = kcalloc(dev->mode_config.num_connector, sizeof(struct drm_fb_helper_connector *), GFP_KERNEL);
	if (!fb_helper->connector_info) {
		kfree(fb_helper->crtc_info);
		return -ENOMEM;
	}
	fb_helper->connector_count = 0;

	for (i = 0; i < crtc_count; i++) {
		fb_helper->crtc_info[i].mode_set.connectors =
			kcalloc(max_conn_count,
				sizeof(struct drm_connector *),
				GFP_KERNEL);

		if (!fb_helper->crtc_info[i].mode_set.connectors)
			goto out_free;
		fb_helper->crtc_info[i].mode_set.num_connectors = 0;
	}

	i = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		fb_helper->crtc_info[i].mode_set.crtc = crtc;
		i++;
	}

	return 0;
out_free:
	drm_fb_helper_crtc_free(fb_helper);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_fb_helper_init);

void drm_fb_helper_fini(struct drm_fb_helper *fb_helper)
{
	if (!list_empty(&fb_helper->kernel_fb_list)) {
		list_del(&fb_helper->kernel_fb_list);
		if (list_empty(&kernel_fb_helper_list)) {
			pr_info("drm: unregistered panic notifier\n");
			atomic_notifier_chain_unregister(&panic_notifier_list,
							 &paniced);
			unregister_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
		}
	}

	drm_fb_helper_crtc_free(fb_helper);

}
EXPORT_SYMBOL(drm_fb_helper_fini);

static int setcolreg(struct drm_crtc *crtc, u16 red, u16 green,
		     u16 blue, u16 regno, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	int pindex;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 *palette;
		u32 value;
		/* place color in psuedopalette */
		if (regno > 16)
			return -EINVAL;
		palette = (u32 *)info->pseudo_palette;
		red >>= (16 - info->var.red.length);
		green >>= (16 - info->var.green.length);
		blue >>= (16 - info->var.blue.length);
		value = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset);
		if (info->var.transp.length > 0) {
			u32 mask = (1 << info->var.transp.length) - 1;
			mask <<= info->var.transp.offset;
			value |= mask;
		}
		palette[regno] = value;
		return 0;
	}

	/*
	 * The driver really shouldn't advertise pseudo/directcolor
	 * visuals if it can't deal with the palette.
	 */
	if (WARN_ON(!fb_helper->funcs->gamma_set ||
		    !fb_helper->funcs->gamma_get))
		return -EINVAL;

	pindex = regno;

	if (fb->bits_per_pixel == 16) {
		pindex = regno << 3;

		if (fb->depth == 16 && regno > 63)
			return -EINVAL;
		if (fb->depth == 15 && regno > 31)
			return -EINVAL;

		if (fb->depth == 16) {
			u16 r, g, b;
			int i;
			if (regno < 32) {
				for (i = 0; i < 8; i++)
					fb_helper->funcs->gamma_set(crtc, red,
						green, blue, pindex + i);
			}

			fb_helper->funcs->gamma_get(crtc, &r,
						    &g, &b,
						    pindex >> 1);

			for (i = 0; i < 4; i++)
				fb_helper->funcs->gamma_set(crtc, r,
							    green, b,
							    (pindex >> 1) + i);
		}
	}

	if (fb->depth != 16)
		fb_helper->funcs->gamma_set(crtc, red, green, blue, pindex);
	return 0;
}

/**
 * drm_fb_helper_setcmap - implementation for ->fb_setcmap
 * @cmap: cmap to set
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc_helper_funcs *crtc_funcs;
	u16 *red, *green, *blue, *transp;
	struct drm_crtc *crtc;
	int i, j, rc = 0;
	int start;

	drm_modeset_lock_all(dev);
	if (!drm_fb_helper_is_bound(fb_helper)) {
		drm_modeset_unlock_all(dev);
		return -EBUSY;
	}

	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;
		crtc_funcs = crtc->helper_private;

		red = cmap->red;
		green = cmap->green;
		blue = cmap->blue;
		transp = cmap->transp;
		start = cmap->start;

		for (j = 0; j < cmap->len; j++) {
			u16 hred, hgreen, hblue, htransp = 0xffff;

			hred = *red++;
			hgreen = *green++;
			hblue = *blue++;

			if (transp)
				htransp = *transp++;

			rc = setcolreg(crtc, hred, hgreen, hblue, start++, info);
			if (rc)
				goto out;
		}
		if (crtc_funcs->load_lut)
			crtc_funcs->load_lut(crtc);
	}
 out:
	drm_modeset_unlock_all(dev);
	return rc;
}
EXPORT_SYMBOL(drm_fb_helper_setcmap);

/**
 * drm_fb_helper_check_var - implementation for ->fb_check_var
 * @var: screeninfo to check
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	int depth;

	if (var->pixclock != 0 || in_dbg_master())
		return -EINVAL;

	/* Need to resize the fb object !!! */
	if (var->bits_per_pixel > fb->bits_per_pixel ||
	    var->xres > fb->width || var->yres > fb->height ||
	    var->xres_virtual > fb->width || var->yres_virtual > fb->height) {
		DRM_DEBUG("fb userspace requested width/height/bpp is greater than current fb "
			  "request %dx%d-%d (virtual %dx%d) > %dx%d-%d\n",
			  var->xres, var->yres, var->bits_per_pixel,
			  var->xres_virtual, var->yres_virtual,
			  fb->width, fb->height, fb->bits_per_pixel);
		return -EINVAL;
	}

	switch (var->bits_per_pixel) {
	case 16:
		depth = (var->green.length == 6) ? 16 : 15;
		break;
	case 32:
		depth = (var->transp.length > 0) ? 32 : 24;
		break;
	default:
		depth = var->bits_per_pixel;
		break;
	}

	switch (depth) {
	case 8:
		var->red.offset = 0;
		var->green.offset = 0;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 15:
		var->red.offset = 10;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 5;
		var->blue.length = 5;
		var->transp.length = 1;
		var->transp.offset = 15;
		break;
	case 16:
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 24:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 32:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 8;
		var->transp.offset = 24;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_check_var);

/**
 * drm_fb_helper_set_par - implementation for ->fb_set_par
 * @info: fbdev registered by the helper
 *
 * This will let fbcon do the mode init and is called at initialization time by
 * the fbdev core when registering the driver, and later on through the hotplug
 * callback.
 */
int drm_fb_helper_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct fb_var_screeninfo *var = &info->var;

	if (var->pixclock != 0) {
		DRM_ERROR("PIXEL CLOCK SET\n");
		return -EINVAL;
	}

	drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);

	if (fb_helper->delayed_hotplug) {
		fb_helper->delayed_hotplug = false;
		drm_fb_helper_hotplug_event(fb_helper);
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_set_par);

/**
 * drm_fb_helper_pan_display - implementation for ->fb_pan_display
 * @var: updated screen information
 * @info: fbdev registered by the helper
 */
int drm_fb_helper_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_mode_set *modeset;
	int ret = 0;
	int i;

	drm_modeset_lock_all(dev);
	if (!drm_fb_helper_is_bound(fb_helper)) {
		drm_modeset_unlock_all(dev);
		return -EBUSY;
	}

	for (i = 0; i < fb_helper->crtc_count; i++) {
		modeset = &fb_helper->crtc_info[i].mode_set;

		modeset->x = var->xoffset;
		modeset->y = var->yoffset;

		if (modeset->num_connectors) {
			ret = drm_mode_set_config_internal(modeset);
			if (!ret) {
				info->var.xoffset = var->xoffset;
				info->var.yoffset = var->yoffset;
			}
		}
	}
	drm_modeset_unlock_all(dev);
	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_pan_display);

/*
 * Allocates the backing storage and sets up the fbdev info structure through
 * the ->fb_probe callback and then registers the fbdev and sets up the panic
 * notifier.
 */
static int drm_fb_helper_single_fb_probe(struct drm_fb_helper *fb_helper,
					 int preferred_bpp)
{
	int ret = 0;
	int crtc_count = 0;
	int i;
	struct fb_info *info;
	struct drm_fb_helper_surface_size sizes;
	int gamma_size = 0;

	memset(&sizes, 0, sizeof(struct drm_fb_helper_surface_size));
	sizes.surface_depth = 24;
	sizes.surface_bpp = 32;
	sizes.fb_width = (unsigned)-1;
	sizes.fb_height = (unsigned)-1;

	/* if driver picks 8 or 16 by default use that
	   for both depth/bpp */
	if (preferred_bpp != sizes.surface_bpp)
		sizes.surface_depth = sizes.surface_bpp = preferred_bpp;

	/* first up get a count of crtcs now in use and new min/maxes width/heights */
	for (i = 0; i < fb_helper->connector_count; i++) {
		struct drm_fb_helper_connector *fb_helper_conn = fb_helper->connector_info[i];
		struct drm_cmdline_mode *cmdline_mode;

		cmdline_mode = &fb_helper_conn->cmdline_mode;

		if (cmdline_mode->bpp_specified) {
			switch (cmdline_mode->bpp) {
			case 8:
				sizes.surface_depth = sizes.surface_bpp = 8;
				break;
			case 15:
				sizes.surface_depth = 15;
				sizes.surface_bpp = 16;
				break;
			case 16:
				sizes.surface_depth = sizes.surface_bpp = 16;
				break;
			case 24:
				sizes.surface_depth = sizes.surface_bpp = 24;
				break;
			case 32:
				sizes.surface_depth = 24;
				sizes.surface_bpp = 32;
				break;
			}
			break;
		}
	}

	crtc_count = 0;
	for (i = 0; i < fb_helper->crtc_count; i++) {
		struct drm_display_mode *desired_mode;
		desired_mode = fb_helper->crtc_info[i].desired_mode;

		if (desired_mode) {
			if (gamma_size == 0)
				gamma_size = fb_helper->crtc_info[i].mode_set.crtc->gamma_size;
			if (desired_mode->hdisplay < sizes.fb_width)
				sizes.fb_width = desired_mode->hdisplay;
			if (desired_mode->vdisplay < sizes.fb_height)
				sizes.fb_height = desired_mode->vdisplay;
			if (desired_mode->hdisplay > sizes.surface_width)
				sizes.surface_width = desired_mode->hdisplay;
			if (desired_mode->vdisplay > sizes.surface_height)
				sizes.surface_height = desired_mode->vdisplay;
			crtc_count++;
		}
	}

	if (crtc_count == 0 || sizes.fb_width == -1 || sizes.fb_height == -1) {
		/* hmm everyone went away - assume VGA cable just fell out
		   and will come back later. */
		DRM_INFO("Cannot find any crtc or sizes - going 1024x768\n");
		sizes.fb_width = sizes.surface_width = 1024;
		sizes.fb_height = sizes.surface_height = 768;
	}

	/* push down into drivers */
	ret = (*fb_helper->funcs->fb_probe)(fb_helper, &sizes);
	if (ret < 0)
		return ret;

	info = fb_helper->fbdev;

	/*
	 * Set the fb pointer - usually drm_setup_crtcs does this for hotplug
	 * events, but at init time drm_setup_crtcs needs to be called before
	 * the fb is allocated (since we need to figure out the desired size of
	 * the fb before we can allocate it ...). Hence we need to fix things up
	 * here again.
	 */
	for (i = 0; i < fb_helper->crtc_count; i++)
		if (fb_helper->crtc_info[i].mode_set.num_connectors)
			fb_helper->crtc_info[i].mode_set.fb = fb_helper->fb;


	info->var.pixclock = 0;
	if (register_framebuffer(info) < 0)
		return -EINVAL;

	dev_info(fb_helper->dev->dev, "fb%d: %s frame buffer device\n",
			info->node, info->fix.id);

	/* Switch back to kernel console on panic */
	/* multi card linked list maybe */
	if (list_empty(&kernel_fb_helper_list)) {
		dev_info(fb_helper->dev->dev, "registered panic notifier\n");
		atomic_notifier_chain_register(&panic_notifier_list,
					       &paniced);
		register_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
	}

	list_add(&fb_helper->kernel_fb_list, &kernel_fb_helper_list);

	return 0;
}

/**
 * drm_fb_helper_fill_fix - initializes fixed fbdev information
 * @info: fbdev registered by the helper
 * @pitch: desired pitch
 * @depth: desired depth
 *
 * Helper to fill in the fixed fbdev information useful for a non-accelerated
 * fbdev emulations. Drivers which support acceleration methods which impose
 * additional constraints need to set up their own limits.
 *
 * Drivers should call this (or their equivalent setup code) from their
 * ->fb_probe callback.
 */
void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
			    uint32_t depth)
{
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = depth == 8 ? FB_VISUAL_PSEUDOCOLOR :
		FB_VISUAL_TRUECOLOR;
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 1; /* doing it in hw */
	info->fix.ypanstep = 1; /* doing it in hw */
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.type_aux = 0;

	info->fix.line_length = pitch;
	return;
}
EXPORT_SYMBOL(drm_fb_helper_fill_fix);

/**
 * drm_fb_helper_fill_var - initalizes variable fbdev information
 * @info: fbdev instance to set up
 * @fb_helper: fb helper instance to use as template
 * @fb_width: desired fb width
 * @fb_height: desired fb height
 *
 * Sets up the variable fbdev metainformation from the given fb helper instance
 * and the drm framebuffer allocated in fb_helper->fb.
 *
 * Drivers should call this (or their equivalent setup code) from their
 * ->fb_probe callback after having allocated the fbdev backing
 * storage framebuffer.
 */
void drm_fb_helper_fill_var(struct fb_info *info, struct drm_fb_helper *fb_helper,
			    uint32_t fb_width, uint32_t fb_height)
{
	struct drm_framebuffer *fb = fb_helper->fb;
	info->pseudo_palette = fb_helper->pseudo_palette;
	info->var.xres_virtual = fb->width;
	info->var.yres_virtual = fb->height;
	info->var.bits_per_pixel = fb->bits_per_pixel;
	info->var.accel_flags = FB_ACCELF_TEXT;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;

	switch (fb->depth) {
	case 8:
		info->var.red.offset = 0;
		info->var.green.offset = 0;
		info->var.blue.offset = 0;
		info->var.red.length = 8; /* 8bit DAC */
		info->var.green.length = 8;
		info->var.blue.length = 8;
		info->var.transp.offset = 0;
		info->var.transp.length = 0;
		break;
	case 15:
		info->var.red.offset = 10;
		info->var.green.offset = 5;
		info->var.blue.offset = 0;
		info->var.red.length = 5;
		info->var.green.length = 5;
		info->var.blue.length = 5;
		info->var.transp.offset = 15;
		info->var.transp.length = 1;
		break;
	case 16:
		info->var.red.offset = 11;
		info->var.green.offset = 5;
		info->var.blue.offset = 0;
		info->var.red.length = 5;
		info->var.green.length = 6;
		info->var.blue.length = 5;
		info->var.transp.offset = 0;
		break;
	case 24:
		info->var.red.offset = 16;
		info->var.green.offset = 8;
		info->var.blue.offset = 0;
		info->var.red.length = 8;
		info->var.green.length = 8;
		info->var.blue.length = 8;
		info->var.transp.offset = 0;
		info->var.transp.length = 0;
		break;
	case 32:
		info->var.red.offset = 16;
		info->var.green.offset = 8;
		info->var.blue.offset = 0;
		info->var.red.length = 8;
		info->var.green.length = 8;
		info->var.blue.length = 8;
		info->var.transp.offset = 24;
		info->var.transp.length = 8;
		break;
	default:
		break;
	}

	info->var.xres = fb_width;
	info->var.yres = fb_height;
}
EXPORT_SYMBOL(drm_fb_helper_fill_var);

static int drm_fb_helper_probe_connector_modes(struct drm_fb_helper *fb_helper,
					       uint32_t maxX,
					       uint32_t maxY)
{
	struct drm_connector *connector;
	int count = 0;
	int i;

	for (i = 0; i < fb_helper->connector_count; i++) {
		connector = fb_helper->connector_info[i]->connector;
		count += connector->funcs->fill_modes(connector, maxX, maxY);
	}

	return count;
}

struct drm_display_mode *drm_has_preferred_mode(struct drm_fb_helper_connector *fb_connector, int width, int height)
{
	struct drm_display_mode *mode;

	list_for_each_entry(mode, &fb_connector->connector->modes, head) {
		if (mode->hdisplay > width ||
		    mode->vdisplay > height)
			continue;
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return mode;
	}
	return NULL;
}
EXPORT_SYMBOL(drm_has_preferred_mode);

static bool drm_has_cmdline_mode(struct drm_fb_helper_connector *fb_connector)
{
	struct drm_cmdline_mode *cmdline_mode;
	cmdline_mode = &fb_connector->cmdline_mode;
	return cmdline_mode->specified;
}

struct drm_display_mode *drm_pick_cmdline_mode(struct drm_fb_helper_connector *fb_helper_conn,
						      int width, int height)
{
	struct drm_cmdline_mode *cmdline_mode;
	struct drm_display_mode *mode = NULL;
	bool prefer_non_interlace;

	cmdline_mode = &fb_helper_conn->cmdline_mode;
	if (cmdline_mode->specified == false)
		return mode;

	/* attempt to find a matching mode in the list of modes
	 *  we have gotten so far, if not add a CVT mode that conforms
	 */
	if (cmdline_mode->rb || cmdline_mode->margins)
		goto create_mode;

	prefer_non_interlace = !cmdline_mode->interlace;
 again:
	list_for_each_entry(mode, &fb_helper_conn->connector->modes, head) {
		/* check width/height */
		if (mode->hdisplay != cmdline_mode->xres ||
		    mode->vdisplay != cmdline_mode->yres)
			continue;

		if (cmdline_mode->refresh_specified) {
			if (mode->vrefresh != cmdline_mode->refresh)
				continue;
		}

		if (cmdline_mode->interlace) {
			if (!(mode->flags & DRM_MODE_FLAG_INTERLACE))
				continue;
		} else if (prefer_non_interlace) {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				continue;
		}
		return mode;
	}

	if (prefer_non_interlace) {
		prefer_non_interlace = false;
		goto again;
	}

create_mode:
	mode = drm_mode_create_from_cmdline_mode(fb_helper_conn->connector->dev,
						 cmdline_mode);
	list_add(&mode->head, &fb_helper_conn->connector->modes);
	return mode;
}
EXPORT_SYMBOL(drm_pick_cmdline_mode);

static bool drm_connector_enabled(struct drm_connector *connector, bool strict)
{
	bool enable;

	if (strict)
		enable = connector->status == connector_status_connected;
	else
		enable = connector->status != connector_status_disconnected;

	return enable;
}

static void drm_enable_connectors(struct drm_fb_helper *fb_helper,
				  bool *enabled)
{
	bool any_enabled = false;
	struct drm_connector *connector;
	int i = 0;

	for (i = 0; i < fb_helper->connector_count; i++) {
		connector = fb_helper->connector_info[i]->connector;
		enabled[i] = drm_connector_enabled(connector, true);
		DRM_DEBUG_KMS("connector %d enabled? %s\n", connector->base.id,
			  enabled[i] ? "yes" : "no");
		any_enabled |= enabled[i];
	}

	if (any_enabled)
		return;

	for (i = 0; i < fb_helper->connector_count; i++) {
		connector = fb_helper->connector_info[i]->connector;
		enabled[i] = drm_connector_enabled(connector, false);
	}
}

static bool drm_target_cloned(struct drm_fb_helper *fb_helper,
			      struct drm_display_mode **modes,
			      bool *enabled, int width, int height)
{
	int count, i, j;
	bool can_clone = false;
	struct drm_fb_helper_connector *fb_helper_conn;
	struct drm_display_mode *dmt_mode, *mode;

	/* only contemplate cloning in the single crtc case */
	if (fb_helper->crtc_count > 1)
		return false;

	count = 0;
	for (i = 0; i < fb_helper->connector_count; i++) {
		if (enabled[i])
			count++;
	}

	/* only contemplate cloning if more than one connector is enabled */
	if (count <= 1)
		return false;

	/* check the command line or if nothing common pick 1024x768 */
	can_clone = true;
	for (i = 0; i < fb_helper->connector_count; i++) {
		if (!enabled[i])
			continue;
		fb_helper_conn = fb_helper->connector_info[i];
		modes[i] = drm_pick_cmdline_mode(fb_helper_conn, width, height);
		if (!modes[i]) {
			can_clone = false;
			break;
		}
		for (j = 0; j < i; j++) {
			if (!enabled[j])
				continue;
			if (!drm_mode_equal(modes[j], modes[i]))
				can_clone = false;
		}
	}

	if (can_clone) {
		DRM_DEBUG_KMS("can clone using command line\n");
		return true;
	}

	/* try and find a 1024x768 mode on each connector */
	can_clone = true;
	dmt_mode = drm_mode_find_dmt(fb_helper->dev, 1024, 768, 60, false);

	for (i = 0; i < fb_helper->connector_count; i++) {

		if (!enabled[i])
			continue;

		fb_helper_conn = fb_helper->connector_info[i];
		list_for_each_entry(mode, &fb_helper_conn->connector->modes, head) {
			if (drm_mode_equal(mode, dmt_mode))
				modes[i] = mode;
		}
		if (!modes[i])
			can_clone = false;
	}

	if (can_clone) {
		DRM_DEBUG_KMS("can clone using 1024x768\n");
		return true;
	}
	DRM_INFO("kms: can't enable cloning when we probably wanted to.\n");
	return false;
}

static bool drm_target_preferred(struct drm_fb_helper *fb_helper,
				 struct drm_display_mode **modes,
				 bool *enabled, int width, int height)
{
	struct drm_fb_helper_connector *fb_helper_conn;
	int i;

	for (i = 0; i < fb_helper->connector_count; i++) {
		fb_helper_conn = fb_helper->connector_info[i];

		if (enabled[i] == false)
			continue;

		DRM_DEBUG_KMS("looking for cmdline mode on connector %d\n",
			      fb_helper_conn->connector->base.id);

		/* got for command line mode first */
		modes[i] = drm_pick_cmdline_mode(fb_helper_conn, width, height);
		if (!modes[i]) {
			DRM_DEBUG_KMS("looking for preferred mode on connector %d\n",
				      fb_helper_conn->connector->base.id);
			modes[i] = drm_has_preferred_mode(fb_helper_conn, width, height);
		}
		/* No preferred modes, pick one off the list */
		if (!modes[i] && !list_empty(&fb_helper_conn->connector->modes)) {
			list_for_each_entry(modes[i], &fb_helper_conn->connector->modes, head)
				break;
		}
		DRM_DEBUG_KMS("found mode %s\n", modes[i] ? modes[i]->name :
			  "none");
	}
	return true;
}

static int drm_pick_crtcs(struct drm_fb_helper *fb_helper,
			  struct drm_fb_helper_crtc **best_crtcs,
			  struct drm_display_mode **modes,
			  int n, int width, int height)
{
	int c, o;
	struct drm_device *dev = fb_helper->dev;
	struct drm_connector *connector;
	struct drm_connector_helper_funcs *connector_funcs;
	struct drm_encoder *encoder;
	int my_score, best_score, score;
	struct drm_fb_helper_crtc **crtcs, *crtc;
	struct drm_fb_helper_connector *fb_helper_conn;

	if (n == fb_helper->connector_count)
		return 0;

	fb_helper_conn = fb_helper->connector_info[n];
	connector = fb_helper_conn->connector;

	best_crtcs[n] = NULL;
	best_score = drm_pick_crtcs(fb_helper, best_crtcs, modes, n+1, width, height);
	if (modes[n] == NULL)
		return best_score;

	crtcs = kzalloc(dev->mode_config.num_connector *
			sizeof(struct drm_fb_helper_crtc *), GFP_KERNEL);
	if (!crtcs)
		return best_score;

	my_score = 1;
	if (connector->status == connector_status_connected)
		my_score++;
	if (drm_has_cmdline_mode(fb_helper_conn))
		my_score++;
	if (drm_has_preferred_mode(fb_helper_conn, width, height))
		my_score++;

	connector_funcs = connector->helper_private;
	encoder = connector_funcs->best_encoder(connector);
	if (!encoder)
		goto out;

	/* select a crtc for this connector and then attempt to configure
	   remaining connectors */
	for (c = 0; c < fb_helper->crtc_count; c++) {
		crtc = &fb_helper->crtc_info[c];

		if ((encoder->possible_crtcs & (1 << c)) == 0)
			continue;

		for (o = 0; o < n; o++)
			if (best_crtcs[o] == crtc)
				break;

		if (o < n) {
			/* ignore cloning unless only a single crtc */
			if (fb_helper->crtc_count > 1)
				continue;

			if (!drm_mode_equal(modes[o], modes[n]))
				continue;
		}

		crtcs[n] = crtc;
		memcpy(crtcs, best_crtcs, n * sizeof(struct drm_fb_helper_crtc *));
		score = my_score + drm_pick_crtcs(fb_helper, crtcs, modes, n + 1,
						  width, height);
		if (score > best_score) {
			best_score = score;
			memcpy(best_crtcs, crtcs,
			       dev->mode_config.num_connector *
			       sizeof(struct drm_fb_helper_crtc *));
		}
	}
out:
	kfree(crtcs);
	return best_score;
}

static void drm_setup_crtcs(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_fb_helper_crtc **crtcs;
	struct drm_display_mode **modes;
	struct drm_mode_set *modeset;
	bool *enabled;
	int width, height;
	int i;

	DRM_DEBUG_KMS("\n");

	width = dev->mode_config.max_width;
	height = dev->mode_config.max_height;

	crtcs = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_fb_helper_crtc *), GFP_KERNEL);
	modes = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_display_mode *), GFP_KERNEL);
	enabled = kcalloc(dev->mode_config.num_connector,
			  sizeof(bool), GFP_KERNEL);
	if (!crtcs || !modes || !enabled) {
		DRM_ERROR("Memory allocation failed\n");
		goto out;
	}


	drm_enable_connectors(fb_helper, enabled);

	if (!(fb_helper->funcs->initial_config &&
	      fb_helper->funcs->initial_config(fb_helper, crtcs, modes,
					       enabled, width, height))) {
		memset(modes, 0, dev->mode_config.num_connector*sizeof(modes[0]));
		memset(crtcs, 0, dev->mode_config.num_connector*sizeof(crtcs[0]));

		if (!drm_target_cloned(fb_helper,
				       modes, enabled, width, height) &&
		    !drm_target_preferred(fb_helper,
					  modes, enabled, width, height))
			DRM_ERROR("Unable to find initial modes\n");

		DRM_DEBUG_KMS("picking CRTCs for %dx%d config\n",
			      width, height);

		drm_pick_crtcs(fb_helper, crtcs, modes, 0, width, height);
	}

	/* need to set the modesets up here for use later */
	/* fill out the connector<->crtc mappings into the modesets */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		modeset = &fb_helper->crtc_info[i].mode_set;
		modeset->num_connectors = 0;
		modeset->fb = NULL;
	}

	for (i = 0; i < fb_helper->connector_count; i++) {
		struct drm_display_mode *mode = modes[i];
		struct drm_fb_helper_crtc *fb_crtc = crtcs[i];
		modeset = &fb_crtc->mode_set;

		if (mode && fb_crtc) {
			DRM_DEBUG_KMS("desired mode %s set on crtc %d\n",
				      mode->name, fb_crtc->mode_set.crtc->base.id);
			fb_crtc->desired_mode = mode;
			if (modeset->mode)
				drm_mode_destroy(dev, modeset->mode);
			modeset->mode = drm_mode_duplicate(dev,
							   fb_crtc->desired_mode);
			modeset->connectors[modeset->num_connectors++] = fb_helper->connector_info[i]->connector;
			modeset->fb = fb_helper->fb;
		}
	}

	/* Clear out any old modes if there are no more connected outputs. */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		modeset = &fb_helper->crtc_info[i].mode_set;
		if (modeset->num_connectors == 0) {
			BUG_ON(modeset->fb);
			BUG_ON(modeset->num_connectors);
			if (modeset->mode)
				drm_mode_destroy(dev, modeset->mode);
			modeset->mode = NULL;
		}
	}
out:
	kfree(crtcs);
	kfree(modes);
	kfree(enabled);
}

/**
 * drm_fb_helper_initial_config - setup a sane initial connector configuration
 * @fb_helper: fb_helper device struct
 * @bpp_sel: bpp value to use for the framebuffer configuration
 *
 * Scans the CRTCs and connectors and tries to put together an initial setup.
 * At the moment, this is a cloned configuration across all heads with
 * a new framebuffer object as the backing store.
 *
 * Note that this also registers the fbdev and so allows userspace to call into
 * the driver through the fbdev interfaces.
 *
 * This function will call down into the ->fb_probe callback to let
 * the driver allocate and initialize the fbdev info structure and the drm
 * framebuffer used to back the fbdev. drm_fb_helper_fill_var() and
 * drm_fb_helper_fill_fix() are provided as helpers to setup simple default
 * values for the fbdev info structure.
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
bool drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel)
{
	struct drm_device *dev = fb_helper->dev;
	int count = 0;

	drm_fb_helper_parse_command_line(fb_helper);

	mutex_lock(&dev->mode_config.mutex);
	count = drm_fb_helper_probe_connector_modes(fb_helper,
						    dev->mode_config.max_width,
						    dev->mode_config.max_height);
	mutex_unlock(&dev->mode_config.mutex);
	/*
	 * we shouldn't end up with no modes here.
	 */
	if (count == 0)
		dev_info(fb_helper->dev->dev, "No connectors reported connected with modes\n");

	drm_setup_crtcs(fb_helper);

	return drm_fb_helper_single_fb_probe(fb_helper, bpp_sel);
}
EXPORT_SYMBOL(drm_fb_helper_initial_config);

/**
 * drm_fb_helper_hotplug_event - respond to a hotplug notification by
 *                               probing all the outputs attached to the fb
 * @fb_helper: the drm_fb_helper
 *
 * Scan the connectors attached to the fb_helper and try to put together a
 * setup after *notification of a change in output configuration.
 *
 * Called at runtime, takes the mode config locks to be able to check/change the
 * modeset configuration. Must be run from process context (which usually means
 * either the output polling work or a work item launched from the driver's
 * hotplug interrupt).
 *
 * Note that the driver must ensure that this is only called _after_ the fb has
 * been fully set up, i.e. after the call to drm_fb_helper_initial_config.
 *
 * RETURNS:
 * 0 on success and a non-zero error code otherwise.
 */
int drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper)
{
	struct drm_device *dev = fb_helper->dev;
	u32 max_width, max_height;

	if (!fb_helper->fb)
		return 0;

	mutex_lock(&fb_helper->dev->mode_config.mutex);
	if (!drm_fb_helper_is_bound(fb_helper)) {
		fb_helper->delayed_hotplug = true;
		mutex_unlock(&fb_helper->dev->mode_config.mutex);
		return 0;
	}
	DRM_DEBUG_KMS("\n");

	max_width = fb_helper->fb->width;
	max_height = fb_helper->fb->height;

	drm_fb_helper_probe_connector_modes(fb_helper, max_width, max_height);
	mutex_unlock(&fb_helper->dev->mode_config.mutex);

	drm_modeset_lock_all(dev);
	drm_setup_crtcs(fb_helper);
	drm_modeset_unlock_all(dev);
	drm_fb_helper_set_par(fb_helper->fbdev);

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_hotplug_event);

/* The Kconfig DRM_KMS_HELPER selects FRAMEBUFFER_CONSOLE (if !EXPERT)
 * but the module doesn't depend on any fb console symbols.  At least
 * attempt to load fbcon to avoid leaving the system without a usable console.
 */
#if defined(CONFIG_FRAMEBUFFER_CONSOLE_MODULE) && !defined(CONFIG_EXPERT)
static int __init drm_fb_helper_modinit(void)
{
	const char *name = "fbcon";
	struct module *fbcon;

	mutex_lock(&module_mutex);
	fbcon = find_module(name);
	mutex_unlock(&module_mutex);

	if (!fbcon)
		request_module_nowait(name);
	return 0;
}

module_init(drm_fb_helper_modinit);
#endif
