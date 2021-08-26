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

/*
 * Laptops with Intel GPUs which have panels that support controlling the
 * backlight through DP AUX can actually use two different interfaces: Intel's
 * proprietary DP AUX backlight interface, and the standard VESA backlight
 * interface. Unfortunately, at the time of writing this a lot of laptops will
 * advertise support for the standard VESA backlight interface when they
 * don't properly support it. However, on these systems the Intel backlight
 * interface generally does work properly. Additionally, these systems will
 * usually just indicate that they use PWM backlight controls in their VBIOS
 * for some reason.
 */

#include "intel_display_types.h"
#include "intel_dp_aux_backlight.h"
#include "intel_panel.h"

/* TODO:
 * Implement HDR, right now we just implement the bare minimum to bring us back into SDR mode so we
 * can make people's backlights work in the mean time
 */

/*
 * DP AUX registers for Intel's proprietary HDR backlight interface. We define
 * them here since we'll likely be the only driver to ever use these.
 */
#define INTEL_EDP_HDR_TCON_CAP0                                        0x340

#define INTEL_EDP_HDR_TCON_CAP1                                        0x341
# define INTEL_EDP_HDR_TCON_2084_DECODE_CAP                           BIT(0)
# define INTEL_EDP_HDR_TCON_2020_GAMUT_CAP                            BIT(1)
# define INTEL_EDP_HDR_TCON_TONE_MAPPING_CAP                          BIT(2)
# define INTEL_EDP_HDR_TCON_SEGMENTED_BACKLIGHT_CAP                   BIT(3)
# define INTEL_EDP_HDR_TCON_BRIGHTNESS_NITS_CAP                       BIT(4)
# define INTEL_EDP_HDR_TCON_OPTIMIZATION_CAP                          BIT(5)
# define INTEL_EDP_HDR_TCON_SDP_COLORIMETRY_CAP                       BIT(6)
# define INTEL_EDP_HDR_TCON_SRGB_TO_PANEL_GAMUT_CONVERSION_CAP        BIT(7)

#define INTEL_EDP_HDR_TCON_CAP2                                        0x342
# define INTEL_EDP_SDR_TCON_BRIGHTNESS_AUX_CAP                        BIT(0)

#define INTEL_EDP_HDR_TCON_CAP3                                        0x343

#define INTEL_EDP_HDR_GETSET_CTRL_PARAMS                               0x344
# define INTEL_EDP_HDR_TCON_2084_DECODE_ENABLE                        BIT(0)
# define INTEL_EDP_HDR_TCON_2020_GAMUT_ENABLE                         BIT(1)
# define INTEL_EDP_HDR_TCON_TONE_MAPPING_ENABLE                       BIT(2) /* Pre-TGL+ */
# define INTEL_EDP_HDR_TCON_SEGMENTED_BACKLIGHT_ENABLE                BIT(3)
# define INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE                     BIT(4)
# define INTEL_EDP_HDR_TCON_SRGB_TO_PANEL_GAMUT_ENABLE                BIT(5)
/* Bit 6 is reserved */
# define INTEL_EDP_HDR_TCON_SDP_COLORIMETRY_ENABLE                    BIT(7)

#define INTEL_EDP_HDR_CONTENT_LUMINANCE                                0x346 /* Pre-TGL+ */
#define INTEL_EDP_HDR_PANEL_LUMINANCE_OVERRIDE                         0x34A
#define INTEL_EDP_SDR_LUMINANCE_LEVEL                                  0x352
#define INTEL_EDP_BRIGHTNESS_NITS_LSB                                  0x354
#define INTEL_EDP_BRIGHTNESS_NITS_MSB                                  0x355
#define INTEL_EDP_BRIGHTNESS_DELAY_FRAMES                              0x356
#define INTEL_EDP_BRIGHTNESS_PER_FRAME_STEPS                           0x357

#define INTEL_EDP_BRIGHTNESS_OPTIMIZATION_0                            0x358
# define INTEL_EDP_TCON_USAGE_MASK                             GENMASK(0, 3)
# define INTEL_EDP_TCON_USAGE_UNKNOWN                                    0x0
# define INTEL_EDP_TCON_USAGE_DESKTOP                                    0x1
# define INTEL_EDP_TCON_USAGE_FULL_SCREEN_MEDIA                          0x2
# define INTEL_EDP_TCON_USAGE_FULL_SCREEN_GAMING                         0x3
# define INTEL_EDP_TCON_POWER_MASK                                    BIT(4)
# define INTEL_EDP_TCON_POWER_DC                                    (0 << 4)
# define INTEL_EDP_TCON_POWER_AC                                    (1 << 4)
# define INTEL_EDP_TCON_OPTIMIZATION_STRENGTH_MASK             GENMASK(5, 7)

