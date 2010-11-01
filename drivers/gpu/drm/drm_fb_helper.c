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
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include "drmP.h"
#include "drm_crtc.h"
#include "drm_fb_helper.h"
#include "drm_crtc_helper.h"

MODULE_AUTHOR("David Airlie, Jesse Barnes");
MODULE_DESCRIPTION("DRM KMS helper");
MODULE_LICENSE("GPL and additional rights");

static LIST_HEAD(kernel_fb_helper_list);

/* simple single crtc case helper function */
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

/**
 * drm_fb_helper_connector_parse_command_line - parse command line for connector
 * @connector - connector to parse line for
 * @mode_option - per connector mode option
 *
 * This parses the connector specific then generic command lines for
 * modes and options to configure the connector.
 *
 * This uses the same parameters as the fb modedb.c, except for extra
 *	<xres>x<yres>[M][R][-<bpp>][@<refresh>][i][m][eDd]
 *
 * enable/enable Digital/disable bit at the end
 */
static bool drm_fb_helper_connector_parse_command_line(struct drm_fb_helper_connector *fb_helper_conn,
						       const char *mode_option)
{
	const char *name;
	unsigned int namelen;
	int res_specified = 0, bpp_specified = 0, refresh_specified = 0;
	unsigned int xres = 0, yres = 0, bpp = 32, refresh = 0;
	int yres_specified = 0, cvt = 0, rb = 0, interlace = 0, margins = 0;
	int i;
	enum drm_connector_force force = DRM_FORCE_UNSPECIFIED;
	struct drm_fb_helper_cmdline_mode *cmdline_mode;
	struct drm_connector *connector;

	if (!fb_helper_conn)
		return false;
	connector = fb_helper_conn->connector;

	cmdline_mode = &fb_helper_conn->cmdline_mode;
	if (!mode_option)
		mode_option = fb_mode_option;

	if (!mode_option) {
		cmdline_mode->specified = false;
		return false;
	}

	name = mode_option;
	namelen = strlen(name);
	for (i = namelen-1; i >= 0; i--) {
		switch (name[i]) {
		case '@':
			namelen = i;
			if (!refresh_specified && !bpp_specified &&
			    !yres_specified) {
				refresh = simple_strtol(&name[i+1], NULL, 10);
				refresh_specified = 1;
				if (cvt || rb)
					cvt = 0;
			} else
				goto done;
			break;
		case '-':
			namelen = i;
			if (!bpp_specified && !yres_specified) {
				bpp = simple_strtol(&name[i+1], NULL, 10);
				bpp_specified = 1;
				if (cvt || rb)
					cvt = 0;
			} else
				goto done;
			break;
		case 'x':
			if (!yres_specified) {
				yres = simple_strtol(&name[i+1], NULL, 10);
				yres_specified = 1;
			} else
				goto done;
		case '0' ... '9':
			break;
		case 'M':
			if (!yres_specified)
				cvt = 1;
			break;
		case 'R':
			if (cvt)
				rb = 1;
			break;
		case 'm':
			if (!cvt)
				margins = 1;
			break;
		case 'i':
			if (!cvt)
				interlace = 1;
			break;
		case 'e':
			force = DRM_FORCE_ON;
			break;
		case 'D':
			if ((connector->connector_type != DRM_MODE_CONNECTOR_DVII) &&
			    (connector->connector_type != DRM_MODE_CONNECTOR_HDMIB))
				force = DRM_FORCE_ON;
			else
				force = DRM_FORCE_ON_DIGITAL;
			break;
		case 'd':
			force = DRM_FORCE_OFF;
			break;
		default:
			goto done;
		}
	}
	if (i < 0 && yres_specified) {
		xres = simple_strtol(name, NULL, 10);
		res_specified = 1;
	}
done:

	DRM_DEBUG_KMS("cmdline mode for connector %s %dx%d@%dHz%s%s%s\n",
		drm_get_connector_name(connector), xres, yres,
		(refresh) ? refresh : 60, (rb) ? " reduced blanking" :
		"", (margins) ? " with margins" : "", (interlace) ?
		" interlaced" : "");

	if (force) {
		const char *s;
		switch (force) {
		case DRM_FORCE_OFF: s = "OFF"; break;
		case DRM_FORCE_ON_DIGITAL: s = "ON - dig"; break;
		default:
		case DRM_FORCE_ON: s = "ON"; break;
		}

		DRM_INFO("forcing %s connector %s\n",
			 drm_get_connector_name(connector), s);
		connector->force = force;
	}

	if (res_specified) {
		cmdline_mode->specified = true;
		cmdline_mode->xres = xres;
		cmdline_mode->yres = yres;
	}

	if (refresh_specified) {
		cmdline_mode->refresh_specified = true;
		cmdline_mode->refresh = refresh;
	}

	if (bpp_specified) {
		cmdline_mode->bpp_specified = true;
		cmdline_mode->bpp = bpp;
	}
	cmdline_mode->rb = rb ? true : false;
	cmdline_mode->cvt = cvt  ? true : false;
	cmdline_mode->interlace = interlace ? true : false;

	return true;
}

