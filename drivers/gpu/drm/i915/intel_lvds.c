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

#include <acpi/button.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include <linux/acpi.h>

/* Private structure for the integrated LVDS support */
struct intel_lvds_priv {
	int fitting_mode;
	u32 pfit_control;
	u32 pfit_pgm_ratios;
};

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
	/*
	 * float point operation is not supported . So the PANEL_RATIO_FACTOR
	 * is defined, which can avoid the float point computation when
	 * calculating the panel ratio.
	 */
#define PANEL_RATIO_FACTOR 8192
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	struct drm_encoder *tmp_encoder;
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_lvds_priv *lvds_priv = intel_output->dev_priv;
	u32 pfit_control = 0, pfit_pgm_ratios = 0;
	int left_border = 0, right_border = 0, top_border = 0;
	int bottom_border = 0;
	bool border = 0;
	int panel_ratio, desired_ratio, vert_scale, horiz_scale;
	int horiz_ratio, vert_ratio;
	u32 hsync_width, vsync_width;
	u32 hblank_width, vblank_width;
	u32 hsync_pos, vsync_pos;

	/* Should never happen!! */
	if (!IS_I965G(dev) && intel_crtc->pipe == 0) {
		DRM_ERROR("Can't support LVDS on pipe A\n");
		return false;
	}

	/* Should never happen!! */
	list_for_each_entry(tmp_encoder, &dev->mode_config.encoder_list, head) {
		if (tmp_encoder != encoder && tmp_encoder->crtc == encoder->crtc) {
			DRM_ERROR("Can't enable LVDS and another "
			       "encoder on the same pipe\n");
			return false;
		}
	}
	/* If we don't have a panel mode, there is nothing we can do */
	if (dev_priv->panel_fixed_mode == NULL)
		return true;
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

	/* Make sure pre-965s set dither correctly */
	if (!IS_I965G(dev)) {
		if (dev_priv->panel_wants_dither || dev_priv->lvds_dither)
			pfit_control |= PANEL_8TO6_DITHER_ENABLE;
	}

	/* Native modes don't need fitting */
	if (adjusted_mode->hdisplay == mode->hdisplay &&
			adjusted_mode->vdisplay == mode->vdisplay) {
		pfit_pgm_ratios = 0;
		border = 0;
		goto out;
	}

	/* full screen scale for now */
	if (IS_IGDNG(dev))
		goto out;

	/* 965+ wants fuzzy fitting */
	if (IS_I965G(dev))
		pfit_control |= (intel_crtc->pipe << PFIT_PIPE_SHIFT) |
					PFIT_FILTER_FUZZY;

	hsync_width = adjusted_mode->crtc_hsync_end -
					adjusted_mode->crtc_hsync_start;
	vsync_width = adjusted_mode->crtc_vsync_end -
					adjusted_mode->crtc_vsync_start;
	hblank_width = adjusted_mode->crtc_hblank_end -
					adjusted_mode->crtc_hblank_start;
	vblank_width = adjusted_mode->crtc_vblank_end -
					adjusted_mode->crtc_vblank_start;
	/*
	 * Deal with panel fitting options. Figure out how to stretch the
	 * image based on its aspect ratio & the current panel fitting mode.
	 */
	panel_ratio = adjusted_mode->hdisplay * PANEL_RATIO_FACTOR /
				adjusted_mode->vdisplay;
	desired_ratio = mode->hdisplay * PANEL_RATIO_FACTOR /
				mode->vdisplay;
	/*
	 * Enable automatic panel scaling for non-native modes so that they fill
	 * the screen.  Should be enabled before the pipe is enabled, according
	 * to register description and PRM.
	 * Change the value here to see the borders for debugging
	 */
	if (!IS_IGDNG(dev)) {
		I915_WRITE(BCLRPAT_A, 0);
		I915_WRITE(BCLRPAT_B, 0);
	}

	switch (lvds_priv->fitting_mode) {
	case DRM_MODE_SCALE_CENTER:
		/*
		 * For centered modes, we have to calculate border widths &
		 * heights and modify the values programmed into the CRTC.
		 */
		left_border = (adjusted_mode->hdisplay - mode->hdisplay) / 2;
		right_border = left_border;
		if (mode->hdisplay & 1)
			right_border++;
		top_border = (adjusted_mode->vdisplay - mode->vdisplay) / 2;
		bottom_border = top_border;
		if (mode->vdisplay & 1)
			bottom_border++;
		/* Set active & border values */
		adjusted_mode->crtc_hdisplay = mode->hdisplay;
		/* Keep the boder be even */
		if (right_border & 1)
			right_border++;
		/* use the border directly instead of border minuse one */
		adjusted_mode->crtc_hblank_start = mode->hdisplay +
						right_border;
		/* keep the blank width constant */
		adjusted_mode->crtc_hblank_end =
			adjusted_mode->crtc_hblank_start + hblank_width;
		/* get the hsync pos relative to hblank start */
		hsync_pos = (hblank_width - hsync_width) / 2;
		/* keep the hsync pos be even */
		if (hsync_pos & 1)
			hsync_pos++;
		adjusted_mode->crtc_hsync_start =
				adjusted_mode->crtc_hblank_start + hsync_pos;
		/* keep the hsync width constant */
		adjusted_mode->crtc_hsync_end =
				adjusted_mode->crtc_hsync_start + hsync_width;
		adjusted_mode->crtc_vdisplay = mode->vdisplay;
		/* use the border instead of border minus one */
		adjusted_mode->crtc_vblank_start = mode->vdisplay +
						bottom_border;
		/* keep the vblank width constant */
		adjusted_mode->crtc_vblank_end =
				adjusted_mode->crtc_vblank_start + vblank_width;
		/* get the vsync start postion relative to vblank start */
		vsync_pos = (vblank_width - vsync_width) / 2;
		adjusted_mode->crtc_vsync_start =
				adjusted_mode->crtc_vblank_start + vsync_pos;
		/* keep the vsync width constant */
		adjusted_mode->crtc_vsync_end =
				adjusted_mode->crtc_vsync_start + vsync_width;
		border = 1;
		break;
	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the spect ratio */
		pfit_control |= PFIT_ENABLE;
		if (IS_I965G(dev)) {
			/* 965+ is easy, it does everything in hw */
			if (panel_ratio > desired_ratio)
				pfit_control |= PFIT_SCALING_PILLAR;
			else if (panel_ratio < desired_ratio)
				pfit_control |= PFIT_SCALING_LETTER;
			else
				pfit_control |= PFIT_SCALING_AUTO;
		} else {
			/*
			 * For earlier chips we have to calculate the scaling
			 * ratio by hand and program it into the
			 * PFIT_PGM_RATIO register
			 */
			u32 horiz_bits, vert_bits, bits = 12;
			horiz_ratio = mode->hdisplay * PANEL_RATIO_FACTOR/
						adjusted_mode->hdisplay;
			vert_ratio = mode->vdisplay * PANEL_RATIO_FACTOR/
						adjusted_mode->vdisplay;
			horiz_scale = adjusted_mode->hdisplay *
					PANEL_RATIO_FACTOR / mode->hdisplay;
			vert_scale = adjusted_mode->vdisplay *
					PANEL_RATIO_FACTOR / mode->vdisplay;

			/* retain aspect ratio */
			if (panel_ratio > desired_ratio) { /* Pillar */
				u32 scaled_width;
				scaled_width = mode->hdisplay * vert_scale /
						PANEL_RATIO_FACTOR;
				horiz_ratio = vert_ratio;
				pfit_control |= (VERT_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_INTERP_BILINEAR);
				/* Pillar will have left/right borders */
				left_border = (adjusted_mode->hdisplay -
						scaled_width) / 2;
				right_border = left_border;
				if (mode->hdisplay & 1) /* odd resolutions */
					right_border++;
				/* keep the border be even */
				if (right_border & 1)
					right_border++;
				adjusted_mode->crtc_hdisplay = scaled_width;
				/* use border instead of border minus one */
				adjusted_mode->crtc_hblank_start =
					scaled_width + right_border;
				/* keep the hblank width constant */
				adjusted_mode->crtc_hblank_end =
					adjusted_mode->crtc_hblank_start +
							hblank_width;
				/*
				 * get the hsync start pos relative to
				 * hblank start
				 */
				hsync_pos = (hblank_width - hsync_width) / 2;
				/* keep the hsync_pos be even */
				if (hsync_pos & 1)
					hsync_pos++;
				adjusted_mode->crtc_hsync_start =
					adjusted_mode->crtc_hblank_start +
							hsync_pos;
				/* keept hsync width constant */
				adjusted_mode->crtc_hsync_end =
					adjusted_mode->crtc_hsync_start +
							hsync_width;
				border = 1;
			} else if (panel_ratio < desired_ratio) { /* letter */
				u32 scaled_height = mode->vdisplay *
					horiz_scale / PANEL_RATIO_FACTOR;
				vert_ratio = horiz_ratio;
				pfit_control |= (HORIZ_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_INTERP_BILINEAR);
				/* Letterbox will have top/bottom border */
				top_border = (adjusted_mode->vdisplay -
					scaled_height) / 2;
				bottom_border = top_border;
				if (mode->vdisplay & 1)
					bottom_border++;
				adjusted_mode->crtc_vdisplay = scaled_height;
				/* use border instead of border minus one */
				adjusted_mode->crtc_vblank_start =
					scaled_height + bottom_border;
				/* keep the vblank width constant */
				adjusted_mode->crtc_vblank_end =
					adjusted_mode->crtc_vblank_start +
							vblank_width;
				/*
				 * get the vsync start pos relative to
				 * vblank start
				 */
				vsync_pos = (vblank_width - vsync_width) / 2;
				adjusted_mode->crtc_vsync_start =
					adjusted_mode->crtc_vblank_start +
							vsync_pos;
				/* keep the vsync width constant */
				adjusted_mode->crtc_vsync_end =
					adjusted_mode->crtc_vsync_start +
							vsync_width;
				border = 1;
			} else {
			/* Aspects match, Let hw scale both directions */
				pfit_control |= (VERT_AUTO_SCALE |
						 HORIZ_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_INTERP_BILINEAR);
			}
			horiz_bits = (1 << bits) * horiz_ratio /
					PANEL_RATIO_FACTOR;
			vert_bits = (1 << bits) * vert_ratio /
					PANEL_RATIO_FACTOR;
			pfit_pgm_ratios =
				((vert_bits << PFIT_VERT_SCALE_SHIFT) &
						PFIT_VERT_SCALE_MASK) |
				((horiz_bits << PFIT_HORIZ_SCALE_SHIFT) &
						PFIT_HORIZ_SCALE_MASK);
		}
		break;

	case DRM_MODE_SCALE_FULLSCREEN:
		/*
		 * Full scaling, even if it changes the aspect ratio.
		 * Fortunately this is all done for us in hw.
		 */
		pfit_control |= PFIT_ENABLE;
		if (IS_I965G(dev))
			pfit_control |= PFIT_SCALING_AUTO;
		else
			pfit_control |= (VERT_AUTO_SCALE | HORIZ_AUTO_SCALE |
					 VERT_INTERP_BILINEAR |
					 HORIZ_INTERP_BILINEAR);
		break;
	default:
		break;
	}

