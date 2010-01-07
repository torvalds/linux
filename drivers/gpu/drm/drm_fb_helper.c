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
#include <linux/sysrq.h>
#include <linux/fb.h>
#include "drmP.h"
#include "drm_crtc.h"
#include "drm_fb_helper.h"
#include "drm_crtc_helper.h"

MODULE_AUTHOR("David Airlie, Jesse Barnes");
MODULE_DESCRIPTION("DRM KMS helper");
MODULE_LICENSE("GPL and additional rights");

static LIST_HEAD(kernel_fb_helper_list);

int drm_fb_helper_add_connector(struct drm_connector *connector)
{
	connector->fb_helper_private = kzalloc(sizeof(struct drm_fb_helper_connector), GFP_KERNEL);
	if (!connector->fb_helper_private)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_add_connector);

static int my_atoi(const char *name)
{
	int val = 0;

	for (;; name++) {
		switch (*name) {
		case '0' ... '9':
			val = 10*val+(*name-'0');
			break;
		default:
			return val;
		}
	}
}

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
static bool drm_fb_helper_connector_parse_command_line(struct drm_connector *connector,
						       const char *mode_option)
{
	const char *name;
	unsigned int namelen;
	int res_specified = 0, bpp_specified = 0, refresh_specified = 0;
	unsigned int xres = 0, yres = 0, bpp = 32, refresh = 0;
	int yres_specified = 0, cvt = 0, rb = 0, interlace = 0, margins = 0;
	int i;
	enum drm_connector_force force = DRM_FORCE_UNSPECIFIED;
	struct drm_fb_helper_connector *fb_help_conn = connector->fb_helper_private;
	struct drm_fb_helper_cmdline_mode *cmdline_mode;

	if (!fb_help_conn)
		return false;

	cmdline_mode = &fb_help_conn->cmdline_mode;
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
				refresh = my_atoi(&name[i+1]);
				refresh_specified = 1;
				if (cvt || rb)
					cvt = 0;
			} else
				goto done;
			break;
		case '-':
			namelen = i;
			if (!bpp_specified && !yres_specified) {
				bpp = my_atoi(&name[i+1]);
				bpp_specified = 1;
				if (cvt || rb)
					cvt = 0;
			} else
				goto done;
			break;
		case 'x':
			if (!yres_specified) {
				yres = my_atoi(&name[i+1]);
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
			if (!cvt)
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
		xres = my_atoi(name);
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

int drm_fb_helper_parse_command_line(struct drm_device *dev)
{
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		char *option = NULL;

		/* do something on return - turn off connector maybe */
		if (fb_get_options(drm_get_connector_name(connector), &option))
			continue;

		drm_fb_helper_connector_parse_command_line(connector, option);
	}
	return 0;
}

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
	DRM_ERROR("panic occurred, switching back to text console\n");
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

static void drm_fb_helper_sysrq(int dummy1, struct tty_struct *dummy3)
{
	schedule_work(&drm_fb_helper_restore_work);
}

static struct sysrq_key_op sysrq_drm_fb_helper_restore_op = {
	.handler = drm_fb_helper_sysrq,
	.help_msg = "force-fb(V)",
	.action_msg = "Restore framebuffer console",
};
#endif

static void drm_fb_helper_on(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int i;

	/*
	 * For each CRTC in this fb, turn the crtc on then,
	 * find all associated encoders and turn them on.
	 */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct drm_crtc_helper_funcs *crtc_funcs =
				crtc->helper_private;

			/* Only mess with CRTCs in this fb */
			if (crtc->base.id != fb_helper->crtc_info[i].crtc_id ||
			    !crtc->enabled)
				continue;

			mutex_lock(&dev->mode_config.mutex);
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
			mutex_unlock(&dev->mode_config.mutex);

			/* Found a CRTC on this fb, now find encoders */
			list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
				if (encoder->crtc == crtc) {
					struct drm_encoder_helper_funcs *encoder_funcs;

					encoder_funcs = encoder->helper_private;
					mutex_lock(&dev->mode_config.mutex);
					encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
					mutex_unlock(&dev->mode_config.mutex);
				}
			}
		}
	}
}