static int drm_fb_helper_parse_command_line(struct drm_fb_helper *fb_helper)
{
	struct drm_fb_helper_connector *fb_helper_conn;
	int i;

	for (i = 0; i < fb_helper->connector_count; i++) {
		char *option = NULL;

		fb_helper_conn = fb_helper->connector_info[i];

		/* do something on return - turn off connector maybe */
		if (fb_get_options(drm_get_connector_name(fb_helper_conn->connector), &option))
			continue;

		drm_fb_helper_connector_parse_command_line(fb_helper_conn, option);
	}
	return 0;
}

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
			funcs->mode_set_base_atomic(mode_set->crtc,
						    mode_set->fb,
						    mode_set->x,
						    mode_set->y);

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
			return c->fb;
	}

	return NULL;
}

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

		funcs->mode_set_base_atomic(mode_set->crtc, fb, crtc->x,
					    crtc->y);
	}

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_debug_leave);

bool drm_fb_helper_force_kernel_mode(void)
{
	int i = 0;
	bool ret, error = false;
	struct drm_fb_helper *helper;

	if (list_empty(&kernel_fb_helper_list))
		return false;

	list_for_each_entry(helper, &kernel_fb_helper_list, kernel_fb_list) {
		for (i = 0; i < helper->crtc_count; i++) {
			struct drm_mode_set *mode_set = &helper->crtc_info[i].mode_set;
			ret = drm_crtc_helper_set_config(mode_set);
			if (ret)
				error = true;
		}
	}
	return error;
}

int drm_fb_helper_panic(struct notifier_block *n, unsigned long ununsed,
			void *panic_str)
{
	printk(KERN_ERR "panic occurred, switching back to text console\n");
	return drm_fb_helper_force_kernel_mode();
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_panic);

static struct notifier_block paniced = {
	.notifier_call = drm_fb_helper_panic,
};

/**
 * drm_fb_helper_restore - restore the framebuffer console (kernel) config
 *
 * Restore's the kernel's fbcon mode, used for lastclose & panic paths.
 */
void drm_fb_helper_restore(void)
{
	bool ret;
	ret = drm_fb_helper_force_kernel_mode();
	if (ret == true)
		DRM_ERROR("Failed to restore crtc configuration\n");
}
EXPORT_SYMBOL(drm_fb_helper_restore);

