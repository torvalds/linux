/*
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "intel_display_types.h"
#include "intel_dp_aux_backlight.h"

static void set_aux_backlight_enable(struct intel_dp *intel_dp, bool enable)
{
	u8 reg_val = 0;

	/* Early return when display use other mechanism to enable backlight. */
	if (!(intel_dp->edp_dpcd[1] & DP_EDP_BACKLIGHT_AUX_ENABLE_CAP))
		return;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
			      &reg_val) < 0) {
		DRM_DEBUG_KMS("Failed to read DPCD register 0x%x\n",
			      DP_EDP_DISPLAY_CONTROL_REGISTER);
		return;
	}
	if (enable)
		reg_val |= DP_EDP_BACKLIGHT_ENABLE;
	else
		reg_val &= ~(DP_EDP_BACKLIGHT_ENABLE);

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
			       reg_val) != 1) {
		DRM_DEBUG_KMS("Failed to %s aux backlight\n",
			      enable ? "enable" : "disable");
	}
}

/*
 * Read the current backlight value from DPCD register(s) based
 * on if 8-bit(MSB) or 16-bit(MSB and LSB) values are supported
 */
static u32 intel_dp_aux_get_backlight(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	u8 read_val[2] = { 0x0 };
	u8 mode_reg;
	u16 level = 0;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_EDP_BACKLIGHT_MODE_SET_REGISTER,
			      &mode_reg) != 1) {
		DRM_DEBUG_KMS("Failed to read the DPCD register 0x%x\n",
			      DP_EDP_BACKLIGHT_MODE_SET_REGISTER);
		return 0;
	}

	/*
	 * If we're not in DPCD control mode yet, the programmed brightness
	 * value is meaningless and we should assume max brightness
	 */
	if ((mode_reg & DP_EDP_BACKLIGHT_CONTROL_MODE_MASK) !=
	    DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD)
		return connector->panel.backlight.max;

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
			     &read_val, sizeof(read_val)) < 0) {
		DRM_DEBUG_KMS("Failed to read DPCD register 0x%x\n",
			      DP_EDP_BACKLIGHT_BRIGHTNESS_MSB);
		return 0;
	}
	level = read_val[0];
	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT)
		level = (read_val[0] << 8 | read_val[1]);

	return level;
}

/*
 * Sends the current backlight level over the aux channel, checking if its using
 * 8-bit or 16 bit value (MSB and LSB)
 */
static void
intel_dp_aux_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	u8 vals[2] = { 0x0 };

	vals[0] = level;

	/* Write the MSB and/or LSB */
	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT) {
		vals[0] = (level & 0xFF00) >> 8;
		vals[1] = (level & 0xFF);
	}
	if (drm_dp_dpcd_write(&intel_dp->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
			      vals, sizeof(vals)) < 0) {
		DRM_DEBUG_KMS("Failed to write aux backlight level\n");
		return;
	}
}

/*
 * Set PWM Frequency divider to match desired frequency in vbt.
 * The PWM Frequency is calculated as 27Mhz / (F x P).
 * - Where F = PWM Frequency Pre-Divider value programmed by field 7:0 of the
 *             EDP_BACKLIGHT_FREQ_SET register (DPCD Address 00728h)
 * - Where P = 2^Pn, where Pn is the value programmed by field 4:0 of the
 *             EDP_PWMGEN_BIT_COUNT register (DPCD Address 00724h)
 */
static bool intel_dp_aux_set_pwm_freq(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	const u8 pn = connector->panel.backlight.pwmgen_bit_count;
	int freq, fxp, f, fxp_actual, fxp_min, fxp_max;

	freq = dev_priv->vbt.backlight.pwm_freq_hz;
	if (!freq) {
		DRM_DEBUG_KMS("Use panel default backlight frequency\n");
		return false;
	}

	fxp = DIV_ROUND_CLOSEST(KHz(DP_EDP_BACKLIGHT_FREQ_BASE_KHZ), freq);
	f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
	fxp_actual = f << pn;

	/* Ensure frequency is within 25% of desired value */
	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);

	if (fxp_min > fxp_actual || fxp_actual > fxp_max) {
		DRM_DEBUG_KMS("Actual frequency out of range\n");
		return false;
	}

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_EDP_BACKLIGHT_FREQ_SET, (u8) f) < 0) {
		DRM_DEBUG_KMS("Failed to write aux backlight freq\n");
		return false;
	}
	return true;
}