static void drm_fb_helper_off(struct fb_info *info, int dpms_mode)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int i;

	/*
	 * For each CRTC in this fb, find all associated encoders
	 * and turn them off, then turn off the CRTC.
	 */
	for (i = 0; i < fb_helper->crtc_count; i++) {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct drm_crtc_helper_funcs *crtc_funcs =
				crtc->helper_private;

			/* Only mess with CRTCs in this fb */
			if (crtc->base.id != fb_helper->crtc_info[i].crtc_id ||
			    !crtc->enabled)
				continue;

			/* Found a CRTC on this fb, now find encoders */
			list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
				if (encoder->crtc == crtc) {
					struct drm_encoder_helper_funcs *encoder_funcs;

					encoder_funcs = encoder->helper_private;
					mutex_lock(&dev->mode_config.mutex);
					encoder_funcs->dpms(encoder, dpms_mode);
					mutex_unlock(&dev->mode_config.mutex);
				}
			}
			mutex_lock(&dev->mode_config.mutex);
			crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
			mutex_unlock(&dev->mode_config.mutex);
		}
	}
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
		drm_fb_helper_off(info, DRM_MODE_DPMS_ON);
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

	for (i = 0; i < helper->crtc_count; i++)
		kfree(helper->crtc_info[i].mode_set.connectors);
	kfree(helper->crtc_info);
}

int drm_fb_helper_init_crtc_count(struct drm_fb_helper *helper, int crtc_count, int max_conn_count)
{
	struct drm_device *dev = helper->dev;
	struct drm_crtc *crtc;
	int ret = 0;
	int i;

	helper->crtc_info = kcalloc(crtc_count, sizeof(struct drm_fb_helper_crtc), GFP_KERNEL);
	if (!helper->crtc_info)
		return -ENOMEM;

	helper->crtc_count = crtc_count;

	for (i = 0; i < crtc_count; i++) {
		helper->crtc_info[i].mode_set.connectors =
			kcalloc(max_conn_count,
				sizeof(struct drm_connector *),
				GFP_KERNEL);

		if (!helper->crtc_info[i].mode_set.connectors) {
			ret = -ENOMEM;
			goto out_free;
		}
		helper->crtc_info[i].mode_set.num_connectors = 0;
	}

	i = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		helper->crtc_info[i].crtc_id = crtc->base.id;
		helper->crtc_info[i].mode_set.crtc = crtc;
		i++;
	}
	helper->conn_limit = max_conn_count;
	return 0;
out_free:
	drm_fb_helper_crtc_free(helper);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_fb_helper_init_crtc_count);

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
	struct drm_device *dev = fb_helper->dev;
	u16 *red, *green, *blue, *transp;
	struct drm_crtc *crtc;
	int i, rc = 0;
	int start;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		for (i = 0; i < fb_helper->crtc_count; i++) {
			if (crtc->base.id == fb_helper->crtc_info[i].crtc_id)
				break;
		}
		if (i == fb_helper->crtc_count)
			continue;

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

int drm_fb_helper_setcolreg(unsigned regno,
			    unsigned red,
			    unsigned green,
			    unsigned blue,
			    unsigned transp,
			    struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_device *dev = fb_helper->dev;
	struct drm_crtc *crtc;
	int i;
	int ret;

	if (regno > 255)
		return 1;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		for (i = 0; i < fb_helper->crtc_count; i++) {
			if (crtc->base.id == fb_helper->crtc_info[i].crtc_id)
				break;
		}
		if (i == fb_helper->crtc_count)
			continue;

		ret = setcolreg(crtc, red, green, blue, regno, info);
		if (ret)
			return ret;

		crtc_funcs->load_lut(crtc);
	}
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_setcolreg);