out:
	lvds_priv->pfit_control = pfit_control;
	lvds_priv->pfit_pgm_ratios = pfit_pgm_ratios;
	/*
	 * When there exists the border, it means that the LVDS_BORDR
	 * should be enabled.
	 */
	if (border)
		dev_priv->lvds_border_bits |= LVDS_BORDER_ENABLE;
	else
		dev_priv->lvds_border_bits &= ~(LVDS_BORDER_ENABLE);
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
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_lvds_priv *lvds_priv = intel_output->dev_priv;

	/*
	 * The LVDS pin pair will already have been turned on in the
	 * intel_crtc_mode_set since it has a large impact on the DPLL
	 * settings.
	 */

	if (IS_IGDNG(dev))
		return;

	/*
	 * Enable automatic panel scaling so that non-native modes fill the
	 * screen.  Should be enabled before the pipe is enabled, according to
	 * register description and PRM.
	 */
	I915_WRITE(PFIT_PGM_RATIOS, lvds_priv->pfit_pgm_ratios);
	I915_WRITE(PFIT_CONTROL, lvds_priv->pfit_control);
}

/* Some lid devices report incorrect lid status, assume they're connected */
static const struct dmi_system_id bad_lid_status[] = {
	{
		.ident = "Aspire One",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire one"),
		},
	},
	{ }
};

