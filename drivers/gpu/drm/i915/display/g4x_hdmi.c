// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 * HDMI support for G4x,ILK,SNB,IVB,VLV,CHV (HSW+ handled by the DDI code).
 */

#include "g4x_hdmi.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_display_types.h"
#include "intel_dpio_phy.h"
#include "intel_fifo_underrun.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_sideband.h"
#include "intel_sdvo.h"

static void intel_hdmi_prepare(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 hdmi_val;

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);

	hdmi_val = SDVO_ENCODING_HDMI;
	if (!HAS_PCH_SPLIT(dev_priv) && crtc_state->limited_color_range)
		hdmi_val |= HDMI_COLOR_RANGE_16_235;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		hdmi_val |= SDVO_VSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		hdmi_val |= SDVO_HSYNC_ACTIVE_HIGH;

	if (crtc_state->pipe_bpp > 24)
		hdmi_val |= HDMI_COLOR_FORMAT_12bpc;
	else
		hdmi_val |= SDVO_COLOR_FORMAT_8bpc;

	if (crtc_state->has_hdmi_sink)
		hdmi_val |= HDMI_MODE_SELECT_HDMI;

	if (HAS_PCH_CPT(dev_priv))
		hdmi_val |= SDVO_PIPE_SEL_CPT(crtc->pipe);
	else if (IS_CHERRYVIEW(dev_priv))
		hdmi_val |= SDVO_PIPE_SEL_CHV(crtc->pipe);
	else
		hdmi_val |= SDVO_PIPE_SEL(crtc->pipe);

	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, hdmi_val);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);
}

static bool intel_hdmi_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_sdvo_port_enabled(dev_priv, intel_hdmi->hdmi_reg, pipe);

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_hdmi_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 tmp, flags = 0;
	int dotclock;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_HDMI);

	tmp = intel_de_read(dev_priv, intel_hdmi->hdmi_reg);

	if (tmp & SDVO_HSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;

	if (tmp & SDVO_VSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	if (tmp & HDMI_MODE_SELECT_HDMI)
		pipe_config->has_hdmi_sink = true;

	pipe_config->infoframes.enable |=
		intel_hdmi_infoframes_enabled(encoder, pipe_config);

	if (pipe_config->infoframes.enable)
		pipe_config->has_infoframe = true;

	if (tmp & HDMI_AUDIO_ENABLE)
		pipe_config->has_audio = true;

	if (!HAS_PCH_SPLIT(dev_priv) &&
	    tmp & HDMI_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->hw.adjusted_mode.flags |= flags;

	if ((tmp & SDVO_COLOR_FORMAT_MASK) == HDMI_COLOR_FORMAT_12bpc)
		dotclock = pipe_config->port_clock * 2 / 3;
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->hw.adjusted_mode.crtc_clock = dotclock;

	pipe_config->lane_count = 4;

	intel_hdmi_read_gcp_infoframe(encoder, pipe_config);

	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_AVI,
			     &pipe_config->infoframes.avi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_SPD,
			     &pipe_config->infoframes.spd);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_VENDOR,
			     &pipe_config->infoframes.hdmi);
}

static void intel_enable_hdmi_audio(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);

	drm_WARN_ON(&i915->drm, !pipe_config->has_hdmi_sink);
	drm_dbg_kms(&i915->drm, "Enabling HDMI audio on pipe %c\n",
		    pipe_name(crtc->pipe));
	intel_audio_codec_enable(encoder, pipe_config, conn_state);
}