static void intel_dp_aux_enable_backlight(const struct intel_crtc_state *crtc_state,
					  const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_panel *panel = &connector->panel;
	u8 dpcd_buf, new_dpcd_buf, edp_backlight_mode;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			DP_EDP_BACKLIGHT_MODE_SET_REGISTER, &dpcd_buf) != 1) {
		DRM_DEBUG_KMS("Failed to read DPCD register 0x%x\n",
			      DP_EDP_BACKLIGHT_MODE_SET_REGISTER);
		return;
	}

	new_dpcd_buf = dpcd_buf;
	edp_backlight_mode = dpcd_buf & DP_EDP_BACKLIGHT_CONTROL_MODE_MASK;

	switch (edp_backlight_mode) {
	case DP_EDP_BACKLIGHT_CONTROL_MODE_PWM:
	case DP_EDP_BACKLIGHT_CONTROL_MODE_PRESET:
	case DP_EDP_BACKLIGHT_CONTROL_MODE_PRODUCT:
		new_dpcd_buf &= ~DP_EDP_BACKLIGHT_CONTROL_MODE_MASK;
		new_dpcd_buf |= DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD;

		if (drm_dp_dpcd_writeb(&intel_dp->aux,
				       DP_EDP_PWMGEN_BIT_COUNT,
				       panel->backlight.pwmgen_bit_count) < 0)
			DRM_DEBUG_KMS("Failed to write aux pwmgen bit count\n");

		break;

	/* Do nothing when it is already DPCD mode */
	case DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD:
	default:
		break;
	}

	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_FREQ_AUX_SET_CAP)
		if (intel_dp_aux_set_pwm_freq(connector))
			new_dpcd_buf |= DP_EDP_BACKLIGHT_FREQ_AUX_SET_ENABLE;

	if (new_dpcd_buf != dpcd_buf) {
		if (drm_dp_dpcd_writeb(&intel_dp->aux,
			DP_EDP_BACKLIGHT_MODE_SET_REGISTER, new_dpcd_buf) < 0) {
			DRM_DEBUG_KMS("Failed to write aux backlight mode\n");
		}
	}

	intel_dp_aux_set_backlight(conn_state,
				   connector->panel.backlight.level);
	set_aux_backlight_enable(intel_dp, true);
}

static void intel_dp_aux_disable_backlight(const struct drm_connector_state *old_conn_state)
{
	set_aux_backlight_enable(enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder)),
				 false);
}