/**
 * Detect the LVDS connection.
 *
 * Since LVDS doesn't have hotlug, we use the lid as a proxy.  Open means
 * connected and closed means disconnected.  We also send hotplug events as
 * needed, using lid status notification from the input layer.
 */
static enum drm_connector_status intel_lvds_detect(struct drm_connector *connector)
{
	enum drm_connector_status status = connector_status_connected;

	if (!acpi_lid_open() && !dmi_check_system(bad_lid_status))
		status = connector_status_disconnected;

	return status;
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

/*
 * Lid events. Note the use of 'modeset_on_lid':
 *  - we set it on lid close, and reset it on open
 *  - we use it as a "only once" bit (ie we ignore
 *    duplicate events where it was already properly
 *    set/reset)
 *  - the suspend/resume paths will also set it to
 *    zero, since they restore the mode ("lid open").
 */
static int intel_lid_notify(struct notifier_block *nb, unsigned long val,
			    void *unused)
{
	struct drm_i915_private *dev_priv =
		container_of(nb, struct drm_i915_private, lid_notifier);
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector = dev_priv->int_lvds_connector;

	/*
	 * check and update the status of LVDS connector after receiving
	 * the LID nofication event.
	 */
	if (connector)
		connector->status = connector->funcs->detect(connector);
	if (!acpi_lid_open()) {
		dev_priv->modeset_on_lid = 1;
		return NOTIFY_OK;
	}

	if (!dev_priv->modeset_on_lid)
		return NOTIFY_OK;

	dev_priv->modeset_on_lid = 0;

	mutex_lock(&dev->mode_config.mutex);
	drm_helper_resume_force_mode(dev);
	mutex_unlock(&dev->mode_config.mutex);

	return NOTIFY_OK;
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
	struct drm_device *dev = connector->dev;
	struct intel_output *intel_output = to_intel_output(connector);
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (intel_output->ddc_bus)
		intel_i2c_destroy(intel_output->ddc_bus);
	if (dev_priv->lid_notifier.notifier_call)
		acpi_lid_notifier_unregister(&dev_priv->lid_notifier);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int intel_lvds_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	struct drm_device *dev = connector->dev;
	struct intel_output *intel_output =
			to_intel_output(connector);

	if (property == dev->mode_config.scaling_mode_property &&
				connector->encoder) {
		struct drm_crtc *crtc = connector->encoder->crtc;
		struct intel_lvds_priv *lvds_priv = intel_output->dev_priv;
		if (value == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return 0;
		}
		if (lvds_priv->fitting_mode == value) {
			/* the LVDS scaling property is not changed */
			return 0;
		}
		lvds_priv->fitting_mode = value;
		if (crtc && crtc->enabled) {
			/*
			 * If the CRTC is enabled, the display will be changed
			 * according to the new panel fitting mode.
			 */
			drm_crtc_helper_set_mode(crtc, &crtc->mode,
				crtc->x, crtc->y, crtc->fb);
		}
	}

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
	DRM_DEBUG_KMS("Skipping LVDS initialization for %s\n", id->ident);
	return 1;
}

/* These systems claim to have LVDS, but really don't */
static const struct dmi_system_id intel_no_lvds[] = {
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini1,1"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core 2 series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple"),
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
		.ident = "AOpen Mini PC MP915",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
			DMI_MATCH(DMI_BOARD_NAME, "i915GMx-F"),
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

#ifdef CONFIG_ACPI
/*
 * check_lid_device -- check whether @handle is an ACPI LID device.
 * @handle: ACPI device handle
 * @level : depth in the ACPI namespace tree
 * @context: the number of LID device when we find the device
 * @rv: a return value to fill if desired (Not use)
 */
static acpi_status
check_lid_device(acpi_handle handle, u32 level, void *context,
			void **return_value)
{
	struct acpi_device *acpi_dev;
	int *lid_present = context;

	acpi_dev = NULL;
	/* Get the acpi device for device handle */
	if (acpi_bus_get_device(handle, &acpi_dev) || !acpi_dev) {
		/* If there is no ACPI device for handle, return */
		return AE_OK;
	}

	if (!strncmp(acpi_device_hid(acpi_dev), "PNP0C0D", 7))
		*lid_present = 1;

	return AE_OK;
}

/**
 * check whether there exists the ACPI LID device by enumerating the ACPI
 * device tree.
 */
static int intel_lid_present(void)
{
	int lid_present = 0;

	if (acpi_disabled) {
		/* If ACPI is disabled, there is no ACPI device tree to
		 * check, so assume the LID device would have been present.
		 */
		return 1;
	}

	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				ACPI_UINT32_MAX,
				check_lid_device, &lid_present, NULL);