int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	int depth;

	if (var->pixclock != 0)
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
		DRM_ERROR("PIXEL CLCOK SET\n");
		return -EINVAL;
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		for (i = 0; i < fb_helper->crtc_count; i++) {
			if (crtc->base.id == fb_helper->crtc_info[i].crtc_id)
				break;
		}
		if (i == fb_helper->crtc_count)
			continue;

		if (crtc->fb == fb_helper->crtc_info[i].mode_set.fb) {
			mutex_lock(&dev->mode_config.mutex);
			ret = crtc->funcs->set_config(&fb_helper->crtc_info[i].mode_set);
			mutex_unlock(&dev->mode_config.mutex);
			if (ret)
				return ret;
		}
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

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		for (i = 0; i < fb_helper->crtc_count; i++) {
			if (crtc->base.id == fb_helper->crtc_info[i].crtc_id)
				break;
		}

		if (i == fb_helper->crtc_count)
			continue;

		modeset = &fb_helper->crtc_info[i].mode_set;

		modeset->x = var->xoffset;
		modeset->y = var->yoffset;

		if (modeset->num_connectors) {
			mutex_lock(&dev->mode_config.mutex);
			ret = crtc->funcs->set_config(modeset);
			mutex_unlock(&dev->mode_config.mutex);
			if (!ret) {
				info->var.xoffset = var->xoffset;
				info->var.yoffset = var->yoffset;
			}
		}
	}
	return ret;
}
EXPORT_SYMBOL(drm_fb_helper_pan_display);

int drm_fb_helper_single_fb_probe(struct drm_device *dev,
				  int preferred_bpp,
				  int (*fb_create)(struct drm_device *dev,
						   uint32_t fb_width,
						   uint32_t fb_height,
						   uint32_t surface_width,
						   uint32_t surface_height,
						   uint32_t surface_depth,
						   uint32_t surface_bpp,
						   struct drm_framebuffer **fb_ptr))
{
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	unsigned int fb_width = (unsigned)-1, fb_height = (unsigned)-1;
	unsigned int surface_width = 0, surface_height = 0;
	int new_fb = 0;
	int crtc_count = 0;
	int ret, i, conn_count = 0;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_set *modeset = NULL;
	struct drm_fb_helper *fb_helper;
	uint32_t surface_depth = 24, surface_bpp = 32;

	/* if driver picks 8 or 16 by default use that
	   for both depth/bpp */
	if (preferred_bpp != surface_bpp) {
		surface_depth = surface_bpp = preferred_bpp;
	}
	/* first up get a count of crtcs now in use and new min/maxes width/heights */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_fb_helper_connector *fb_help_conn = connector->fb_helper_private;

		struct drm_fb_helper_cmdline_mode *cmdline_mode;

		if (!fb_help_conn)
			continue;
		
		cmdline_mode = &fb_help_conn->cmdline_mode;