static void g4x_enable_hdmi(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	u32 temp;

	temp = intel_de_read(dev_priv, intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= HDMI_AUDIO_ENABLE;

	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void ibx_enable_hdmi(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	u32 temp;

	temp = intel_de_read(dev_priv, intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= HDMI_AUDIO_ENABLE;

	/*
	 * HW workaround, need to write this twice for issue
	 * that may result in first write getting masked.
	 */
	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);
	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

	/*
	 * HW workaround, need to toggle enable bit off and on
	 * for 12bpc with pixel repeat.
	 *
	 * FIXME: BSpec says this should be done at the end of
	 * the modeset sequence, so not sure if this isn't too soon.
	 */
	if (pipe_config->pipe_bpp > 24 &&
	    pipe_config->pixel_multiplier > 1) {
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg,
			       temp & ~SDVO_ENABLE);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

		/*
		 * HW workaround, need to write this twice for issue
		 * that may result in first write getting masked.
		 */
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);
	}

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void cpt_enable_hdmi(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	enum pipe pipe = crtc->pipe;
	u32 temp;

	temp = intel_de_read(dev_priv, intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= HDMI_AUDIO_ENABLE;

	/*
	 * WaEnableHDMI8bpcBefore12bpc:snb,ivb
	 *
	 * The procedure for 12bpc is as follows:
	 * 1. disable HDMI clock gating
	 * 2. enable HDMI with 8bpc
	 * 3. enable HDMI with 12bpc
	 * 4. enable HDMI clock gating
	 */

	if (pipe_config->pipe_bpp > 24) {
		intel_de_write(dev_priv, TRANS_CHICKEN1(pipe),
			       intel_de_read(dev_priv, TRANS_CHICKEN1(pipe)) | TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE);

		temp &= ~SDVO_COLOR_FORMAT_MASK;
		temp |= SDVO_COLOR_FORMAT_8bpc;
	}

	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

	if (pipe_config->pipe_bpp > 24) {
		temp &= ~SDVO_COLOR_FORMAT_MASK;
		temp |= HDMI_COLOR_FORMAT_12bpc;

		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

		intel_de_write(dev_priv, TRANS_CHICKEN1(pipe),
			       intel_de_read(dev_priv, TRANS_CHICKEN1(pipe)) & ~TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE);
	}

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void vlv_enable_hdmi(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
}

static void intel_disable_hdmi(struct intel_atomic_state *state,
			       struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	struct intel_digital_port *dig_port =
		hdmi_to_dig_port(intel_hdmi);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	u32 temp;

	temp = intel_de_read(dev_priv, intel_hdmi->hdmi_reg);

	temp &= ~(SDVO_ENABLE | HDMI_AUDIO_ENABLE);
	intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
	intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

	/*
	 * HW workaround for IBX, we need to move the port
	 * to transcoder A after disabling it to allow the
	 * matching DP port to be enabled on transcoder A.
	 */
	if (HAS_PCH_IBX(dev_priv) && crtc->pipe == PIPE_B) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		temp &= ~SDVO_PIPE_SEL_MASK;
		temp |= SDVO_ENABLE | SDVO_PIPE_SEL(PIPE_A);
		/*
		 * HW workaround, need to write this twice for issue
		 * that may result in first write getting masked.
		 */
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

		temp &= ~SDVO_ENABLE;
		intel_de_write(dev_priv, intel_hdmi->hdmi_reg, temp);
		intel_de_posting_read(dev_priv, intel_hdmi->hdmi_reg);

		intel_wait_for_vblank_if_active(dev_priv, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}

	dig_port->set_infoframes(encoder,
				       false,
				       old_crtc_state, old_conn_state);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

static void g4x_disable_hdmi(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	intel_disable_hdmi(state, encoder, old_crtc_state, old_conn_state);
}

static void pch_disable_hdmi(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);
}

static void pch_post_disable_hdmi(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	intel_disable_hdmi(state, encoder, old_crtc_state, old_conn_state);
}

static void intel_hdmi_pre_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port =
		enc_to_dig_port(encoder);

	intel_hdmi_prepare(encoder, pipe_config);

	dig_port->set_infoframes(encoder,
				       pipe_config->has_infoframe,
				       pipe_config, conn_state);
}

static void vlv_hdmi_pre_enable(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	vlv_phy_pre_encoder_enable(encoder, pipe_config);

	/* HDMI 1.0V-2dB */
	vlv_set_phy_signal_level(encoder, pipe_config,
				 0x2b245f5f, 0x00002000,
				 0x5578b83a, 0x2b247878);

	dig_port->set_infoframes(encoder,
			      pipe_config->has_infoframe,
			      pipe_config, conn_state);

	g4x_enable_hdmi(state, encoder, pipe_config, conn_state);

	vlv_wait_port_ready(dev_priv, dig_port, 0x0);
}

static void vlv_hdmi_pre_pll_enable(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	intel_hdmi_prepare(encoder, pipe_config);

	vlv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_hdmi_pre_pll_enable(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	intel_hdmi_prepare(encoder, pipe_config);

	chv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_hdmi_post_pll_disable(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	chv_phy_post_pll_disable(encoder, old_crtc_state);
}

static void vlv_hdmi_post_disable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	/* Reset lanes to avoid HDMI flicker (VLV w/a) */
	vlv_phy_reset_lanes(encoder, old_crtc_state);
}

static void chv_hdmi_post_disable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	vlv_dpio_get(dev_priv);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, old_crtc_state, true);

	vlv_dpio_put(dev_priv);
}

static void chv_hdmi_pre_enable(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	chv_phy_pre_encoder_enable(encoder, pipe_config);

	/* FIXME: Program the support xxx V-dB */
	/* Use 800mV-0dB */
	chv_set_phy_signal_level(encoder, pipe_config, 128, 102, false);

	dig_port->set_infoframes(encoder,
			      pipe_config->has_infoframe,
			      pipe_config, conn_state);

	g4x_enable_hdmi(state, encoder, pipe_config, conn_state);

	vlv_wait_port_ready(dev_priv, dig_port, 0x0);

	/* Second common lane will stay alive on its own now */
	chv_phy_release_cl2_override(encoder);
}

static const struct drm_encoder_funcs intel_hdmi_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

static enum intel_hotplug_state
intel_hdmi_hotplug(struct intel_encoder *encoder,
		   struct intel_connector *connector)
{
	enum intel_hotplug_state state;

	state = intel_encoder_hotplug(encoder, connector);

	/*
	 * On many platforms the HDMI live state signal is known to be
	 * unreliable, so we can't use it to detect if a sink is connected or
	 * not. Instead we detect if it's connected based on whether we can
	 * read the EDID or not. That in turn has a problem during disconnect,
	 * since the HPD interrupt may be raised before the DDC lines get
	 * disconnected (due to how the required length of DDC vs. HPD
	 * connector pins are specified) and so we'll still be able to get a
	 * valid EDID. To solve this schedule another detection cycle if this
	 * time around we didn't detect any change in the sink's connection
	 * status.
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED && !connector->hotplug_retries)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

void g4x_hdmi_init(struct drm_i915_private *dev_priv,
		   i915_reg_t hdmi_reg, enum port port)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;

	dig_port = kzalloc(sizeof(*dig_port), GFP_KERNEL);
	if (!dig_port)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(dig_port);
		return;
	}

	intel_encoder = &dig_port->base;

	mutex_init(&dig_port->hdcp_mutex);

	drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
			 &intel_hdmi_enc_funcs, DRM_MODE_ENCODER_TMDS,
			 "HDMI %c", port_name(port));

	intel_encoder->hotplug = intel_hdmi_hotplug;
	intel_encoder->compute_config = intel_hdmi_compute_config;
	if (HAS_PCH_SPLIT(dev_priv)) {
		intel_encoder->disable = pch_disable_hdmi;
		intel_encoder->post_disable = pch_post_disable_hdmi;
	} else {
		intel_encoder->disable = g4x_disable_hdmi;
	}
	intel_encoder->get_hw_state = intel_hdmi_get_hw_state;
	intel_encoder->get_config = intel_hdmi_get_config;
	if (IS_CHERRYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = chv_hdmi_pre_pll_enable;
		intel_encoder->pre_enable = chv_hdmi_pre_enable;
		intel_encoder->enable = vlv_enable_hdmi;
		intel_encoder->post_disable = chv_hdmi_post_disable;
		intel_encoder->post_pll_disable = chv_hdmi_post_pll_disable;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = vlv_hdmi_pre_pll_enable;
		intel_encoder->pre_enable = vlv_hdmi_pre_enable;
		intel_encoder->enable = vlv_enable_hdmi;
		intel_encoder->post_disable = vlv_hdmi_post_disable;
	} else {
		intel_encoder->pre_enable = intel_hdmi_pre_enable;
		if (HAS_PCH_CPT(dev_priv))
			intel_encoder->enable = cpt_enable_hdmi;
		else if (HAS_PCH_IBX(dev_priv))
			intel_encoder->enable = ibx_enable_hdmi;
		else
			intel_encoder->enable = g4x_enable_hdmi;
	}

	intel_encoder->type = INTEL_OUTPUT_HDMI;
	intel_encoder->power_domain = intel_port_to_power_domain(port);
	intel_encoder->port = port;
	if (IS_CHERRYVIEW(dev_priv)) {
		if (port == PORT_D)
			intel_encoder->pipe_mask = BIT(PIPE_C);
		else
			intel_encoder->pipe_mask = BIT(PIPE_A) | BIT(PIPE_B);
	} else {
		intel_encoder->pipe_mask = ~0;
	}
	intel_encoder->cloneable = 1 << INTEL_OUTPUT_ANALOG;
	intel_encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);
	/*
	 * BSpec is unclear about HDMI+HDMI cloning on g4x, but it seems
	 * to work on real hardware. And since g4x can send infoframes to
	 * only one port anyway, nothing is lost by allowing it.
	 */
	if (IS_G4X(dev_priv))
		intel_encoder->cloneable |= 1 << INTEL_OUTPUT_HDMI;

	dig_port->hdmi.hdmi_reg = hdmi_reg;
	dig_port->dp.output_reg = INVALID_MMIO_REG;
	dig_port->max_lanes = 4;

	intel_infoframe_init(dig_port);

	dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);
	intel_hdmi_init_connector(dig_port, intel_connector);
}
