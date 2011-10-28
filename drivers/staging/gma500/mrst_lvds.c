/*
 * Copyright © 2006-2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Dave Airlie <airlied@linux.ie>
 *	Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/i2c.h>
#include <drm/drmP.h>
#include <asm/mrst.h>

#include "psb_intel_bios.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_powermgmt.h"
#include <linux/pm_runtime.h>

/* The max/min PWM frequency in BPCR[31:17] - */
/* The smallest number is 1 (not 0) that can fit in the
 * 15-bit field of the and then*/
/* shifts to the left by one bit to get the actual 16-bit
 * value that the 15-bits correspond to.*/
#define MRST_BLC_MAX_PWM_REG_FREQ	    0xFFFF
#define BRIGHTNESS_MAX_LEVEL 100

/**
 * Sets the power state for the panel.
 */
static void mrst_lvds_set_power(struct drm_device *dev,
				struct psb_intel_output *output, bool on)
{
	u32 pp_status;
	DRM_DRIVER_PRIVATE_T *dev_priv = dev->dev_private;
	PSB_DEBUG_ENTRY("\n");

	if (!gma_power_begin(dev, true))
		return;

	if (on) {
		REG_WRITE(PP_CONTROL, REG_READ(PP_CONTROL) |
			  POWER_TARGET_ON);
		do {
			pp_status = REG_READ(PP_STATUS);
		} while ((pp_status & (PP_ON | PP_READY)) == PP_READY);
		dev_priv->is_lvds_on = true;
	} else {
		REG_WRITE(PP_CONTROL, REG_READ(PP_CONTROL) &
			  ~POWER_TARGET_ON);
		do {
			pp_status = REG_READ(PP_STATUS);
		} while (pp_status & PP_ON);
		dev_priv->is_lvds_on = false;
		pm_request_idle(&dev->pdev->dev);
	}

	gma_power_end(dev);
}

static void mrst_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct psb_intel_output *output = enc_to_psb_intel_output(encoder);

	PSB_DEBUG_ENTRY("\n");

	if (mode == DRM_MODE_DPMS_ON)
		mrst_lvds_set_power(dev, output, true);
	else
		mrst_lvds_set_power(dev, output, false);

	/* XXX: We never power down the LVDS pairs. */
}

static void mrst_lvds_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct psb_intel_mode_device *mode_dev =
				enc_to_psb_intel_output(encoder)->mode_dev;
	struct drm_device *dev = encoder->dev;
	u32 lvds_port;
	uint64_t v = DRM_MODE_SCALE_FULLSCREEN;

	PSB_DEBUG_ENTRY("\n");

	if (!gma_power_begin(dev, true))
		return;

	/*
	 * The LVDS pin pair will already have been turned on in the
	 * psb_intel_crtc_mode_set since it has a large impact on the DPLL
	 * settings.
	 */
	lvds_port = (REG_READ(LVDS) &
		    (~LVDS_PIPEB_SELECT)) |
		    LVDS_PORT_EN |
		    LVDS_BORDER_EN;

	if (mode_dev->panel_wants_dither)
		lvds_port |= MRST_PANEL_8TO6_DITHER_ENABLE;

	REG_WRITE(LVDS, lvds_port);

	drm_connector_property_get_value(
		&enc_to_psb_intel_output(encoder)->base,
		dev->mode_config.scaling_mode_property,
		&v);

	if (v == DRM_MODE_SCALE_NO_SCALE)
		REG_WRITE(PFIT_CONTROL, 0);
	else if (v == DRM_MODE_SCALE_ASPECT) {
		if ((mode->vdisplay != adjusted_mode->crtc_vdisplay) ||
		    (mode->hdisplay != adjusted_mode->crtc_hdisplay)) {
			if ((adjusted_mode->crtc_hdisplay * mode->vdisplay) ==
			    (mode->hdisplay * adjusted_mode->crtc_vdisplay))
				REG_WRITE(PFIT_CONTROL, PFIT_ENABLE);
			else if ((adjusted_mode->crtc_hdisplay *
				mode->vdisplay) > (mode->hdisplay *
				adjusted_mode->crtc_vdisplay))
				REG_WRITE(PFIT_CONTROL, PFIT_ENABLE |
					  PFIT_SCALING_MODE_PILLARBOX);
			else
				REG_WRITE(PFIT_CONTROL, PFIT_ENABLE |
					  PFIT_SCALING_MODE_LETTERBOX);
		} else
			REG_WRITE(PFIT_CONTROL, PFIT_ENABLE);
	} else /*(v == DRM_MODE_SCALE_FULLSCREEN)*/
		REG_WRITE(PFIT_CONTROL, PFIT_ENABLE);

	gma_power_end(dev);
}