#define INTEL_EDP_BRIGHTNESS_OPTIMIZATION_1                            0x359

/* Intel EDP backlight callbacks */
static bool
intel_dp_aux_supports_hdr_backlight(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	struct drm_dp_aux *aux = &intel_dp->aux;
	struct intel_panel *panel = &connector->panel;
	int ret;
	u8 tcon_cap[4];

	ret = drm_dp_dpcd_read(aux, INTEL_EDP_HDR_TCON_CAP0, tcon_cap, sizeof(tcon_cap));
	if (ret < 0)
		return false;

	if (!(tcon_cap[1] & INTEL_EDP_HDR_TCON_BRIGHTNESS_NITS_CAP))
		return false;

	if (tcon_cap[0] >= 1) {
		drm_dbg_kms(&i915->drm, "Detected Intel HDR backlight interface version %d\n",
			    tcon_cap[0]);
	} else {
		drm_dbg_kms(&i915->drm, "Detected unsupported HDR backlight interface version %d\n",
			    tcon_cap[0]);
		return false;
	}

	panel->backlight.edp.intel.sdr_uses_aux =
		tcon_cap[2] & INTEL_EDP_SDR_TCON_BRIGHTNESS_AUX_CAP;

	return true;
}

static u32
intel_dp_aux_hdr_get_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	u8 tmp;
	u8 buf[2] = { 0 };

	if (drm_dp_dpcd_readb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, &tmp) < 0) {
		drm_err(&i915->drm, "Failed to read current backlight mode from DPCD\n");
		return 0;
	}

	if (!(tmp & INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE)) {
		if (!panel->backlight.edp.intel.sdr_uses_aux) {
			u32 pwm_level = panel->backlight.pwm_funcs->get(connector, pipe);

			return intel_panel_backlight_level_from_pwm(connector, pwm_level);
		}

		/* Assume 100% brightness if backlight controls aren't enabled yet */
		return panel->backlight.max;
	}

	if (drm_dp_dpcd_read(&intel_dp->aux, INTEL_EDP_BRIGHTNESS_NITS_LSB, buf, sizeof(buf)) < 0) {
		drm_err(&i915->drm, "Failed to read brightness from DPCD\n");
		return 0;
	}

	return (buf[1] << 8 | buf[0]);
}

static void
intel_dp_aux_hdr_set_aux_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_device *dev = connector->base.dev;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	u8 buf[4] = { 0 };

	buf[0] = level & 0xFF;
	buf[1] = (level & 0xFF00) >> 8;

	if (drm_dp_dpcd_write(&intel_dp->aux, INTEL_EDP_BRIGHTNESS_NITS_LSB, buf, 4) < 0)
		drm_err(dev, "Failed to write brightness level to DPCD\n");
}

static void
intel_dp_aux_hdr_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	if (panel->backlight.edp.intel.sdr_uses_aux) {
		intel_dp_aux_hdr_set_aux_backlight(conn_state, level);
	} else {
		const u32 pwm_level = intel_panel_backlight_level_to_pwm(connector, level);

		intel_panel_set_pwm_level(conn_state, pwm_level);
	}
}

static void
intel_dp_aux_hdr_enable_backlight(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	int ret;
	u8 old_ctrl, ctrl;

	ret = drm_dp_dpcd_readb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, &old_ctrl);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to read current backlight control mode: %d\n", ret);
		return;
	}

	ctrl = old_ctrl;
	if (panel->backlight.edp.intel.sdr_uses_aux) {
		ctrl |= INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE;
		intel_dp_aux_hdr_set_aux_backlight(conn_state, level);
	} else {
		u32 pwm_level = intel_panel_backlight_level_to_pwm(connector, level);

		panel->backlight.pwm_funcs->enable(crtc_state, conn_state, pwm_level);

		ctrl &= ~INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE;
	}

	if (ctrl != old_ctrl)
		if (drm_dp_dpcd_writeb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, ctrl) < 0)
			drm_err(&i915->drm, "Failed to configure DPCD brightness controls\n");
}

