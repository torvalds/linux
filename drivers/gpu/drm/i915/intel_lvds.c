/*
 * Copyright © 2006-2007 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
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
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/dmi.h>
#include <linux/i2c.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define I915_LVDS "i915_lvds"

/**
 * Sets the backlight level.
 *
 * \param level backlight level, from 0 to intel_lvds_get_max_backlight().
 */
static void intel_lvds_set_backlight(struct drm_device *dev, int level)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 blc_pwm_ctl, reg;

	if (IS_IGDNG(dev))
		reg = BLC_PWM_CPU_CTL;
	else
		reg = BLC_PWM_CTL;

	blc_pwm_ctl = I915_READ(reg) & ~BACKLIGHT_DUTY_CYCLE_MASK;
	I915_WRITE(reg, (blc_pwm_ctl |
				 (level << BACKLIGHT_DUTY_CYCLE_SHIFT)));
}

/**
 * Returns the maximum level of the backlight duty cycle field.
 */
static u32 intel_lvds_get_max_backlight(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;

	if (IS_IGDNG(dev))
		reg = BLC_PWM_PCH_CTL2;
	else
		reg = BLC_PWM_CTL;

	return ((I915_READ(reg) & BACKLIGHT_MODULATION_FREQ_MASK) >>
		BACKLIGHT_MODULATION_FREQ_SHIFT) * 2;
}

/**
 * Sets the power state for the panel.
 */
static void intel_lvds_set_power(struct drm_device *dev, bool on)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_status, ctl_reg, status_reg;

	if (IS_IGDNG(dev)) {
		ctl_reg = PCH_PP_CONTROL;
		status_reg = PCH_PP_STATUS;
	} else {
		ctl_reg = PP_CONTROL;
		status_reg = PP_STATUS;
	}

	if (on) {
		I915_WRITE(ctl_reg, I915_READ(ctl_reg) |
			   POWER_TARGET_ON);
		do {
			pp_status = I915_READ(status_reg);
		} while ((pp_status & PP_ON) == 0);

		intel_lvds_set_backlight(dev, dev_priv->backlight_duty_cycle);
	} else {
		intel_lvds_set_backlight(dev, 0);

		I915_WRITE(ctl_reg, I915_READ(ctl_reg) &
			   ~POWER_TARGET_ON);
		do {
			pp_status = I915_READ(status_reg);
		} while (pp_status & PP_ON);
	}
}

static void intel_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;

	if (mode == DRM_MODE_DPMS_ON)
		intel_lvds_set_power(dev, true);
	else
		intel_lvds_set_power(dev, false);

	/* XXX: We never power down the LVDS pairs. */
}

static void intel_lvds_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_on_reg, pp_off_reg, pp_ctl_reg, pp_div_reg;
	u32 pwm_ctl_reg;

	if (IS_IGDNG(dev)) {
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_ctl_reg = PCH_PP_CONTROL;
		pp_div_reg = PCH_PP_DIVISOR;
		pwm_ctl_reg = BLC_PWM_CPU_CTL;
	} else {
		pp_on_reg = PP_ON_DELAYS;
		pp_off_reg = PP_OFF_DELAYS;
		pp_ctl_reg = PP_CONTROL;
		pp_div_reg = PP_DIVISOR;
		pwm_ctl_reg = BLC_PWM_CTL;
	}

	dev_priv->savePP_ON = I915_READ(pp_on_reg);
	dev_priv->savePP_OFF = I915_READ(pp_off_reg);
	dev_priv->savePP_CONTROL = I915_READ(pp_ctl_reg);
	dev_priv->savePP_DIVISOR = I915_READ(pp_div_reg);
	dev_priv->saveBLC_PWM_CTL = I915_READ(pwm_ctl_reg);
	dev_priv->backlight_duty_cycle = (dev_priv->saveBLC_PWM_CTL &
				       BACKLIGHT_DUTY_CYCLE_MASK);

	/*
	 * If the light is off at server startup, just make it full brightness
	 */
	if (dev_priv->backlight_duty_cycle == 0)
		dev_priv->backlight_duty_cycle =
			intel_lvds_get_max_backlight(dev);
}