#ifdef CONFIG_MAGIC_SYSRQ
static void drm_fb_helper_restore_work_fn(struct work_struct *ignored)
{
	drm_fb_helper_restore();
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

static void drm_fb_helper_on(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_helper_funcs *crtc_funcs;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int i, j;

	/*
	 * For each CRTC in this fb, turn the crtc on then,
	 * find all associated encoders and turn them on.
	 */
	mutex_lock(&dev->mode_config.mutex);
	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;
		crtc_funcs = crtc->helper_private;

		if (!crtc->enabled)
			continue;

		crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);

		/* Walk the connectors & encoders on this fb turning them on */
		for (j = 0; j < fb_helper->connector_count; j++) {
			connector = fb_helper->connector_info[j]->connector;
			connector->dpms = DRM_MODE_DPMS_ON;
			drm_connector_property_set_value(connector,
							 dev->mode_config.dpms_property,
							 DRM_MODE_DPMS_ON);
		}
		/* Found a CRTC on this fb, now find encoders */
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			if (encoder->crtc == crtc) {
				struct drm_encoder_helper_funcs *encoder_funcs;

				encoder_funcs = encoder->helper_private;
				encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
			}
		}
	}
	mutex_unlock(&dev->mode_config.mutex);
}

static void drm_fb_helper_off(struct fb_info *info, int dpms_mode)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_helper_funcs *crtc_funcs;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int i, j;

	/*
	 * For each CRTC in this fb, find all associated encoders
	 * and turn them off, then turn off the CRTC.
	 */
	mutex_lock(&dev->mode_config.mutex);
	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;
		crtc_funcs = crtc->helper_private;

		if (!crtc->enabled)
			continue;

		/* Walk the connectors on this fb and mark them off */
		for (j = 0; j < fb_helper->connector_count; j++) {
			connector = fb_helper->connector_info[j]->connector;
			connector->dpms = dpms_mode;
			drm_connector_property_set_value(connector,
							 dev->mode_config.dpms_property,
							 dpms_mode);
		}
		/* Found a CRTC on this fb, now find encoders */
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			if (encoder->crtc == crtc) {
				struct drm_encoder_helper_funcs *encoder_funcs;

				encoder_funcs = encoder->helper_private;
				encoder_funcs->dpms(encoder, dpms_mode);
			}
		}
		crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
	}
	mutex_unlock(&dev->mode_config.mutex);
}

int drm_fb_helper_blank(int blank, struct fb_info *info)
{
	switch (blank) {
	/* Display: On; HSync: On, VSync: On */
	case FB_BLANK_UNBLANK:
		drm_fb_helper_on(info);
		break;
	/* Display: Off; HSync: On, VSync: On */
	case FB_BLANK_NORMAL:
		drm_fb_helper_off(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: Off, VSync: On */
	case FB_BLANK_HSYNC_SUSPEND:
		drm_fb_helper_off(info, DRM_MODE_DPMS_STANDBY);
		break;
	/* Display: Off; HSync: On, VSync: Off */
	case FB_BLANK_VSYNC_SUSPEND:
		drm_fb_helper_off(info, DRM_MODE_DPMS_SUSPEND);
		break;
	/* Display: Off; HSync: Off, VSync: Off */
	case FB_BLANK_POWERDOWN:
		drm_fb_helper_off(info, DRM_MODE_DPMS_OFF);
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
	for (i = 0; i < helper->crtc_count; i++)
		kfree(helper->crtc_info[i].mode_set.connectors);
	kfree(helper->crtc_info);
}

int drm_fb_helper_init(struct drm_device *dev,
		       struct drm_fb_helper *fb_helper,
		       int crtc_count, int max_conn_count)
{
	struct drm_crtc *crtc;
	int ret = 0;
	int i;

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

		if (!fb_helper->crtc_info[i].mode_set.connectors) {
			ret = -ENOMEM;
			goto out_free;
		}
		fb_helper->crtc_info[i].mode_set.num_connectors = 0;
	}

	i = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		fb_helper->crtc_info[i].crtc_id = crtc->base.id;
		fb_helper->crtc_info[i].mode_set.crtc = crtc;
		i++;
	}
	fb_helper->conn_limit = max_conn_count;
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
			printk(KERN_INFO "drm: unregistered panic notifier\n");
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
		palette[regno] = value;
		return 0;
	}

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