static void
intel_dp_aux_hdr_disable_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	/* Nothing to do for AUX based backlight controls */
	if (panel->backlight.edp.intel.sdr_uses_aux)
		return;

	/* Note we want the actual pwm_level to be 0, regardless of pwm_min */
	panel->backlight.pwm_funcs->disable(conn_state, intel_panel_invert_pwm_level(connector, 0));
}

static int
intel_dp_aux_hdr_setup_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	int ret;

	if (panel->backlight.edp.intel.sdr_uses_aux) {
		drm_dbg_kms(&i915->drm, "SDR backlight is controlled through DPCD\n");
	} else {
		drm_dbg_kms(&i915->drm, "SDR backlight is controlled through PWM\n");

		ret = panel->backlight.pwm_funcs->setup(connector, pipe);
		if (ret < 0) {
			drm_err(&i915->drm,
				"Failed to setup SDR backlight controls through PWM: %d\n", ret);
			return ret;
		}
	}

	panel->backlight.max = 512;
	panel->backlight.min = 0;
	panel->backlight.level = intel_dp_aux_hdr_get_backlight(connector, pipe);
	panel->backlight.enabled = panel->backlight.level != 0;

	return 0;
}

/* VESA backlight callbacks */
static void set_vesa_backlight_enable(struct intel_dp *intel_dp, bool enable)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 reg_val = 0;

	/* Early return when display use other mechanism to enable backlight. */
	if (!(intel_dp->edp_dpcd[1] & DP_EDP_BACKLIGHT_AUX_ENABLE_CAP))
		return;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
			      &reg_val) < 0) {
		drm_dbg_kms(&i915->drm, "Failed to read DPCD register 0x%x\n",
			    DP_EDP_DISPLAY_CONTROL_REGISTER);
		return;
	}
	if (enable)
		reg_val |= DP_EDP_BACKLIGHT_ENABLE;
	else
		reg_val &= ~(DP_EDP_BACKLIGHT_ENABLE);

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
			       reg_val) != 1) {
		drm_dbg_kms(&i915->drm, "Failed to %s aux backlight\n",
			    enabledisable(enable));
	}
}

static bool intel_dp_aux_vesa_backlight_dpcd_mode(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 mode_reg;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_EDP_BACKLIGHT_MODE_SET_REGISTER,
			      &mode_reg) != 1) {
		drm_dbg_kms(&i915->drm,
			    "Failed to read the DPCD register 0x%x\n",
			    DP_EDP_BACKLIGHT_MODE_SET_REGISTER);
		return false;
	}

	return (mode_reg & DP_EDP_BACKLIGHT_CONTROL_MODE_MASK) ==
	       DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD;
}

/*
 * Read the current backlight value from DPCD register(s) based
 * on if 8-bit(MSB) or 16-bit(MSB and LSB) values are supported
 */
static u32 intel_dp_aux_vesa_get_backlight(struct intel_connector *connector, enum pipe unused)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 read_val[2] = { 0x0 };
	u16 level = 0;

	/*
	 * If we're not in DPCD control mode yet, the programmed brightness
	 * value is meaningless and we should assume max brightness
	 */
	if (!intel_dp_aux_vesa_backlight_dpcd_mode(connector))
		return connector->panel.backlight.max;

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
			     &read_val, sizeof(read_val)) < 0) {
		drm_dbg_kms(&i915->drm, "Failed to read DPCD register 0x%x\n",
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
intel_dp_aux_vesa_set_backlight(const struct drm_connector_state *conn_state,
				u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 vals[2] = { 0x0 };

	vals[0] = level;

	/* Write the MSB and/or LSB */
	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT) {
		vals[0] = (level & 0xFF00) >> 8;
		vals[1] = (level & 0xFF);
	}
	if (drm_dp_dpcd_write(&intel_dp->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
			      vals, sizeof(vals)) < 0) {
		drm_dbg_kms(&i915->drm,
			    "Failed to write aux backlight level\n");
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
static bool intel_dp_aux_vesa_set_pwm_freq(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	const u8 pn = connector->panel.backlight.edp.vesa.pwmgen_bit_count;
	int freq, fxp, f, fxp_actual, fxp_min, fxp_max;

	freq = dev_priv->vbt.backlight.pwm_freq_hz;
	if (!freq) {
		drm_dbg_kms(&dev_priv->drm,
			    "Use panel default backlight frequency\n");
		return false;
	}

	fxp = DIV_ROUND_CLOSEST(KHz(DP_EDP_BACKLIGHT_FREQ_BASE_KHZ), freq);
	f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
	fxp_actual = f << pn;

	/* Ensure frequency is within 25% of desired value */
	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);

	if (fxp_min > fxp_actual || fxp_actual > fxp_max) {
		drm_dbg_kms(&dev_priv->drm, "Actual frequency out of range\n");
		return false;
	}

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_EDP_BACKLIGHT_FREQ_SET, (u8) f) < 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "Failed to write aux backlight freq\n");
		return false;
	}
	return true;
}

