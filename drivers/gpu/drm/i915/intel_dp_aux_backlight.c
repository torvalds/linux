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

#include "intel_drv.h"

static void set_aux_backlight_enable(struct intel_dp *intel_dp, bool enable)
{
	uint8_t reg_val = 0;

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
static uint32_t intel_dp_aux_get_backlight(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	uint8_t read_val[2] = { 0x0 };
	uint16_t level = 0;

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
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	uint8_t vals[2] = { 0x0 };

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
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	int freq, fxp, fxp_min, fxp_max, fxp_actual, f = 1;
	u8 pn, pn_min, pn_max;

	/* Find desired value of (F x P)
	 * Note that, if F x P is out of supported range, the maximum value or
	 * minimum value will applied automatically. So no need to check that.
	 */
	freq = dev_priv->vbt.backlight.pwm_freq_hz;
	DRM_DEBUG_KMS("VBT defined backlight frequency %u Hz\n", freq);
	if (!freq) {
		DRM_DEBUG_KMS("Use panel default backlight frequency\n");
		return false;
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
		return false;
	}
	if (drm_dp_dpcd_readb(&intel_dp->aux,
			       DP_EDP_PWMGEN_BIT_COUNT_CAP_MAX, &pn_max) != 1) {
		DRM_DEBUG_KMS("Failed to read pwmgen bit count cap max\n");
		return false;
	}
	pn_min &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
	pn_max &= DP_EDP_PWMGEN_BIT_COUNT_MASK;

	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);
	if (fxp_min < (1 << pn_min) || (255 << pn_max) < fxp_max) {
		DRM_DEBUG_KMS("VBT defined backlight frequency out of range\n");
		return false;
	}

	for (pn = pn_max; pn >= pn_min; pn--) {
		f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
		fxp_actual = f << pn;
		if (fxp_min <= fxp_actual && fxp_actual <= fxp_max)
			break;
	}

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_EDP_PWMGEN_BIT_COUNT, pn) < 0) {
		DRM_DEBUG_KMS("Failed to write aux pwmgen bit count\n");
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
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	uint8_t dpcd_buf, new_dpcd_buf, edp_backlight_mode;

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

	set_aux_backlight_enable(intel_dp, true);
	intel_dp_aux_set_backlight(conn_state, connector->panel.backlight.level);
}

static void intel_dp_aux_disable_backlight(const struct drm_connector_state *old_conn_state)
{
	set_aux_backlight_enable(enc_to_intel_dp(old_conn_state->best_encoder), false);
}

static int intel_dp_aux_setup_backlight(struct intel_connector *connector,
					enum pipe pipe)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	struct intel_panel *panel = &connector->panel;

	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT)
		panel->backlight.max = 0xFFFF;
	else
		panel->backlight.max = 0xFF;

	panel->backlight.min = 0;
	panel->backlight.level = intel_dp_aux_get_backlight(connector);

	panel->backlight.enabled = panel->backlight.level != 0;

	return 0;
}

static bool
intel_dp_aux_display_control_capable(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);

	/* Check the eDP Display control capabilities registers to determine if
	 * the panel can support backlight control over the aux channel
	 */
	if ((intel_dp->edp_dpcd[1] & DP_EDP_TCON_BACKLIGHT_ADJUSTMENT_CAP) &&
	    (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_AUX_SET_CAP)) {
		DRM_DEBUG_KMS("AUX Backlight Control Supported!\n");
		return true;
	}
	return false;
}

/*
 * Heuristic function whether we should use AUX for backlight adjustment or not.
 *
 * We should use AUX for backlight brightness adjustment if panel doesn't this
 * via PWM pin or using AUX is better than using PWM pin.
 *
 * The heuristic to determine that using AUX pin is better than using PWM pin is
 * that the panel support any of the feature list here.
 * - Regional backlight brightness adjustment
 * - Backlight PWM frequency set
 * - More than 8 bits resolution of brightness level
 * - Backlight enablement via AUX and not by BL_ENABLE pin
 *
 * If all above are not true, assume that using PWM pin is better.
 */
static bool
intel_dp_aux_display_control_heuristic(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&connector->encoder->base);
	uint8_t reg_val;

	/* Panel doesn't support adjusting backlight brightness via PWN pin */
	if (!(intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_PWM_PIN_CAP))
		return true;

	/* Panel supports regional backlight brightness adjustment */
	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_EDP_GENERAL_CAP_3,
			      &reg_val) != 1) {
		DRM_DEBUG_KMS("Failed to read DPCD register 0x%x\n",
			       DP_EDP_GENERAL_CAP_3);
		return false;
	}
	if (reg_val > 0)
		return true;

	/* Panel supports backlight PWM frequency set */
	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_FREQ_AUX_SET_CAP)
		return true;

	/* Panel supports more than 8 bits resolution of brightness level */
	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT)
		return true;

	/* Panel supports enabling backlight via AUX but not by BL_ENABLE pin */
	if ((intel_dp->edp_dpcd[1] & DP_EDP_BACKLIGHT_AUX_ENABLE_CAP) &&
	    !(intel_dp->edp_dpcd[1] & DP_EDP_BACKLIGHT_PIN_ENABLE_CAP))
		return true;

	return false;

}

int intel_dp_aux_init_backlight_funcs(struct intel_connector *intel_connector)
{
	struct intel_panel *panel = &intel_connector->panel;

	if (!i915.enable_dpcd_backlight)
		return -ENODEV;

	if (!intel_dp_aux_display_control_capable(intel_connector))
		return -ENODEV;

	if (i915.enable_dpcd_backlight == -1 &&
	    !intel_dp_aux_display_control_heuristic(intel_connector))
		return -ENODEV;

	panel->backlight.setup = intel_dp_aux_setup_backlight;
	panel->backlight.enable = intel_dp_aux_enable_backlight;
	panel->backlight.disable = intel_dp_aux_disable_backlight;
	panel->backlight.set = intel_dp_aux_set_backlight;
	panel->backlight.get = intel_dp_aux_get_backlight;

	return 0;
}