int drm_fb_helper_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_crtc_helper_funcs *crtc_funcs;
	u16 *red, *green, *blue, *transp;
	struct drm_crtc *crtc;
	int i, rc = 0;
	int start;

	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;
		crtc_funcs = crtc->helper_private;

		red = cmap->red;
		green = cmap->green;
		blue = cmap->blue;
		transp = cmap->transp;
		start = cmap->start;

		for (i = 0; i < cmap->len; i++) {
			u16 hred, hgreen, hblue, htransp = 0xffff;

			hred = *red++;
			hgreen = *green++;
			hblue = *blue++;

			if (transp)
				htransp = *transp++;

			rc = setcolreg(crtc, hred, hgreen, hblue, start++, info);
			if (rc)
				return rc;
		}
		crtc_funcs->load_lut(crtc);
	}
	return rc;
}
EXPORT_SYMBOL(drm_fb_helper_setcmap);

int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	int depth;

	if (var->pixclock != 0 || in_dbg_master())
		return -EINVAL;

	/* Need to resize the fb object !!! */
	if (var->bits_per_pixel > fb->bits_per_pixel || var->xres > fb->width || var->yres > fb->height) {
		DRM_DEBUG("fb userspace requested width/height/bpp is greater than current fb "
			  "object %dx%d-%d > %dx%d-%d\n", var->xres, var->yres, var->bits_per_pixel,
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

/* this will let fbcon do the mode init */
int drm_fb_helper_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct fb_var_screeninfo *var = &info->var;
	struct drm_crtc *crtc;
	int ret;
	int i;

	if (var->pixclock != 0) {
		DRM_ERROR("PIXEL CLOCK SET\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mode_config.mutex);
	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;
		ret = crtc->funcs->set_config(&fb_helper->crtc_info[i].mode_set);
		if (ret) {
			mutex_unlock(&dev->mode_config.mutex);
			return ret;
		}
	}
	mutex_unlock(&dev->mode_config.mutex);

	if (fb_helper->delayed_hotplug) {
		fb_helper->delayed_hotplug = false;
		drm_fb_helper_hotplug_event(fb_helper);
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_set_par);

int drm_fb_helper_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_mode_set *modeset;
	struct drm_crtc *crtc;
	int ret = 0;
	int i;

	mutex_lock(&dev->mode_config.mutex);
	for (i = 0; i < fb_helper->crtc_count; i++) {
		crtc = fb_helper->crtc_info[i].mode_set.crtc;

		modeset = &fb_helper->crtc_info[i].mode_set;

		modeset->x = var->xoffset;
		modeset->y = var->yoffset;

		if (modeset->num_connectors) {
			ret = crtc->funcs->set_config(modeset);
			if (!ret) {
				info->var.xoffset = var->xoffset;
				info->var.yoffset = var->yoffset;
			}
		}
	}
	mutex_unlock(&dev->mode_config.mutex);
	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_pan_display);

int drm_fb_helper_single_fb_probe(struct drm_fb_helper *fb_helper,
				  int preferred_bpp)
{
	int new_fb = 0;
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
	if (preferred_bpp != sizes.surface_bpp) {
		sizes.surface_depth = sizes.surface_bpp = preferred_bpp;
	}
	/* first up get a count of crtcs now in use and new min/maxes width/heights */
	for (i = 0; i < fb_helper->connector_count; i++) {
		struct drm_fb_helper_connector *fb_helper_conn = fb_helper->connector_info[i];
		struct drm_fb_helper_cmdline_mode *cmdline_mode;

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
	new_fb = (*fb_helper->funcs->fb_probe)(fb_helper, &sizes);
	if (new_fb < 0)
		return new_fb;

	info = fb_helper->fbdev;

	/* set the fb pointer */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		fb_helper->crtc_info[i].mode_set.fb = fb_helper->fb;
	}

	if (new_fb) {
		info->var.pixclock = 0;
		if (register_framebuffer(info) < 0) {
			return -EINVAL;
		}

		printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
		       info->fix.id);

	} else {
		drm_fb_helper_set_par(info);
	}

	/* Switch back to kernel console on panic */
	/* multi card linked list maybe */
	if (list_empty(&kernel_fb_helper_list)) {
		printk(KERN_INFO "drm: registered panic notifier\n");
		atomic_notifier_chain_register(&panic_notifier_list,
					       &paniced);
		register_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
	}
	if (new_fb)
		list_add(&fb_helper->kernel_fb_list, &kernel_fb_helper_list);

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_single_fb_probe);

void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
			    uint32_t depth)
{
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = depth == 8 ? FB_VISUAL_PSEUDOCOLOR :
		FB_VISUAL_TRUECOLOR;
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

void drm_fb_helper_fill_var(struct fb_info *info, struct drm_fb_helper *fb_helper,
			    uint32_t fb_width, uint32_t fb_height)
{
	struct drm_framebuffer *fb = fb_helper->fb;
	info->pseudo_palette = fb_helper->pseudo_palette;
	info->var.xres_virtual = fb->width;
	info->var.yres_virtual = fb->height;
	info->var.bits_per_pixel = fb->bits_per_pixel;
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

static struct drm_display_mode *drm_has_preferred_mode(struct drm_fb_helper_connector *fb_connector, int width, int height)
{
	struct drm_display_mode *mode;

	list_for_each_entry(mode, &fb_connector->connector->modes, head) {
		if (drm_mode_width(mode) > width ||
		    drm_mode_height(mode) > height)
			continue;
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return mode;
	}
	return NULL;
}

static bool drm_has_cmdline_mode(struct drm_fb_helper_connector *fb_connector)
{
	struct drm_fb_helper_cmdline_mode *cmdline_mode;
	cmdline_mode = &fb_connector->cmdline_mode;
	return cmdline_mode->specified;
}

static struct drm_display_mode *drm_pick_cmdline_mode(struct drm_fb_helper_connector *fb_helper_conn,
						      int width, int height)
{
	struct drm_fb_helper_cmdline_mode *cmdline_mode;
	struct drm_display_mode *mode = NULL;

	cmdline_mode = &fb_helper_conn->cmdline_mode;
	if (cmdline_mode->specified == false)
		return mode;

	/* attempt to find a matching mode in the list of modes
	 *  we have gotten so far, if not add a CVT mode that conforms
	 */
	if (cmdline_mode->rb || cmdline_mode->margins)
		goto create_mode;

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
		}
		return mode;
	}

create_mode:
	if (cmdline_mode->cvt)
		mode = drm_cvt_mode(fb_helper_conn->connector->dev,
				    cmdline_mode->xres, cmdline_mode->yres,
				    cmdline_mode->refresh_specified ? cmdline_mode->refresh : 60,
				    cmdline_mode->rb, cmdline_mode->interlace,
				    cmdline_mode->margins);
	else
		mode = drm_gtf_mode(fb_helper_conn->connector->dev,
				    cmdline_mode->xres, cmdline_mode->yres,
				    cmdline_mode->refresh_specified ? cmdline_mode->refresh : 60,
				    cmdline_mode->interlace,
				    cmdline_mode->margins);
	drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
	list_add(&mode->head, &fb_helper_conn->connector->modes);
	return mode;
}