static const struct drm_encoder_helper_funcs mrst_lvds_helper_funcs = {
	.dpms = mrst_lvds_dpms,
	.mode_fixup = psb_intel_lvds_mode_fixup,
	.prepare = psb_intel_lvds_prepare,
	.mode_set = mrst_lvds_mode_set,
	.commit = psb_intel_lvds_commit,
};

static struct drm_display_mode lvds_configuration_modes[] = {
	/* hard coded fixed mode for TPO LTPS LPJ040K001A */
	{ DRM_MODE("800x480",  DRM_MODE_TYPE_DRIVER, 33264, 800, 836,
		   846, 1056, 0, 480, 489, 491, 525, 0, 0) },
	/* hard coded fixed mode for LVDS 800x480 */
	{ DRM_MODE("800x480",  DRM_MODE_TYPE_DRIVER, 30994, 800, 801,
		   802, 1024, 0, 480, 481, 482, 525, 0, 0) },
	/* hard coded fixed mode for Samsung 480wsvga LVDS 1024x600@75 */
	{ DRM_MODE("1024x600", DRM_MODE_TYPE_DRIVER, 53990, 1024, 1072,
		   1104, 1184, 0, 600, 603, 604, 608, 0, 0) },
	/* hard coded fixed mode for Samsung 480wsvga LVDS 1024x600@75 */
	{ DRM_MODE("1024x600", DRM_MODE_TYPE_DRIVER, 53990, 1024, 1104,
		   1136, 1184, 0, 600, 603, 604, 608, 0, 0) },
	/* hard coded fixed mode for Sharp wsvga LVDS 1024x600 */
	{ DRM_MODE("1024x600", DRM_MODE_TYPE_DRIVER, 48885, 1024, 1124,
		   1204, 1312, 0, 600, 607, 610, 621, 0, 0) },
	/* hard coded fixed mode for LVDS 1024x768 */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0, 768, 771, 777, 806, 0, 0) },
	/* hard coded fixed mode for LVDS 1366x768 */
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 77500, 1366, 1430,
		   1558, 1664, 0, 768, 769, 770, 776, 0, 0) },
};

/* Returns the panel fixed mode from configuration. */

static struct drm_display_mode *
mrst_lvds_get_configuration_mode(struct drm_device *dev)
{
	struct drm_display_mode *mode = NULL;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mrst_timing_info *ti = &dev_priv->gct_data.DTD;

	if (dev_priv->vbt_data.size != 0x00) { /*if non-zero, then use vbt*/
		mode = kzalloc(sizeof(*mode), GFP_KERNEL);
		if (!mode)
			return NULL;

		mode->hdisplay = (ti->hactive_hi << 8) | ti->hactive_lo;
		mode->vdisplay = (ti->vactive_hi << 8) | ti->vactive_lo;
		mode->hsync_start = mode->hdisplay + \
				((ti->hsync_offset_hi << 8) | \
				ti->hsync_offset_lo);
		mode->hsync_end = mode->hsync_start + \
				((ti->hsync_pulse_width_hi << 8) | \
				ti->hsync_pulse_width_lo);
		mode->htotal = mode->hdisplay + ((ti->hblank_hi << 8) | \
							ti->hblank_lo);
		mode->vsync_start = \
			mode->vdisplay + ((ti->vsync_offset_hi << 4) | \
						ti->vsync_offset_lo);
		mode->vsync_end = \
			mode->vsync_start + ((ti->vsync_pulse_width_hi << 4) | \
						ti->vsync_pulse_width_lo);
		mode->vtotal = mode->vdisplay + \
				((ti->vblank_hi << 8) | ti->vblank_lo);
		mode->clock = ti->pixel_clock * 10;
#if 0
		printk(KERN_INFO "hdisplay is %d\n", mode->hdisplay);
		printk(KERN_INFO "vdisplay is %d\n", mode->vdisplay);
		printk(KERN_INFO "HSS is %d\n", mode->hsync_start);
		printk(KERN_INFO "HSE is %d\n", mode->hsync_end);
		printk(KERN_INFO "htotal is %d\n", mode->htotal);
		printk(KERN_INFO "VSS is %d\n", mode->vsync_start);
		printk(KERN_INFO "VSE is %d\n", mode->vsync_end);
		printk(KERN_INFO "vtotal is %d\n", mode->vtotal);
		printk(KERN_INFO "clock is %d\n", mode->clock);
#endif
	} else
		mode = drm_mode_duplicate(dev, &lvds_configuration_modes[2]);

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	return mode;
}

