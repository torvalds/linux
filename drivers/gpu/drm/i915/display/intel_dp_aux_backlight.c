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

#include "i915_drv.h"
#include "intel_backlight.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_aux_backlight.h"

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
# define INTEL_EDP_HDR_TCON_TONE_MAPPING_ENABLE                       BIT(2)
# define INTEL_EDP_HDR_TCON_SEGMENTED_BACKLIGHT_ENABLE                BIT(3)
# define INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE                     BIT(4)
# define INTEL_EDP_HDR_TCON_SRGB_TO_PANEL_GAMUT_ENABLE                BIT(5)
/* Bit 6 is reserved */
# define INTEL_EDP_HDR_TCON_SDP_OVERRIDE_AUX			      BIT(7)

#define INTEL_EDP_HDR_CONTENT_LUMINANCE                                0x346
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

enum intel_dp_aux_backlight_modparam {
	INTEL_DP_AUX_BACKLIGHT_AUTO = -1,
	INTEL_DP_AUX_BACKLIGHT_OFF = 0,
	INTEL_DP_AUX_BACKLIGHT_ON = 1,
	INTEL_DP_AUX_BACKLIGHT_FORCE_VESA = 2,
	INTEL_DP_AUX_BACKLIGHT_FORCE_INTEL = 3,
};

static bool is_intel_tcon_cap(const u8 tcon_cap[4])
{
	return tcon_cap[0] >= 1;
}

/* Intel EDP backlight callbacks */
static bool
intel_dp_aux_supports_hdr_backlight(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	struct drm_dp_aux *aux = &intel_dp->aux;
	struct intel_panel *panel = &connector->panel;
	int ret;
	u8 tcon_cap[4];

	intel_dp_wait_source_oui(intel_dp);

	ret = drm_dp_dpcd_read(aux, INTEL_EDP_HDR_TCON_CAP0, tcon_cap, sizeof(tcon_cap));
	if (ret != sizeof(tcon_cap))
		return false;

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] Detected %s HDR backlight interface version %d\n",
		    connector->base.base.id, connector->base.name,
		    is_intel_tcon_cap(tcon_cap) ? "Intel" : "unsupported", tcon_cap[0]);

	if (!is_intel_tcon_cap(tcon_cap))
		return false;

	if (!(tcon_cap[1] & INTEL_EDP_HDR_TCON_BRIGHTNESS_NITS_CAP))
		return false;

	/*
	 * If we don't have HDR static metadata there is no way to
	 * runtime detect used range for nits based control. For now
	 * do not use Intel proprietary eDP backlight control if we
	 * don't have this data in panel EDID. In case we find panel
	 * which supports only nits based control, but doesn't provide
	 * HDR static metadata we need to start maintaining table of
	 * ranges for such panels.
	 */
	if (display->params.enable_dpcd_backlight != INTEL_DP_AUX_BACKLIGHT_FORCE_INTEL &&
	    !(connector->base.hdr_sink_metadata.hdmi_type1.metadata_type &
	      BIT(HDMI_STATIC_METADATA_TYPE1))) {
		drm_info(display->drm,
			 "[CONNECTOR:%d:%s] Panel is missing HDR static metadata. Possible support for Intel HDR backlight interface is not used. If your backlight controls don't work try booting with i915.enable_dpcd_backlight=%d. needs this, please file a _new_ bug report on drm/i915, see " FDO_BUG_URL " for details.\n",
			 connector->base.base.id, connector->base.name,
			 INTEL_DP_AUX_BACKLIGHT_FORCE_INTEL);
		return false;
	}

	panel->backlight.edp.intel_cap.sdr_uses_aux =
		tcon_cap[2] & INTEL_EDP_SDR_TCON_BRIGHTNESS_AUX_CAP;
	panel->backlight.edp.intel_cap.supports_2084_decode =
		tcon_cap[1] & INTEL_EDP_HDR_TCON_2084_DECODE_CAP;
	panel->backlight.edp.intel_cap.supports_2020_gamut =
		tcon_cap[1] & INTEL_EDP_HDR_TCON_2020_GAMUT_CAP;
	panel->backlight.edp.intel_cap.supports_segmented_backlight =
		tcon_cap[1] & INTEL_EDP_HDR_TCON_SEGMENTED_BACKLIGHT_CAP;
	panel->backlight.edp.intel_cap.supports_sdp_colorimetry =
		tcon_cap[1] & INTEL_EDP_HDR_TCON_SDP_COLORIMETRY_CAP;
	panel->backlight.edp.intel_cap.supports_tone_mapping =
		tcon_cap[1] & INTEL_EDP_HDR_TCON_TONE_MAPPING_CAP;

	return true;
}