static u32 intel_dp_aux_calc_max_backlight(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_panel *panel = &connector->panel;
	u32 max_backlight = 0;
	int freq, fxp, fxp_min, fxp_max, fxp_actual, f = 1;
	u8 pn, pn_min, pn_max;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_EDP_PWMGEN_BIT_COUNT, &pn) == 1) {
		pn &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
		max_backlight = (1 << pn) - 1;
	}

	/* Find desired value of (F x P)
	 * Note that, if F x P is out of supported range, the maximum value or
	 * minimum value will applied automatically. So no need to check that.
	 */
	freq = i915->vbt.backlight.pwm_freq_hz;
	DRM_DEBUG_KMS("VBT defined backlight frequency %u Hz\n", freq);
	if (!freq) {
		DRM_DEBUG_KMS("Use panel default backlight frequency\n");
		return max_backlight;
	}

	fxp = DIV_ROUND_CLOSEST(KHz(DP_EDP_BACKLIGHT_FREQ_BASE_KHZ), freq);

	/* Use highest possible value of Pn for more granularity of brightness
	 * adjustment while satifying the conditions below.
	 * - Pn is in the range of Pn_min and Pn_max
	 * - F is in the range of 1 and 255
	 * - FxP is within 25% of desired value.
	 *   Note: 25% is arbitrary value and may need some tweak.
	 */
	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_EDP_PWMGEN_BIT_COUNT_CAP_MIN, &pn_min) != 1) {
		DRM_DEBUG_KMS("Failed to read pwmgen bit count cap min\n");
		return max_backlight;
	}
	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_EDP_PWMGEN_BIT_COUNT_CAP_MAX, &pn_max) != 1) {
		DRM_DEBUG_KMS("Failed to read pwmgen bit count cap max\n");
		return max_backlight;
	}
	pn_min &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
	pn_max &= DP_EDP_PWMGEN_BIT_COUNT_MASK;

	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);
	if (fxp_min < (1 << pn_min) || (255 << pn_max) < fxp_max) {
		DRM_DEBUG_KMS("VBT defined backlight frequency out of range\n");
		return max_backlight;
	}

	for (pn = pn_max; pn >= pn_min; pn--) {
		f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
		fxp_actual = f << pn;
		if (fxp_min <= fxp_actual && fxp_actual <= fxp_max)
			break;
	}

	DRM_DEBUG_KMS("Using eDP pwmgen bit count of %d\n", pn);
	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_EDP_PWMGEN_BIT_COUNT, pn) < 0) {
		DRM_DEBUG_KMS("Failed to write aux pwmgen bit count\n");
		return max_backlight;
	}
	panel->backlight.pwmgen_bit_count = pn;

	max_backlight = (1 << pn) - 1;

	return max_backlight;
}

static int intel_dp_aux_setup_backlight(struct intel_connector *connector,
					enum pipe pipe)
{
	struct intel_panel *panel = &connector->panel;

	panel->backlight.max = intel_dp_aux_calc_max_backlight(connector);
	if (!panel->backlight.max)
		return -ENODEV;

	panel->backlight.min = 0;
	panel->backlight.level = intel_dp_aux_get_backlight(connector);
	panel->backlight.enabled = panel->backlight.level != 0;

	return 0;
}

static bool
intel_dp_aux_display_control_capable(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	/* Check the eDP Display control capabilities registers to determine if
	 * the panel can support backlight control over the aux channel
	 */
	if (intel_dp->edp_dpcd[1] & DP_EDP_TCON_BACKLIGHT_ADJUSTMENT_CAP &&
	    (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_AUX_SET_CAP) &&
	    !(intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_PWM_PIN_CAP)) {
		DRM_DEBUG_KMS("AUX Backlight Control Supported!\n");
		return true;
	}
	return false;
}

int intel_dp_aux_init_backlight_funcs(struct intel_connector *intel_connector)
{
	struct intel_panel *panel = &intel_connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_connector->encoder);
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (i915_modparams.enable_dpcd_backlight == 0 ||
	    !intel_dp_aux_display_control_capable(intel_connector))
		return -ENODEV;

	/*
	 * There are a lot of machines that don't advertise the backlight
	 * control interface to use properly in their VBIOS, :\
	 */
	if (dev_priv->vbt.backlight.type !=
	    INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE &&
	    !drm_dp_has_quirk(&intel_dp->desc, intel_dp->edid_quirks,
			      DP_QUIRK_FORCE_DPCD_BACKLIGHT)) {
		DRM_DEV_INFO(dev->dev,
			     "Panel advertises DPCD backlight support, but "
			     "VBT disagrees. If your backlight controls "
			     "don't work try booting with "
			     "i915.enable_dpcd_backlight=1. If your machine "
			     "needs this, please file a _new_ bug report on "
			     "drm/i915, see " FDO_BUG_URL " for details.\n");
		return -ENODEV;
	}

	panel->backlight.setup = intel_dp_aux_setup_backlight;
	panel->backlight.enable = intel_dp_aux_enable_backlight;
	panel->backlight.disable = intel_dp_aux_disable_backlight;
	panel->backlight.set = intel_dp_aux_set_backlight;
	panel->backlight.get = intel_dp_aux_get_backlight;

	return 0;
}