static void
intel_dp_aux_vesa_enable_backlight(const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_panel *panel = &connector->panel;
	u8 dpcd_buf, new_dpcd_buf, edp_backlight_mode;
	u8 pwmgen_bit_count = panel->backlight.edp.vesa.pwmgen_bit_count;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			DP_EDP_BACKLIGHT_MODE_SET_REGISTER, &dpcd_buf) != 1) {
		drm_dbg_kms(&i915->drm, "Failed to read DPCD register 0x%x\n",
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
				       pwmgen_bit_count) < 0)
			drm_dbg_kms(&i915->drm,
				    "Failed to write aux pwmgen bit count\n");

		break;

	/* Do nothing when it is already DPCD mode */
	case DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD:
	default:
		break;
	}

	if (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_FREQ_AUX_SET_CAP)
		if (intel_dp_aux_vesa_set_pwm_freq(connector))
			new_dpcd_buf |= DP_EDP_BACKLIGHT_FREQ_AUX_SET_ENABLE;

	if (new_dpcd_buf != dpcd_buf) {
		if (drm_dp_dpcd_writeb(&intel_dp->aux,
			DP_EDP_BACKLIGHT_MODE_SET_REGISTER, new_dpcd_buf) < 0) {
			drm_dbg_kms(&i915->drm,
				    "Failed to write aux backlight mode\n");
		}
	}

	intel_dp_aux_vesa_set_backlight(conn_state, level);
	set_vesa_backlight_enable(intel_dp, true);
}

static void intel_dp_aux_vesa_disable_backlight(const struct drm_connector_state *old_conn_state,
						u32 level)
{
	set_vesa_backlight_enable(enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder)),
				  false);
}

static u32 intel_dp_aux_vesa_calc_max_backlight(struct intel_connector *connector)
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
	drm_dbg_kms(&i915->drm, "VBT defined backlight frequency %u Hz\n",
		    freq);
	if (!freq) {
		drm_dbg_kms(&i915->drm,
			    "Use panel default backlight frequency\n");
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
		drm_dbg_kms(&i915->drm,
			    "Failed to read pwmgen bit count cap min\n");
		return max_backlight;
	}
	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_EDP_PWMGEN_BIT_COUNT_CAP_MAX, &pn_max) != 1) {
		drm_dbg_kms(&i915->drm,
			    "Failed to read pwmgen bit count cap max\n");
		return max_backlight;
	}
	pn_min &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
	pn_max &= DP_EDP_PWMGEN_BIT_COUNT_MASK;

	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);
	if (fxp_min < (1 << pn_min) || (255 << pn_max) < fxp_max) {
		drm_dbg_kms(&i915->drm,
			    "VBT defined backlight frequency out of range\n");
		return max_backlight;
	}

	for (pn = pn_max; pn >= pn_min; pn--) {
		f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
		fxp_actual = f << pn;
		if (fxp_min <= fxp_actual && fxp_actual <= fxp_max)
			break;
	}

	drm_dbg_kms(&i915->drm, "Using eDP pwmgen bit count of %d\n", pn);
	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_EDP_PWMGEN_BIT_COUNT, pn) < 0) {
		drm_dbg_kms(&i915->drm,
			    "Failed to write aux pwmgen bit count\n");
		return max_backlight;
	}
	panel->backlight.edp.vesa.pwmgen_bit_count = pn;

	max_backlight = (1 << pn) - 1;

	return max_backlight;
}

static int intel_dp_aux_vesa_setup_backlight(struct intel_connector *connector,
					     enum pipe pipe)
{
	struct intel_panel *panel = &connector->panel;

	panel->backlight.max = intel_dp_aux_vesa_calc_max_backlight(connector);
	if (!panel->backlight.max)
		return -ENODEV;