static u32
intel_dp_aux_hdr_get_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	u8 tmp;
	u8 buf[2] = {};

	if (drm_dp_dpcd_readb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, &tmp) != 1) {
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] Failed to read current backlight mode from DPCD\n",
			connector->base.base.id, connector->base.name);
		return 0;
	}

	if (!(tmp & INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE)) {
		if (!panel->backlight.edp.intel_cap.sdr_uses_aux) {
			u32 pwm_level = panel->backlight.pwm_funcs->get(connector, pipe);

			return intel_backlight_level_from_pwm(connector, pwm_level);
		}

		/* Assume 100% brightness if backlight controls aren't enabled yet */
		return panel->backlight.max;
	}

	if (drm_dp_dpcd_read(&intel_dp->aux, INTEL_EDP_BRIGHTNESS_NITS_LSB, buf,
			     sizeof(buf)) != sizeof(buf)) {
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] Failed to read brightness from DPCD\n",
			connector->base.base.id, connector->base.name);
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
	u8 buf[4] = {};

	buf[0] = level & 0xFF;
	buf[1] = (level & 0xFF00) >> 8;

	if (drm_dp_dpcd_write(&intel_dp->aux, INTEL_EDP_BRIGHTNESS_NITS_LSB, buf,
			      sizeof(buf)) != sizeof(buf))
		drm_err(dev, "[CONNECTOR:%d:%s] Failed to write brightness level to DPCD\n",
			connector->base.base.id, connector->base.name);
}

static bool
intel_dp_in_hdr_mode(const struct drm_connector_state *conn_state)
{
	struct hdr_output_metadata *hdr_metadata;

	if (!conn_state->hdr_output_metadata)
		return false;

	hdr_metadata = conn_state->hdr_output_metadata->data;

	return hdr_metadata->hdmi_metadata_type1.eotf == HDMI_EOTF_SMPTE_ST2084;
}

static void
intel_dp_aux_hdr_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	if (intel_dp_in_hdr_mode(conn_state) ||
	    panel->backlight.edp.intel_cap.sdr_uses_aux) {
		intel_dp_aux_hdr_set_aux_backlight(conn_state, level);
	} else {
		const u32 pwm_level = intel_backlight_level_to_pwm(connector, level);

		intel_backlight_set_pwm_level(conn_state, pwm_level);
	}
}

static void
intel_dp_aux_write_content_luminance(struct intel_connector *connector,
				     struct hdr_output_metadata *hdr_metadata)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	int ret;
	u8 buf[4];

	if (!intel_dp_has_gamut_metadata_dip(connector->encoder))
		return;

	buf[0] = hdr_metadata->hdmi_metadata_type1.max_cll & 0xFF;
	buf[1] = (hdr_metadata->hdmi_metadata_type1.max_cll & 0xFF00) >> 8;
	buf[2] = hdr_metadata->hdmi_metadata_type1.max_fall & 0xFF;
	buf[3] = (hdr_metadata->hdmi_metadata_type1.max_fall & 0xFF00) >> 8;

	ret = drm_dp_dpcd_write(&intel_dp->aux,
				INTEL_EDP_HDR_CONTENT_LUMINANCE,
				buf, sizeof(buf));
	if (ret < 0)
		drm_dbg_kms(display->drm,
			    "Content Luminance DPCD reg write failed, err:-%d\n",
			    ret);
}