static bool drm_connector_enabled(struct drm_connector *connector, bool strict)
{
	bool enable;

	if (strict) {
		enable = connector->status == connector_status_connected;
	} else {
		enable = connector->status != connector_status_disconnected;
	}
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
	dmt_mode = drm_mode_find_dmt(fb_helper->dev, 1024, 768, 60);

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
	struct drm_fb_helper_crtc *best_crtc;
	int my_score, best_score, score;
	struct drm_fb_helper_crtc **crtcs, *crtc;
	struct drm_fb_helper_connector *fb_helper_conn;

	if (n == fb_helper->connector_count)
		return 0;

	fb_helper_conn = fb_helper->connector_info[n];
	connector = fb_helper_conn->connector;

	best_crtcs[n] = NULL;
	best_crtc = NULL;
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

		if ((encoder->possible_crtcs & (1 << c)) == 0) {
			continue;
		}

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
			best_crtc = crtc;
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
	struct drm_encoder *encoder;
	struct drm_mode_set *modeset;
	bool *enabled;
	int width, height;
	int i, ret;

	DRM_DEBUG_KMS("\n");

	width = dev->mode_config.max_width;
	height = dev->mode_config.max_height;

	/* clean out all the encoder/crtc combos */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder->crtc = NULL;
	}

	crtcs = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_fb_helper_crtc *), GFP_KERNEL);
	modes = kcalloc(dev->mode_config.num_connector,
			sizeof(struct drm_display_mode *), GFP_KERNEL);
	enabled = kcalloc(dev->mode_config.num_connector,
			  sizeof(bool), GFP_KERNEL);

	drm_enable_connectors(fb_helper, enabled);

	ret = drm_target_cloned(fb_helper, modes, enabled, width, height);
	if (!ret) {
		ret = drm_target_preferred(fb_helper, modes, enabled, width, height);
		if (!ret)
			DRM_ERROR("Unable to find initial modes\n");
	}

	DRM_DEBUG_KMS("picking CRTCs for %dx%d config\n", width, height);

	drm_pick_crtcs(fb_helper, crtcs, modes, 0, width, height);

	/* need to set the modesets up here for use later */
	/* fill out the connector<->crtc mappings into the modesets */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		modeset = &fb_helper->crtc_info[i].mode_set;
		modeset->num_connectors = 0;
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
		}
	}

	kfree(crtcs);
	kfree(modes);
	kfree(enabled);
}