static void intel_lvds_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_on_reg, pp_off_reg, pp_ctl_reg, pp_div_reg;
	u32 pwm_ctl_reg;

	if (IS_IGDNG(dev)) {
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_ctl_reg = PCH_PP_CONTROL;
		pp_div_reg = PCH_PP_DIVISOR;
		pwm_ctl_reg = BLC_PWM_CPU_CTL;
	} else {
		pp_on_reg = PP_ON_DELAYS;
		pp_off_reg = PP_OFF_DELAYS;
		pp_ctl_reg = PP_CONTROL;
		pp_div_reg = PP_DIVISOR;
		pwm_ctl_reg = BLC_PWM_CTL;
	}

	I915_WRITE(pwm_ctl_reg, dev_priv->saveBLC_PWM_CTL);
	I915_WRITE(pp_on_reg, dev_priv->savePP_ON);
	I915_WRITE(pp_off_reg, dev_priv->savePP_OFF);
	I915_WRITE(pp_div_reg, dev_priv->savePP_DIVISOR);
	I915_WRITE(pp_ctl_reg, dev_priv->savePP_CONTROL);
	if (dev_priv->savePP_CONTROL & POWER_TARGET_ON)
		intel_lvds_set_power(dev, true);
	else
		intel_lvds_set_power(dev, false);
}

static int intel_lvds_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *fixed_mode = dev_priv->panel_fixed_mode;

	if (fixed_mode)	{
		if (mode->hdisplay > fixed_mode->hdisplay)
			return MODE_PANEL;
		if (mode->vdisplay > fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	return MODE_OK;
}

static bool intel_lvds_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	struct drm_encoder *tmp_encoder;

	/* Should never happen!! */
	if (!IS_I965G(dev) && intel_crtc->pipe == 0) {
		printk(KERN_ERR "Can't support LVDS on pipe A\n");
		return false;
	}

	/* Should never happen!! */
	list_for_each_entry(tmp_encoder, &dev->mode_config.encoder_list, head) {
		if (tmp_encoder != encoder && tmp_encoder->crtc == encoder->crtc) {
			printk(KERN_ERR "Can't enable LVDS and another "
			       "encoder on the same pipe\n");
			return false;
		}
	}

	/*
	 * If we have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	if (dev_priv->panel_fixed_mode != NULL) {
		adjusted_mode->hdisplay = dev_priv->panel_fixed_mode->hdisplay;
		adjusted_mode->hsync_start =
			dev_priv->panel_fixed_mode->hsync_start;
		adjusted_mode->hsync_end =
			dev_priv->panel_fixed_mode->hsync_end;
		adjusted_mode->htotal = dev_priv->panel_fixed_mode->htotal;
		adjusted_mode->vdisplay = dev_priv->panel_fixed_mode->vdisplay;
		adjusted_mode->vsync_start =
			dev_priv->panel_fixed_mode->vsync_start;
		adjusted_mode->vsync_end =
			dev_priv->panel_fixed_mode->vsync_end;
		adjusted_mode->vtotal = dev_priv->panel_fixed_mode->vtotal;
		adjusted_mode->clock = dev_priv->panel_fixed_mode->clock;
		drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
	}

	/*
	 * XXX: It would be nice to support lower refresh rates on the
	 * panels to reduce power consumption, and perhaps match the
	 * user's requested refresh rate.
	 */

	return true;
}

static void intel_lvds_prepare(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;

	if (IS_IGDNG(dev))
		reg = BLC_PWM_CPU_CTL;
	else
		reg = BLC_PWM_CTL;

	dev_priv->saveBLC_PWM_CTL = I915_READ(reg);
	dev_priv->backlight_duty_cycle = (dev_priv->saveBLC_PWM_CTL &
				       BACKLIGHT_DUTY_CYCLE_MASK);

	intel_lvds_set_power(dev, false);
}

static void intel_lvds_commit( struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->backlight_duty_cycle == 0)
		dev_priv->backlight_duty_cycle =
			intel_lvds_get_max_backlight(dev);

	intel_lvds_set_power(dev, true);
}