	panel->backlight.min = 0;
	panel->backlight.level = intel_dp_aux_vesa_get_backlight(connector, pipe);
	panel->backlight.enabled = intel_dp_aux_vesa_backlight_dpcd_mode(connector) &&
				   panel->backlight.level != 0;

	return 0;
}

static bool
intel_dp_aux_supports_vesa_backlight(struct intel_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	/* Check the eDP Display control capabilities registers to determine if
	 * the panel can support backlight control over the aux channel.
	 *
	 * TODO: We currently only support AUX only backlight configurations, not backlights which
	 * require a mix of PWM and AUX controls to work. In the mean time, these machines typically
	 * work just fine using normal PWM controls anyway.
	 */
	if (intel_dp->edp_dpcd[1] & DP_EDP_TCON_BACKLIGHT_ADJUSTMENT_CAP &&
	    (intel_dp->edp_dpcd[1] & DP_EDP_BACKLIGHT_AUX_ENABLE_CAP) &&
	    (intel_dp->edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_AUX_SET_CAP)) {
		drm_dbg_kms(&i915->drm, "AUX Backlight Control Supported!\n");
		return true;
	}
	return false;
}

static const struct intel_panel_bl_funcs intel_dp_hdr_bl_funcs = {
	.setup = intel_dp_aux_hdr_setup_backlight,
	.enable = intel_dp_aux_hdr_enable_backlight,
	.disable = intel_dp_aux_hdr_disable_backlight,
	.set = intel_dp_aux_hdr_set_backlight,
	.get = intel_dp_aux_hdr_get_backlight,
};

static const struct intel_panel_bl_funcs intel_dp_vesa_bl_funcs = {
	.setup = intel_dp_aux_vesa_setup_backlight,
	.enable = intel_dp_aux_vesa_enable_backlight,
	.disable = intel_dp_aux_vesa_disable_backlight,
	.set = intel_dp_aux_vesa_set_backlight,
	.get = intel_dp_aux_vesa_get_backlight,
};

enum intel_dp_aux_backlight_modparam {
	INTEL_DP_AUX_BACKLIGHT_AUTO = -1,
	INTEL_DP_AUX_BACKLIGHT_OFF = 0,
	INTEL_DP_AUX_BACKLIGHT_ON = 1,
	INTEL_DP_AUX_BACKLIGHT_FORCE_VESA = 2,
	INTEL_DP_AUX_BACKLIGHT_FORCE_INTEL = 3,
};

int intel_dp_aux_init_backlight_funcs(struct intel_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	bool try_intel_interface = false, try_vesa_interface = false;

	/* Check the VBT and user's module parameters to figure out which
	 * interfaces to probe
	 */
	switch (i915->params.enable_dpcd_backlight) {
	case INTEL_DP_AUX_BACKLIGHT_OFF:
		return -ENODEV;
	case INTEL_DP_AUX_BACKLIGHT_AUTO:
		switch (i915->vbt.backlight.type) {
		case INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE:
			try_vesa_interface = true;
			break;
		case INTEL_BACKLIGHT_DISPLAY_DDI:
			try_intel_interface = true;
			break;
		default:
			return -ENODEV;
		}
		break;
	case INTEL_DP_AUX_BACKLIGHT_ON:
		if (i915->vbt.backlight.type != INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE)
			try_intel_interface = true;

		try_vesa_interface = true;
		break;
	case INTEL_DP_AUX_BACKLIGHT_FORCE_VESA:
		try_vesa_interface = true;
		break;
	case INTEL_DP_AUX_BACKLIGHT_FORCE_INTEL:
		try_intel_interface = true;
		break;
	}

	/*
	 * A lot of eDP panels in the wild will report supporting both the
	 * Intel proprietary backlight control interface, and the VESA
	 * backlight control interface. Many of these panels are liars though,
	 * and will only work with the Intel interface. So, always probe for
	 * that first.
	 */
	if (try_intel_interface && intel_dp_aux_supports_hdr_backlight(connector)) {
		drm_dbg_kms(dev, "Using Intel proprietary eDP backlight controls\n");
		panel->backlight.funcs = &intel_dp_hdr_bl_funcs;
		return 0;
	}

	if (try_vesa_interface && intel_dp_aux_supports_vesa_backlight(connector)) {
		drm_dbg_kms(dev, "Using VESA eDP backlight controls\n");
		panel->backlight.funcs = &intel_dp_vesa_bl_funcs;
		return 0;
	}

	return -ENODEV;
}
