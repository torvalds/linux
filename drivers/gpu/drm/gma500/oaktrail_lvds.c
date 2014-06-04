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
#include <asm/intel-mid.h>

#include "intel_bios.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "power.h"
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
static void oaktrail_lvds_set_power(struct drm_device *dev,
				struct gma_encoder *gma_encoder,
				bool on)
{
	u32 pp_status;
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (!gma_power_begin(dev, true))
		return;

	if (on) {
		REG_WRITE(PP_CONTROL, REG_READ(PP_CONTROL) |
			  POWER_TARGET_ON);
		do {
			pp_status = REG_READ(PP_STATUS);
		} while ((pp_status & (PP_ON | PP_READY)) == PP_READY);
		dev_priv->is_lvds_on = true;
		if (dev_priv->ops->lvds_bl_power)
			dev_priv->ops->lvds_bl_power(dev, true);
	} else {
		if (dev_priv->ops->lvds_bl_power)
			dev_priv->ops->lvds_bl_power(dev, false);
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

static void oaktrail_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);

	if (mode == DRM_MODE_DPMS_ON)
		oaktrail_lvds_set_power(dev, gma_encoder, true);
	else
		oaktrail_lvds_set_power(dev, gma_encoder, false);

	/* XXX: We never power down the LVDS pairs. */
}