static void intel_lvds_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	u32 pfit_control;

	/*
	 * The LVDS pin pair will already have been turned on in the
	 * intel_crtc_mode_set since it has a large impact on the DPLL
	 * settings.
	 */

	/* No panel fitting yet, fixme */
	if (IS_IGDNG(dev))
		return;

	/*
	 * Enable automatic panel scaling so that non-native modes fill the
	 * screen.  Should be enabled before the pipe is enabled, according to
	 * register description and PRM.
	 */
	if (mode->hdisplay != adjusted_mode->hdisplay ||
	    mode->vdisplay != adjusted_mode->vdisplay)
		pfit_control = (PFIT_ENABLE | VERT_AUTO_SCALE |
				HORIZ_AUTO_SCALE | VERT_INTERP_BILINEAR |
				HORIZ_INTERP_BILINEAR);
	else
		pfit_control = 0;

	if (!IS_I965G(dev)) {
		if (dev_priv->panel_wants_dither || dev_priv->lvds_dither)
			pfit_control |= PANEL_8TO6_DITHER_ENABLE;
	}
	else
		pfit_control |= intel_crtc->pipe << PFIT_PIPE_SHIFT;

	I915_WRITE(PFIT_CONTROL, pfit_control);
}

/**
 * Detect the LVDS connection.
 *
 * This always returns CONNECTOR_STATUS_CONNECTED.  This connector should only have
 * been set up if the LVDS was actually connected anyway.
 */
static enum drm_connector_status intel_lvds_detect(struct drm_connector *connector)
{
	return connector_status_connected;
}

/**
 * Return the list of DDC modes if available, or the BIOS fixed mode otherwise.
 */
static int intel_lvds_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct intel_output *intel_output = to_intel_output(connector);
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = 0;

	ret = intel_ddc_get_modes(intel_output);

	if (ret)
		return ret;

	/* Didn't get an EDID, so
	 * Set wide sync ranges so we get all modes
	 * handed to valid_mode for checking
	 */
	connector->display_info.min_vfreq = 0;
	connector->display_info.max_vfreq = 200;
	connector->display_info.min_hfreq = 0;
	connector->display_info.max_hfreq = 200;

	if (dev_priv->panel_fixed_mode != NULL) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, dev_priv->panel_fixed_mode);
		drm_mode_probed_add(connector, mode);

		return 1;
	}

	return 0;
}

/**
 * intel_lvds_destroy - unregister and free LVDS structures
 * @connector: connector to free
 *
 * Unregister the DDC bus for this connector then free the driver private
 * structure.
 */
static void intel_lvds_destroy(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	if (intel_output->ddc_bus)
		intel_i2c_destroy(intel_output->ddc_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int intel_lvds_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	return 0;
}

static const struct drm_encoder_helper_funcs intel_lvds_helper_funcs = {
	.dpms = intel_lvds_dpms,
	.mode_fixup = intel_lvds_mode_fixup,
	.prepare = intel_lvds_prepare,
	.mode_set = intel_lvds_mode_set,
	.commit = intel_lvds_commit,
};

static const struct drm_connector_helper_funcs intel_lvds_connector_helper_funcs = {
	.get_modes = intel_lvds_get_modes,
	.mode_valid = intel_lvds_mode_valid,
	.best_encoder = intel_best_encoder,
};

static const struct drm_connector_funcs intel_lvds_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = intel_lvds_save,
	.restore = intel_lvds_restore,
	.detect = intel_lvds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_lvds_set_property,
	.destroy = intel_lvds_destroy,
};


static void intel_lvds_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_lvds_enc_funcs = {
	.destroy = intel_lvds_enc_destroy,
};

static int __init intel_no_lvds_dmi_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_KMS(I915_LVDS,
		      "Skipping LVDS initialization for %s\n", id->ident);
	return 1;
}

/* These systems claim to have LVDS, but really don't */
static const struct dmi_system_id intel_no_lvds[] = {
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini1,1"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core 2 series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini2,1"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "MSI IM-945GSE-A",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MSI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "A9830IMS"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Dell Studio Hybrid",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Studio Hybrid 140g"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "AOpen Mini PC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AOpen"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i965GMx-IF"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Aopen i945GTt-VFA",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_VERSION, "AO00001JW"),
		},
	},

	{ }	/* terminating entry */
};