static void
intel_dp_aux_fill_hdr_tcon_params(const struct drm_connector_state *conn_state, u8 *ctrl)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_display *display = to_intel_display(connector);

	/*
	 * According to spec segmented backlight needs to be set whenever panel is in
	 * HDR mode.
	 */
	if (intel_dp_in_hdr_mode(conn_state)) {
		*ctrl |= INTEL_EDP_HDR_TCON_SEGMENTED_BACKLIGHT_ENABLE;
		*ctrl |= INTEL_EDP_HDR_TCON_2084_DECODE_ENABLE;
	}

	if (DISPLAY_VER(display) < 11)
		*ctrl &= ~INTEL_EDP_HDR_TCON_TONE_MAPPING_ENABLE;

	if (panel->backlight.edp.intel_cap.supports_2020_gamut &&
	    (conn_state->colorspace == DRM_MODE_COLORIMETRY_BT2020_RGB ||
	     conn_state->colorspace == DRM_MODE_COLORIMETRY_BT2020_YCC ||
	     conn_state->colorspace == DRM_MODE_COLORIMETRY_BT2020_CYCC))
		*ctrl |= INTEL_EDP_HDR_TCON_2020_GAMUT_ENABLE;

	if (panel->backlight.edp.intel_cap.supports_sdp_colorimetry &&
	    intel_dp_has_gamut_metadata_dip(connector->encoder))
		*ctrl |= INTEL_EDP_HDR_TCON_SDP_OVERRIDE_AUX;
	else
		*ctrl &= ~INTEL_EDP_HDR_TCON_SDP_OVERRIDE_AUX;
}

static void
intel_dp_aux_hdr_enable_backlight(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	struct hdr_output_metadata *hdr_metadata;
	int ret;
	u8 old_ctrl, ctrl;

	intel_dp_wait_source_oui(intel_dp);

	ret = drm_dp_dpcd_readb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, &old_ctrl);
	if (ret != 1) {
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] Failed to read current backlight control mode: %d\n",
			connector->base.base.id, connector->base.name, ret);
		return;
	}

	ctrl = old_ctrl;
	if (intel_dp_in_hdr_mode(conn_state) ||
	    panel->backlight.edp.intel_cap.sdr_uses_aux) {
		ctrl |= INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE;

		intel_dp_aux_hdr_set_aux_backlight(conn_state, level);
	} else {
		u32 pwm_level = intel_backlight_level_to_pwm(connector, level);

		panel->backlight.pwm_funcs->enable(crtc_state, conn_state, pwm_level);

		ctrl &= ~INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE;
	}

	intel_dp_aux_fill_hdr_tcon_params(conn_state, &ctrl);

	if (ctrl != old_ctrl &&
	    drm_dp_dpcd_writeb(&intel_dp->aux, INTEL_EDP_HDR_GETSET_CTRL_PARAMS, ctrl) != 1)
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] Failed to configure DPCD brightness controls\n",
			connector->base.base.id, connector->base.name);

	if (intel_dp_in_hdr_mode(conn_state)) {
		hdr_metadata = conn_state->hdr_output_metadata->data;
		intel_dp_aux_write_content_luminance(connector, hdr_metadata);
	}
}

static void
intel_dp_aux_hdr_disable_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	/* Nothing to do for AUX based backlight controls */
	if (panel->backlight.edp.intel_cap.sdr_uses_aux)
		return;

	/* Note we want the actual pwm_level to be 0, regardless of pwm_min */
	panel->backlight.pwm_funcs->disable(conn_state, intel_backlight_invert_pwm_level(connector, 0));
}

static const char *dpcd_vs_pwm_str(bool aux)
{
	return aux ? "DPCD" : "PWM";
}

static void
intel_dp_aux_write_panel_luminance_override(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);
	int ret;
	u8 buf[4] = {};

	buf[0] = panel->backlight.min & 0xFF;
	buf[1] = (panel->backlight.min & 0xFF00) >> 8;
	buf[2] = panel->backlight.max & 0xFF;
	buf[3] = (panel->backlight.max & 0xFF00) >> 8;

	ret = drm_dp_dpcd_write(&intel_dp->aux,
				INTEL_EDP_HDR_PANEL_LUMINANCE_OVERRIDE,
				buf, sizeof(buf));
	if (ret < 0)
		drm_dbg_kms(display->drm,
			    "Panel Luminance DPCD reg write failed, err:-%d\n",
			    ret);
}