/**
 * drm_helper_initial_config - setup a sane initial connector configuration
 * @dev: DRM device
 *
 * LOCKING:
 * Called at init time, must take mode config lock.
 *
 * Scan the CRTCs and connectors and try to put together an initial setup.
 * At the moment, this is a cloned configuration across all heads with
 * a new framebuffer object as the backing store.
 *
 * RETURNS:
 * Zero if everything went ok, nonzero otherwise.
 */
bool drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel)
{
	struct drm_device *dev = fb_helper->dev;
	int count = 0;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(fb_helper->dev);

	drm_fb_helper_parse_command_line(fb_helper);

	count = drm_fb_helper_probe_connector_modes(fb_helper,
						    dev->mode_config.max_width,
						    dev->mode_config.max_height);
	/*
	 * we shouldn't end up with no modes here.
	 */
	if (count == 0) {
		printk(KERN_INFO "No connectors reported connected with modes\n");
	}
	drm_setup_crtcs(fb_helper);

	return drm_fb_helper_single_fb_probe(fb_helper, bpp_sel);
}
EXPORT_SYMBOL(drm_fb_helper_initial_config);

bool drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper)
{
	int count = 0;
	u32 max_width, max_height, bpp_sel;
	bool bound = false, crtcs_bound = false;
	struct drm_crtc *crtc;

	if (!fb_helper->fb)
		return false;

	list_for_each_entry(crtc, &fb_helper->dev->mode_config.crtc_list, head) {
		if (crtc->fb)
			crtcs_bound = true;
		if (crtc->fb == fb_helper->fb)
			bound = true;
	}

	if (!bound && crtcs_bound) {
		fb_helper->delayed_hotplug = true;
		return false;
	}
	DRM_DEBUG_KMS("\n");

	max_width = fb_helper->fb->width;
	max_height = fb_helper->fb->height;
	bpp_sel = fb_helper->fb->bits_per_pixel;

	count = drm_fb_helper_probe_connector_modes(fb_helper, max_width,
						    max_height);
	drm_setup_crtcs(fb_helper);

	return drm_fb_helper_single_fb_probe(fb_helper, bpp_sel);
}
EXPORT_SYMBOL(drm_fb_helper_hotplug_event);