/**
 * intel_lvds_init - setup LVDS connectors on this device
 * @dev: drm device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void intel_lvds_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_output *intel_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_display_mode *scan; /* *modes, *bios_mode; */
	struct drm_crtc *crtc;
	u32 lvds;
	int pipe, gpio = GPIOC;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds))
		return;

	if (IS_IGDNG(dev)) {
		if ((I915_READ(PCH_LVDS) & LVDS_DETECTED) == 0)
			return;
		gpio = PCH_GPIOC;
	}

	intel_output = kzalloc(sizeof(struct intel_output), GFP_KERNEL);
	if (!intel_output) {
		return;
	}

	connector = &intel_output->base;
	encoder = &intel_output->enc;
	drm_connector_init(dev, &intel_output->base, &intel_lvds_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, &intel_output->enc, &intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS);

	drm_mode_connector_attach_encoder(&intel_output->base, &intel_output->enc);
	intel_output->type = INTEL_OUTPUT_LVDS;

	drm_encoder_helper_add(encoder, &intel_lvds_helper_funcs);
	drm_connector_helper_add(connector, &intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;


	/*
	 * LVDS discovery:
	 * 1) check for EDID on DDC
	 * 2) check for VBT data
	 * 3) check to see if LVDS is already on
	 *    if none of the above, no panel
	 * 4) make sure lid is open
	 *    if closed, act like it's not there for now
	 */

	/* Set up the DDC bus. */
	intel_output->ddc_bus = intel_i2c_create(dev, gpio, "LVDSDDC_C");
	if (!intel_output->ddc_bus) {
		dev_printk(KERN_ERR, &dev->pdev->dev, "DDC bus registration "
			   "failed.\n");
		goto failed;
	}

	/*
	 * Attempt to get the fixed panel mode from DDC.  Assume that the
	 * preferred mode is the right one.
	 */
	intel_ddc_get_modes(intel_output);

	list_for_each_entry(scan, &connector->probed_modes, head) {
		mutex_lock(&dev->mode_config.mutex);
		if (scan->type & DRM_MODE_TYPE_PREFERRED) {
			dev_priv->panel_fixed_mode =
				drm_mode_duplicate(dev, scan);
			mutex_unlock(&dev->mode_config.mutex);
			goto out;
		}
		mutex_unlock(&dev->mode_config.mutex);
	}

	/* Failed to get EDID, what about VBT? */
	if (dev_priv->lfp_lvds_vbt_mode) {
		mutex_lock(&dev->mode_config.mutex);
		dev_priv->panel_fixed_mode =
			drm_mode_duplicate(dev, dev_priv->lfp_lvds_vbt_mode);
		mutex_unlock(&dev->mode_config.mutex);
		if (dev_priv->panel_fixed_mode) {
			dev_priv->panel_fixed_mode->type |=
				DRM_MODE_TYPE_PREFERRED;
			goto out;
		}
	}

	/*
	 * If we didn't get EDID, try checking if the panel is already turned
	 * on.  If so, assume that whatever is currently programmed is the
	 * correct mode.
	 */

	/* IGDNG: FIXME if still fail, not try pipe mode now */
	if (IS_IGDNG(dev))
		goto failed;

	lvds = I915_READ(LVDS);
	pipe = (lvds & LVDS_PIPEB_SELECT) ? 1 : 0;
	crtc = intel_get_crtc_from_pipe(dev, pipe);

	if (crtc && (lvds & LVDS_PORT_EN)) {
		dev_priv->panel_fixed_mode = intel_crtc_mode_get(dev, crtc);
		if (dev_priv->panel_fixed_mode) {
			dev_priv->panel_fixed_mode->type |=
				DRM_MODE_TYPE_PREFERRED;
			goto out;
		}
	}

	/* If we still don't have a mode after all that, give up. */
	if (!dev_priv->panel_fixed_mode)
		goto failed;

out:
	if (IS_IGDNG(dev)) {
		u32 pwm;
		/* make sure PWM is enabled */
		pwm = I915_READ(BLC_PWM_CPU_CTL2);
		pwm |= (PWM_ENABLE | PWM_PIPE_B);
		I915_WRITE(BLC_PWM_CPU_CTL2, pwm);

		pwm = I915_READ(BLC_PWM_PCH_CTL1);
		pwm |= PWM_PCH_ENABLE;
		I915_WRITE(BLC_PWM_PCH_CTL1, pwm);
	}
	drm_sysfs_connector_add(connector);
	return;

failed:
	DRM_DEBUG_KMS(I915_LVDS, "No LVDS modes found, disabling.\n");
	if (intel_output->ddc_bus)
		intel_i2c_destroy(intel_output->ddc_bus);
	drm_connector_cleanup(connector);
	kfree(connector);
}