	return lid_present;
}
#else
static int intel_lid_present(void)
{
	/* In the absence of ACPI built in, assume that the LID device would
	 * have been present.
	 */
	return 1;
}
#endif

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
	struct intel_lvds_priv *lvds_priv;
	u32 lvds;
	int pipe, gpio = GPIOC;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds))
		return;

	/* Assume that any device without an ACPI LID device also doesn't
	 * have an integrated LVDS.  We would be better off parsing the BIOS
	 * to get a reliable indicator, but that code isn't written yet.
	 *
	 * In the case of all-in-one desktops using LVDS that we've seen,
	 * they're using SDVO LVDS.
	 */
	if (!intel_lid_present())
		return;

	if (IS_IGDNG(dev)) {
		if ((I915_READ(PCH_LVDS) & LVDS_DETECTED) == 0)
			return;
		if (dev_priv->edp_support) {
			DRM_DEBUG("disable LVDS for eDP support\n");
			return;
		}
		gpio = PCH_GPIOC;
	}

	intel_output = kzalloc(sizeof(struct intel_output) +
				sizeof(struct intel_lvds_priv), GFP_KERNEL);
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

	intel_output->clone_mask = (1 << INTEL_LVDS_CLONE_BIT);
	intel_output->crtc_mask = (1 << 1);
	drm_encoder_helper_add(encoder, &intel_lvds_helper_funcs);
	drm_connector_helper_add(connector, &intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	lvds_priv = (struct intel_lvds_priv *)(intel_output + 1);
	intel_output->dev_priv = lvds_priv;
	/* create the scaling mode property */
	drm_mode_create_scaling_mode_property(dev);
	/*
	 * the initial panel fitting mode will be FULL_SCREEN.
	 */

	drm_connector_attach_property(&intel_output->base,
				      dev->mode_config.scaling_mode_property,
				      DRM_MODE_SCALE_FULLSCREEN);
	lvds_priv->fitting_mode = DRM_MODE_SCALE_FULLSCREEN;
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
	dev_priv->lid_notifier.notifier_call = intel_lid_notify;
	if (acpi_lid_notifier_register(&dev_priv->lid_notifier)) {
		DRM_DEBUG("lid notifier registration failed\n");
		dev_priv->lid_notifier.notifier_call = NULL;
	}
	/* keep the LVDS connector */
	dev_priv->int_lvds_connector = connector;
	drm_sysfs_connector_add(connector);
	return;

failed:
	DRM_DEBUG_KMS("No LVDS modes found, disabling.\n");
	if (intel_output->ddc_bus)
		intel_i2c_destroy(intel_output->ddc_bus);
	drm_connector_cleanup(connector);
	kfree(intel_output);
}