static int
intel_dp_aux_hdr_setup_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_panel *panel = &connector->panel;
	struct drm_luminance_range_info *luminance_range =
		&connector->base.display_info.luminance_range;
	int ret;

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] SDR backlight is controlled through %s\n",
		    connector->base.base.id, connector->base.name,
		    dpcd_vs_pwm_str(panel->backlight.edp.intel_cap.sdr_uses_aux));

	if (!panel->backlight.edp.intel_cap.sdr_uses_aux) {
		ret = panel->backlight.pwm_funcs->setup(connector, pipe);
		if (ret < 0) {
			drm_err(display->drm,
				"[CONNECTOR:%d:%s] Failed to setup SDR backlight controls through PWM: %d\n",
				connector->base.base.id, connector->base.name, ret);
			return ret;
		}
	}

	if (luminance_range->max_luminance) {
		panel->backlight.max = luminance_range->max_luminance;
		panel->backlight.min = luminance_range->min_luminance;
	} else {
		panel->backlight.max = 512;
		panel->backlight.min = 0;
	}

	intel_dp_aux_write_panel_luminance_override(connector);

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] Using AUX HDR interface for backlight control (range %d..%d)\n",
		    connector->base.base.id, connector->base.name,
		    panel->backlight.min, panel->backlight.max);

	panel->backlight.level = intel_dp_aux_hdr_get_backlight(connector, pipe);
	panel->backlight.enabled = panel->backlight.level != 0;

	return 0;
}

/* VESA backlight callbacks */
static u32 intel_dp_aux_vesa_get_backlight(struct intel_connector *connector, enum pipe unused)
{
	return connector->panel.backlight.level;
}

static void
intel_dp_aux_vesa_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);

	if (!panel->backlight.edp.vesa.info.aux_set) {
		const u32 pwm_level = intel_backlight_level_to_pwm(connector, level);

		intel_backlight_set_pwm_level(conn_state, pwm_level);
	}

	drm_edp_backlight_set_level(&intel_dp->aux, &panel->backlight.edp.vesa.info, level);
}

static void
intel_dp_aux_vesa_enable_backlight(const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);

	if (!panel->backlight.edp.vesa.info.aux_enable) {
		u32 pwm_level;

		if (!panel->backlight.edp.vesa.info.aux_set)
			pwm_level = intel_backlight_level_to_pwm(connector, level);
		else
			pwm_level = intel_backlight_invert_pwm_level(connector,
								     panel->backlight.pwm_level_max);

		panel->backlight.pwm_funcs->enable(crtc_state, conn_state, pwm_level);
	}

	drm_edp_backlight_enable(&intel_dp->aux, &panel->backlight.edp.vesa.info, level);
}

static void intel_dp_aux_vesa_disable_backlight(const struct drm_connector_state *old_conn_state,
						u32 level)
{
	struct intel_connector *connector = to_intel_connector(old_conn_state->connector);
	struct intel_panel *panel = &connector->panel;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);

	drm_edp_backlight_disable(&intel_dp->aux, &panel->backlight.edp.vesa.info);

	if (!panel->backlight.edp.vesa.info.aux_enable)
		panel->backlight.pwm_funcs->disable(old_conn_state,
						    intel_backlight_invert_pwm_level(connector, 0));
}