		if (cmdline_mode->bpp_specified) {
			switch (cmdline_mode->bpp) {
			case 8:
				surface_depth = surface_bpp = 8;
				break;
			case 15:
				surface_depth = 15;
				surface_bpp = 16;
				break;
			case 16:
				surface_depth = surface_bpp = 16;
				break;
			case 24:
				surface_depth = surface_bpp = 24;
				break;
			case 32:
				surface_depth = 24;
				surface_bpp = 32;
				break;
			}
			break;
		}
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (drm_helper_crtc_in_use(crtc)) {
			if (crtc->desired_mode) {
				if (crtc->desired_mode->hdisplay < fb_width)
					fb_width = crtc->desired_mode->hdisplay;

				if (crtc->desired_mode->vdisplay < fb_height)
					fb_height = crtc->desired_mode->vdisplay;

				if (crtc->desired_mode->hdisplay > surface_width)
					surface_width = crtc->desired_mode->hdisplay;

				if (crtc->desired_mode->vdisplay > surface_height)
					surface_height = crtc->desired_mode->vdisplay;
			}
			crtc_count++;
		}
	}

	if (crtc_count == 0 || fb_width == -1 || fb_height == -1) {
		/* hmm everyone went away - assume VGA cable just fell out
		   and will come back later. */
		return 0;
	}

	/* do we have an fb already? */
	if (list_empty(&dev->mode_config.fb_kernel_list)) {
		ret = (*fb_create)(dev, fb_width, fb_height, surface_width,
				   surface_height, surface_depth, surface_bpp,
				   &fb);
		if (ret)
			return -EINVAL;
		new_fb = 1;
	} else {
		fb = list_first_entry(&dev->mode_config.fb_kernel_list,
				      struct drm_framebuffer, filp_head);

		/* if someone hotplugs something bigger than we have already allocated, we are pwned.
		   As really we can't resize an fbdev that is in the wild currently due to fbdev
		   not really being designed for the lower layers moving stuff around under it.
		   - so in the grand style of things - punt. */
		if ((fb->width < surface_width) ||
		    (fb->height < surface_height)) {
			DRM_ERROR("Framebuffer not large enough to scale console onto.\n");
			return -EINVAL;
		}
	}

	info = fb->fbdev;
	fb_helper = info->par;

	crtc_count = 0;
	/* okay we need to setup new connector sets in the crtcs */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		modeset = &fb_helper->crtc_info[crtc_count].mode_set;
		modeset->fb = fb;
		conn_count = 0;
		list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
			if (connector->encoder)
				if (connector->encoder->crtc == modeset->crtc) {
					modeset->connectors[conn_count] = connector;
					conn_count++;
					if (conn_count > fb_helper->conn_limit)
						BUG();
				}
		}

		for (i = conn_count; i < fb_helper->conn_limit; i++)
			modeset->connectors[i] = NULL;

		modeset->crtc = crtc;
		crtc_count++;

		modeset->num_connectors = conn_count;
		if (modeset->crtc->desired_mode) {
			if (modeset->mode)
				drm_mode_destroy(dev, modeset->mode);
			modeset->mode = drm_mode_duplicate(dev,
							   modeset->crtc->desired_mode);
		}
	}
	fb_helper->crtc_count = crtc_count;
	fb_helper->fb = fb;

	if (new_fb) {
		info->var.pixclock = 0;
		ret = fb_alloc_cmap(&info->cmap, modeset->crtc->gamma_size, 0);
		if (ret)
			return ret;
		if (register_framebuffer(info) < 0) {
			fb_dealloc_cmap(&info->cmap);
			return -EINVAL;
		}
	} else {
		drm_fb_helper_set_par(info);
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
	       info->fix.id);

	/* Switch back to kernel console on panic */
	/* multi card linked list maybe */
	if (list_empty(&kernel_fb_helper_list)) {
		printk(KERN_INFO "registered panic notifier\n");
		atomic_notifier_chain_register(&panic_notifier_list,
					       &paniced);
		register_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
	}
	list_add(&fb_helper->kernel_fb_list, &kernel_fb_helper_list);
	return 0;
}
EXPORT_SYMBOL(drm_fb_helper_single_fb_probe);

void drm_fb_helper_free(struct drm_fb_helper *helper)
{
	list_del(&helper->kernel_fb_list);
	if (list_empty(&kernel_fb_helper_list)) {
		printk(KERN_INFO "unregistered panic notifier\n");
		atomic_notifier_chain_unregister(&panic_notifier_list,
						 &paniced);
		unregister_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);
	}
	drm_fb_helper_crtc_free(helper);
	fb_dealloc_cmap(&helper->fb->fbdev->cmap);
}
EXPORT_SYMBOL(drm_fb_helper_free);

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

void drm_fb_helper_fill_var(struct fb_info *info, struct drm_framebuffer *fb,
			    uint32_t fb_width, uint32_t fb_height)
{
	info->pseudo_palette = fb->pseudo_palette;
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