static void oaktrail_lvds_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector = NULL;
	struct drm_crtc *crtc = encoder->crtc;
	u32 lvds_port;
	uint64_t v = DRM_MODE_SCALE_FULLSCREEN;

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

	/* If the firmware says dither on Moorestown, or the BIOS does
	   on Oaktrail then enable dithering */
	if (mode_dev->panel_wants_dither || dev_priv->lvds_dither)
		lvds_port |= MRST_PANEL_8TO6_DITHER_ENABLE;

	REG_WRITE(LVDS, lvds_port);

	/* Find the connector we're trying to set up */
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		if (!connector->encoder || connector->encoder->crtc != crtc)
			continue;
	}

	if (!connector) {
		DRM_ERROR("Couldn't find connector when setting mode");
		return;
	}

	drm_object_property_get_value(
		&connector->base,
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

static void oaktrail_lvds_prepare(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;

	if (!gma_power_begin(dev, true))
		return;

	mode_dev->saveBLC_PWM_CTL = REG_READ(BLC_PWM_CTL);
	mode_dev->backlight_duty_cycle = (mode_dev->saveBLC_PWM_CTL &
					  BACKLIGHT_DUTY_CYCLE_MASK);
	oaktrail_lvds_set_power(dev, gma_encoder, false);
	gma_power_end(dev);
}

static u32 oaktrail_lvds_get_max_backlight(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 ret;

	if (gma_power_begin(dev, false)) {
		ret = ((REG_READ(BLC_PWM_CTL) &
			  BACKLIGHT_MODULATION_FREQ_MASK) >>
			  BACKLIGHT_MODULATION_FREQ_SHIFT) * 2;

		gma_power_end(dev);
	} else
		ret = ((dev_priv->regs.saveBLC_PWM_CTL &
			  BACKLIGHT_MODULATION_FREQ_MASK) >>
			  BACKLIGHT_MODULATION_FREQ_SHIFT) * 2;

	return ret;
}

static void oaktrail_lvds_commit(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;

	if (mode_dev->backlight_duty_cycle == 0)
		mode_dev->backlight_duty_cycle =
					oaktrail_lvds_get_max_backlight(dev);
	oaktrail_lvds_set_power(dev, gma_encoder, true);
}

static const struct drm_encoder_helper_funcs oaktrail_lvds_helper_funcs = {
	.dpms = oaktrail_lvds_dpms,
	.mode_fixup = psb_intel_lvds_mode_fixup,
	.prepare = oaktrail_lvds_prepare,
	.mode_set = oaktrail_lvds_mode_set,
	.commit = oaktrail_lvds_commit,
};

/* Returns the panel fixed mode from configuration. */

static void oaktrail_lvds_get_configuration_mode(struct drm_device *dev,
					struct psb_intel_mode_device *mode_dev)
{
	struct drm_display_mode *mode = NULL;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct oaktrail_timing_info *ti = &dev_priv->gct_data.DTD;

	mode_dev->panel_fixed_mode = NULL;

	/* Use the firmware provided data on Moorestown */
	if (dev_priv->has_gct) {
		mode = kzalloc(sizeof(*mode), GFP_KERNEL);
		if (!mode)
			return;

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
		mode_dev->panel_fixed_mode = mode;
	}

	/* Use the BIOS VBT mode if available */
	if (mode_dev->panel_fixed_mode == NULL && mode_dev->vbt_mode)
		mode_dev->panel_fixed_mode = drm_mode_duplicate(dev,
						mode_dev->vbt_mode);

	/* Then try the LVDS VBT mode */
	if (mode_dev->panel_fixed_mode == NULL)
		if (dev_priv->lfp_lvds_vbt_mode)
			mode_dev->panel_fixed_mode =
				drm_mode_duplicate(dev,
					dev_priv->lfp_lvds_vbt_mode);

	/* If we still got no mode then bail */
	if (mode_dev->panel_fixed_mode == NULL)
		return;

	drm_mode_set_name(mode_dev->panel_fixed_mode);
	drm_mode_set_crtcinfo(mode_dev->panel_fixed_mode, 0);
}

/**
 * oaktrail_lvds_init - setup LVDS connectors on this device
 * @dev: drm device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void oaktrail_lvds_init(struct drm_device *dev,
		    struct psb_intel_mode_device *mode_dev)
{
	struct gma_encoder *gma_encoder;
	struct gma_connector *gma_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct edid *edid;
	struct i2c_adapter *i2c_adap;
	struct drm_display_mode *scan;	/* *modes, *bios_mode; */

	gma_encoder = kzalloc(sizeof(struct gma_encoder), GFP_KERNEL);
	if (!gma_encoder)
		return;

	gma_connector = kzalloc(sizeof(struct gma_connector), GFP_KERNEL);
	if (!gma_connector)
		goto failed_connector;

	connector = &gma_connector->base;
	encoder = &gma_encoder->base;
	dev_priv->is_lvds_on = true;
	drm_connector_init(dev, connector,
			   &psb_intel_lvds_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, encoder, &psb_intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS);

	gma_connector_attach_encoder(gma_connector, gma_encoder);
	gma_encoder->type = INTEL_OUTPUT_LVDS;

	drm_encoder_helper_add(encoder, &oaktrail_lvds_helper_funcs);
	drm_connector_helper_add(connector,
				 &psb_intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_object_attach_property(&connector->base,
					dev->mode_config.scaling_mode_property,
					DRM_MODE_SCALE_FULLSCREEN);
	drm_object_attach_property(&connector->base,
					dev_priv->backlight_property,
					BRIGHTNESS_MAX_LEVEL);

	mode_dev->panel_wants_dither = false;
	if (dev_priv->has_gct)
		mode_dev->panel_wants_dither = (dev_priv->gct_data.
			Panel_Port_Control & MRST_PANEL_8TO6_DITHER_ENABLE);
        if (dev_priv->lvds_dither)
                mode_dev->panel_wants_dither = 1;

	/*
	 * LVDS discovery:
	 * 1) check for EDID on DDC
	 * 2) check for VBT data
	 * 3) check to see if LVDS is already on
	 *    if none of the above, no panel
	 * 4) make sure lid is open
	 *    if closed, act like it's not there for now
	 */

	mutex_lock(&dev->mode_config.mutex);
	i2c_adap = i2c_get_adapter(dev_priv->ops->i2c_bus);
	if (i2c_adap == NULL)
		dev_err(dev->dev, "No ddc adapter available!\n");
	/*
	 * Attempt to get the fixed panel mode from DDC.  Assume that the
	 * preferred mode is the right one.
	 */
	if (i2c_adap) {
		edid = drm_get_edid(connector, i2c_adap);
		if (edid) {
			drm_mode_connector_update_edid_property(connector,
									edid);
			drm_add_edid_modes(connector, edid);
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
	oaktrail_lvds_get_configuration_mode(dev, mode_dev);

	if (mode_dev->panel_fixed_mode) {
		mode_dev->panel_fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
		goto out;	/* FIXME: check for quirks */
	}

	/* If we still don't have a mode after all that, give up. */
	if (!mode_dev->panel_fixed_mode) {
		dev_err(dev->dev, "Found no modes on the lvds, ignoring the LVDS\n");
		goto failed_find;
	}

out:
	mutex_unlock(&dev->mode_config.mutex);

	drm_sysfs_connector_add(connector);
	return;

failed_find:
	mutex_unlock(&dev->mode_config.mutex);

	dev_dbg(dev->dev, "No LVDS modes found, disabling.\n");
	if (gma_encoder->ddc_bus)
		psb_intel_i2c_destroy(gma_encoder->ddc_bus);

/* failed_ddc: */

	drm_encoder_cleanup(encoder);
	drm_connector_cleanup(connector);
	kfree(gma_connector);
failed_connector:
	kfree(gma_encoder);
}