static int intel_dp_aux_vesa_setup_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_panel *panel = &connector->panel;
	u16 current_level;
	u8 current_mode;
	int ret;

	ret = drm_edp_backlight_init(&intel_dp->aux, &panel->backlight.edp.vesa.info,
				     panel->vbt.backlight.pwm_freq_hz, intel_dp->edp_dpcd,
				     &current_level, &current_mode);
	if (ret < 0)
		return ret;

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] AUX VESA backlight enable is controlled through %s\n",
		    connector->base.base.id, connector->base.name,
		    dpcd_vs_pwm_str(panel->backlight.edp.vesa.info.aux_enable));
	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] AUX VESA backlight level is controlled through %s\n",
		    connector->base.base.id, connector->base.name,
		    dpcd_vs_pwm_str(panel->backlight.edp.vesa.info.aux_set));

	if (!panel->backlight.edp.vesa.info.aux_set || !panel->backlight.edp.vesa.info.aux_enable) {
		ret = panel->backlight.pwm_funcs->setup(connector, pipe);
		if (ret < 0) {
			drm_err(display->drm,
				"[CONNECTOR:%d:%s] Failed to setup PWM backlight controls for eDP backlight: %d\n",
				connector->base.base.id, connector->base.name, ret);
			return ret;
		}
	}

	if (panel->backlight.edp.vesa.info.aux_set) {
		panel->backlight.max = panel->backlight.edp.vesa.info.max;
		panel->backlight.min = 0;
		if (current_mode == DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD) {
			panel->backlight.level = current_level;
			panel->backlight.enabled = panel->backlight.level != 0;
		} else {
			panel->backlight.level = panel->backlight.max;
			panel->backlight.enabled = false;
		}
	} else {
		panel->backlight.max = panel->backlight.pwm_level_max;
		panel->backlight.min = panel->backlight.pwm_level_min;
		if (current_mode == DP_EDP_BACKLIGHT_CONTROL_MODE_PWM) {
			panel->backlight.level = panel->backlight.pwm_funcs->get(connector, pipe);
			panel->backlight.enabled = panel->backlight.pwm_enabled;
		} else {
			panel->backlight.level = panel->backlight.max;
			panel->backlight.enabled = false;
		}
	}

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] Using AUX VESA interface for backlight control\n",
		    connector->base.base.id, connector->base.name);

	return 0;
}

static bool
intel_dp_aux_supports_vesa_backlight(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	if (drm_edp_backlight_supported(intel_dp->edp_dpcd)) {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] AUX Backlight Control Supported!\n",
			    connector->base.base.id, connector->base.name);
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

int intel_dp_aux_init_backlight_funcs(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_device *dev = connector->base.dev;
	struct intel_panel *panel = &connector->panel;
	bool try_intel_interface = false, try_vesa_interface = false;

	/* Check the VBT and user's module parameters to figure out which
	 * interfaces to probe
	 */
	switch (display->params.enable_dpcd_backlight) {
	case INTEL_DP_AUX_BACKLIGHT_OFF:
		return -ENODEV;
	case INTEL_DP_AUX_BACKLIGHT_AUTO:
		switch (panel->vbt.backlight.type) {
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
		if (panel->vbt.backlight.type != INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE)
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
	 * Since Intel has their own backlight control interface, the majority of machines out there
	 * using DPCD backlight controls with Intel GPUs will be using this interface as opposed to
	 * the VESA interface. However, other GPUs (such as Nvidia's) will always use the VESA
	 * interface. This means that there's quite a number of panels out there that will advertise
	 * support for both interfaces, primarily systems with Intel/Nvidia hybrid GPU setups.
	 *
	 * There's a catch to this though: on many panels that advertise support for both
	 * interfaces, the VESA backlight interface will stop working once we've programmed the
	 * panel with Intel's OUI - which is also required for us to be able to detect Intel's
	 * backlight interface at all. This means that the only sensible way for us to detect both
	 * interfaces is to probe for Intel's first, and VESA's second.
	 */
	if (try_intel_interface && intel_dp_aux_supports_hdr_backlight(connector)) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Using Intel proprietary eDP backlight controls\n",
			    connector->base.base.id, connector->base.name);
		panel->backlight.funcs = &intel_dp_hdr_bl_funcs;
		return 0;
	}

	if (try_vesa_interface && intel_dp_aux_supports_vesa_backlight(connector)) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Using VESA eDP backlight controls\n",
			    connector->base.base.id, connector->base.name);
		panel->backlight.funcs = &intel_dp_vesa_bl_funcs;
		return 0;
	}

	return -ENODEV;
}