/**
 * mrst_lvds_init - setup LVDS connectors on this device
 * @dev: drm device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void mrst_lvds_init(struct drm_device *dev,
		    struct psb_intel_mode_device *mode_dev)
{
	struct psb_intel_output *psb_intel_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_psb_private *dev_priv =
				(struct drm_psb_private *) dev->dev_private;
	struct edid *edid;
	int ret = 0;
	struct i2c_adapter *i2c_adap;
	struct drm_display_mode *scan;	/* *modes, *bios_mode; */

	PSB_DEBUG_ENTRY("\n");

	psb_intel_output = kzalloc(sizeof(struct psb_intel_output), GFP_KERNEL);
	if (!psb_intel_output)
		return;

	psb_intel_output->mode_dev = mode_dev;
	connector = &psb_intel_output->base;
	encoder = &psb_intel_output->enc;
	dev_priv->is_lvds_on = true;
	drm_connector_init(dev, &psb_intel_output->base,
			   &psb_intel_lvds_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, &psb_intel_output->enc, &psb_intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS);

	drm_mode_connector_attach_encoder(&psb_intel_output->base,
					  &psb_intel_output->enc);
	psb_intel_output->type = INTEL_OUTPUT_LVDS;

	drm_encoder_helper_add(encoder, &mrst_lvds_helper_funcs);
	drm_connector_helper_add(connector,
				 &psb_intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_connector_attach_property(connector,
					dev->mode_config.scaling_mode_property,
					DRM_MODE_SCALE_FULLSCREEN);
	drm_connector_attach_property(connector,
					dev_priv->backlight_property,
					BRIGHTNESS_MAX_LEVEL);

	mode_dev->panel_wants_dither = false;
	if (dev_priv->vbt_data.size != 0x00)
		mode_dev->panel_wants_dither = (dev_priv->gct_data.
			Panel_Port_Control & MRST_PANEL_8TO6_DITHER_ENABLE);

	/*
	 * LVDS discovery:
	 * 1) check for EDID on DDC
	 * 2) check for VBT data
	 * 3) check to see if LVDS is already on
	 *    if none of the above, no panel
	 * 4) make sure lid is open
	 *    if closed, act like it's not there for now
	 */

	 /* This ifdef can go once the cpu ident stuff is cleaned up in arch */
#if defined(CONFIG_X86_MRST)
	if (mrst_identify_cpu())
        	i2c_adap = i2c_get_adapter(2);
        else	/* Oaktrail uses I2C 1 */
#endif        
        	i2c_adap = i2c_get_adapter(1);

	if (i2c_adap == NULL)
		printk(KERN_ALERT "No ddc adapter available!\n");
	/*
	 * Attempt to get the fixed panel mode from DDC.  Assume that the
	 * preferred mode is the right one.
	 */
	if (i2c_adap) {
		edid = drm_get_edid(connector, i2c_adap);
		if (edid) {
			drm_mode_connector_update_edid_property(connector,
									edid);
			ret = drm_add_edid_modes(connector, edid);
			kfree(edid);
		}

		list_for_each_entry(scan, &connector->probed_modes, head) {
			if (scan->type & DRM_MODE_TYPE_PREFERRED) {
				mode_dev->panel_fixed_mode =
				    drm_mode_duplicate(dev, scan);
				goto out;	/* FIXME: check for quirks */
			}
		}
	}

	/*
	 * If we didn't get EDID, try geting panel timing
	 * from configuration data
	 */
	mode_dev->panel_fixed_mode = mrst_lvds_get_configuration_mode(dev);

	if (mode_dev->panel_fixed_mode) {
		mode_dev->panel_fixed_mode->type |=
		    DRM_MODE_TYPE_PREFERRED;
		goto out;	/* FIXME: check for quirks */
	}

	/* If we still don't have a mode after all that, give up. */
	if (!mode_dev->panel_fixed_mode) {
		DRM_DEBUG
		    ("Found no modes on the lvds, ignoring the LVDS\n");
		goto failed_find;
	}

out:
	drm_sysfs_connector_add(connector);
	return;

failed_find:
	DRM_DEBUG("No LVDS modes found, disabling.\n");
	if (psb_intel_output->ddc_bus)
		psb_intel_i2c_destroy(psb_intel_output->ddc_bus);

/* failed_ddc: */

	drm_encoder_cleanup(encoder);
	drm_connector_cleanup(connector);
	kfree(connector);
}

