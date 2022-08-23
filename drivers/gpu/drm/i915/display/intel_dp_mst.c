/*
 * Copyright © 2008 Intel Corporation
 *             2014 Red Hat Inc.
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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_hdcp.h"
#include "intel_dp_mst.h"
#include "intel_dpio_phy.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "skl_scaler.h"

static int intel_dp_mst_compute_link_config(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    struct link_config_limits *limits)
{
	struct drm_atomic_state *state = crtc_state->uapi.state;
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	bool constant_n = drm_dp_has_quirk(&intel_dp->desc, DP_DPCD_QUIRK_CONSTANT_N);
	int bpp, slots = -EINVAL;

	crtc_state->lane_count = limits->max_lane_count;
	crtc_state->port_clock = limits->max_rate;

	for (bpp = limits->max_bpp; bpp >= limits->min_bpp; bpp -= 2 * 3) {
		crtc_state->pipe_bpp = bpp;

		crtc_state->pbn = drm_dp_calc_pbn_mode(adjusted_mode->crtc_clock,
						       crtc_state->pipe_bpp,
						       false);

		slots = drm_dp_atomic_find_vcpi_slots(state, &intel_dp->mst_mgr,
						      connector->port,
						      crtc_state->pbn,
						      drm_dp_get_vc_payload_bw(&intel_dp->mst_mgr,
									       crtc_state->port_clock,
									       crtc_state->lane_count));
		if (slots == -EDEADLK)
			return slots;
		if (slots >= 0)
			break;
	}

	if (slots < 0) {
		drm_dbg_kms(&i915->drm, "failed finding vcpi slots:%d\n",
			    slots);
		return slots;
	}

	intel_link_compute_m_n(crtc_state->pipe_bpp,
			       crtc_state->lane_count,
			       adjusted_mode->crtc_clock,
			       crtc_state->port_clock,
			       &crtc_state->dp_m_n,
			       constant_n, crtc_state->fec_enable);
	crtc_state->dp_m_n.tu = slots;

	return 0;
}

static int intel_dp_mst_update_slots(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst_mgr;
	struct drm_dp_mst_topology_state *topology_state;
	u8 link_coding_cap = intel_dp_is_uhbr(crtc_state) ?
		DP_CAP_ANSI_128B132B : DP_CAP_ANSI_8B10B;

	topology_state = drm_atomic_get_mst_topology_state(conn_state->state, mgr);
	if (IS_ERR(topology_state)) {
		drm_dbg_kms(&i915->drm, "slot update failed\n");
		return PTR_ERR(topology_state);
	}

	drm_dp_mst_update_slots(topology_state, link_coding_cap);

	return 0;
}

static int intel_dp_mst_compute_config(struct intel_encoder *encoder,
				       struct intel_crtc_state *pipe_config,
				       struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct link_config_limits limits;
	int ret;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->has_pch_encoder = false;

	if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
		pipe_config->has_audio = connector->port->has_audio;
	else
		pipe_config->has_audio =
			intel_conn_state->force_audio == HDMI_AUDIO_ON;

	/*
	 * for MST we always configure max link bw - the spec doesn't
	 * seem to suggest we should do otherwise.
	 */
	limits.min_rate =
	limits.max_rate = intel_dp_max_link_rate(intel_dp);

	limits.min_lane_count =
	limits.max_lane_count = intel_dp_max_lane_count(intel_dp);

	limits.min_bpp = intel_dp_min_bpp(pipe_config->output_format);
	/*
	 * FIXME: If all the streams can't fit into the link with
	 * their current pipe_bpp we should reduce pipe_bpp across
	 * the board until things start to fit. Until then we
	 * limit to <= 8bpc since that's what was hardcoded for all
	 * MST streams previously. This hack should be removed once
	 * we have the proper retry logic in place.
	 */
	limits.max_bpp = min(pipe_config->pipe_bpp, 24);

	intel_dp_adjust_compliance_config(intel_dp, pipe_config, &limits);

	ret = intel_dp_mst_compute_link_config(encoder, pipe_config,
					       conn_state, &limits);
	if (ret)
		return ret;

	ret = intel_dp_mst_update_slots(encoder, pipe_config, conn_state);
	if (ret)
		return ret;

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_calc_lane_lat_optim_mask(pipe_config->lane_count);

	intel_ddi_compute_min_voltage_level(dev_priv, pipe_config);

	return 0;
}

/*
 * Iterate over all connectors and return a mask of
 * all CPU transcoders streaming over the same DP link.
 */
static unsigned int
intel_dp_mst_transcoder_mask(struct intel_atomic_state *state,
			     struct intel_dp *mst_port)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	const struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	u8 transcoders = 0;
	int i;

	if (DISPLAY_VER(dev_priv) < 12)
		return 0;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		const struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector->mst_port != mst_port || !conn_state->base.crtc)
			continue;

		crtc = to_intel_crtc(conn_state->base.crtc);
		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		if (!crtc_state->hw.active)
			continue;

		transcoders |= BIT(crtc_state->cpu_transcoder);
	}

	return transcoders;
}

static int intel_dp_mst_compute_config_late(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(conn_state->state);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;

	/* lowest numbered transcoder will be designated master */
	crtc_state->mst_master_transcoder =
		ffs(intel_dp_mst_transcoder_mask(state, intel_dp)) - 1;

	return 0;
}

/*
 * If one of the connectors in a MST stream needs a modeset, mark all CRTCs
 * that shares the same MST stream as mode changed,
 * intel_modeset_pipe_config()+intel_crtc_check_fastset() will take care to do
 * a fastset when possible.
 */
static int
intel_dp_mst_atomic_master_trans_check(struct intel_connector *connector,
				       struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct drm_connector_list_iter connector_list_iter;
	struct intel_connector *connector_iter;
	int ret = 0;

	if (DISPLAY_VER(dev_priv) < 12)
		return  0;

	if (!intel_connector_needs_modeset(state, &connector->base))
		return 0;

	drm_connector_list_iter_begin(&dev_priv->drm, &connector_list_iter);
	for_each_intel_connector_iter(connector_iter, &connector_list_iter) {
		struct intel_digital_connector_state *conn_iter_state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector_iter->mst_port != connector->mst_port ||
		    connector_iter == connector)
			continue;

		conn_iter_state = intel_atomic_get_digital_connector_state(state,
									   connector_iter);
		if (IS_ERR(conn_iter_state)) {
			ret = PTR_ERR(conn_iter_state);
			break;
		}

		if (!conn_iter_state->base.crtc)
			continue;

		crtc = to_intel_crtc(conn_iter_state->base.crtc);
		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			break;
		}

		ret = drm_atomic_add_affected_planes(&state->base, &crtc->base);
		if (ret)
			break;
		crtc_state->uapi.mode_changed = true;
	}
	drm_connector_list_iter_end(&connector_list_iter);

	return ret;
}

static int
intel_dp_mst_atomic_check(struct drm_connector *connector,
			  struct drm_atomic_state *_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(&state->base, connector);
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(&state->base, connector);
	struct intel_connector *intel_connector =
		to_intel_connector(connector);
	struct drm_crtc *new_crtc = new_conn_state->crtc;
	struct drm_dp_mst_topology_mgr *mgr;
	int ret;

	ret = intel_digital_connector_atomic_check(connector, &state->base);
	if (ret)
		return ret;

	ret = intel_dp_mst_atomic_master_trans_check(intel_connector, state);
	if (ret)
		return ret;

	if (!old_conn_state->crtc)
		return 0;

	/* We only want to free VCPI if this state disables the CRTC on this
	 * connector
	 */
	if (new_crtc) {
		struct intel_crtc *crtc = to_intel_crtc(new_crtc);
		struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state, crtc);

		if (!crtc_state ||
		    !drm_atomic_crtc_needs_modeset(&crtc_state->uapi) ||
		    crtc_state->uapi.enable)
			return 0;
	}

	mgr = &enc_to_mst(to_intel_encoder(old_conn_state->best_encoder))->primary->dp.mst_mgr;
	ret = drm_dp_atomic_release_vcpi_slots(&state->base, mgr,
					       intel_connector->port);

	return ret;
}

static void clear_act_sent(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_de_write(i915, dp_tp_status_reg(encoder, crtc_state),
		       DP_TP_STATUS_ACT_SENT);
}

static void wait_for_act_sent(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;

	if (intel_de_wait_for_set(i915, dp_tp_status_reg(encoder, crtc_state),
				  DP_TP_STATUS_ACT_SENT, 1))
		drm_err(&i915->drm, "Timed out waiting for ACT sent\n");

	drm_dp_check_act_status(&intel_dp->mst_mgr);
}

static void intel_mst_disable_dp(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int start_slot = intel_dp_is_uhbr(old_crtc_state) ? 0 : 1;
	int ret;

	drm_dbg_kms(&i915->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	intel_hdcp_disable(intel_mst->connector);

	drm_dp_mst_reset_vcpi_slots(&intel_dp->mst_mgr, connector->port);

	ret = drm_dp_update_payload_part1(&intel_dp->mst_mgr, start_slot);
	if (ret) {
		drm_dbg_kms(&i915->drm, "failed to update payload %d\n", ret);
	}

	intel_audio_codec_disable(encoder, old_crtc_state, old_conn_state);
}

static void intel_mst_post_disable_dp(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	bool last_mst_stream;

	intel_dp->active_mst_links--;
	last_mst_stream = intel_dp->active_mst_links == 0;
	drm_WARN_ON(&dev_priv->drm,
		    DISPLAY_VER(dev_priv) >= 12 && last_mst_stream &&
		    !intel_dp_mst_is_master_trans(old_crtc_state));

	intel_crtc_vblank_off(old_crtc_state);

	intel_disable_transcoder(old_crtc_state);

	drm_dp_update_payload_part2(&intel_dp->mst_mgr);

	clear_act_sent(encoder, old_crtc_state);

	intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(old_crtc_state->cpu_transcoder),
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC, 0);

	wait_for_act_sent(encoder, old_crtc_state);

	drm_dp_mst_deallocate_vcpi(&intel_dp->mst_mgr, connector->port);

	intel_ddi_disable_transcoder_func(old_crtc_state);

	if (DISPLAY_VER(dev_priv) >= 9)
		skl_scaler_disable(old_crtc_state);
	else
		ilk_pfit_disable(old_crtc_state);

	/*
	 * Power down mst path before disabling the port, otherwise we end
	 * up getting interrupts from the sink upon detecting link loss.
	 */
	drm_dp_send_power_updown_phy(&intel_dp->mst_mgr, connector->port,
				     false);

	/*
	 * BSpec 4287: disable DIP after the transcoder is disabled and before
	 * the transcoder clock select is set to none.
	 */
	if (last_mst_stream)
		intel_dp_set_infoframes(&dig_port->base, false,
					old_crtc_state, NULL);
	/*
	 * From TGL spec: "If multi-stream slave transcoder: Configure
	 * Transcoder Clock Select to direct no clock to the transcoder"
	 *
	 * From older GENs spec: "Configure Transcoder Clock Select to direct
	 * no clock to the transcoder"
	 */
	if (DISPLAY_VER(dev_priv) < 12 || !last_mst_stream)
		intel_ddi_disable_pipe_clock(old_crtc_state);


	intel_mst->connector = NULL;
	if (last_mst_stream)
		dig_port->base.post_disable(state, &dig_port->base,
						  old_crtc_state, NULL);

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);
}

static void intel_mst_pre_pll_enable_dp(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *pipe_config,
					const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;

	if (intel_dp->active_mst_links == 0)
		dig_port->base.pre_pll_enable(state, &dig_port->base,
						    pipe_config, NULL);
}

static void intel_mst_pre_enable_dp(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	int start_slot = intel_dp_is_uhbr(pipe_config) ? 0 : 1;
	int ret;
	bool first_mst_stream;

	/* MST encoders are bound to a crtc, not to a connector,
	 * force the mapping here for get_hw_state.
	 */
	connector->encoder = encoder;
	intel_mst->connector = connector;
	first_mst_stream = intel_dp->active_mst_links == 0;
	drm_WARN_ON(&dev_priv->drm,
		    DISPLAY_VER(dev_priv) >= 12 && first_mst_stream &&
		    !intel_dp_mst_is_master_trans(pipe_config));

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	if (first_mst_stream)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);

	drm_dp_send_power_updown_phy(&intel_dp->mst_mgr, connector->port, true);

	if (first_mst_stream)
		dig_port->base.pre_enable(state, &dig_port->base,
						pipe_config, NULL);

	ret = drm_dp_mst_allocate_vcpi(&intel_dp->mst_mgr,
				       connector->port,
				       pipe_config->pbn,
				       pipe_config->dp_m_n.tu);
	if (!ret)
		drm_err(&dev_priv->drm, "failed to allocate vcpi\n");

	intel_dp->active_mst_links++;

	ret = drm_dp_update_payload_part1(&intel_dp->mst_mgr, start_slot);

	/*
	 * Before Gen 12 this is not done as part of
	 * dig_port->base.pre_enable() and should be done here. For
	 * Gen 12+ the step in which this should be done is different for the
	 * first MST stream, so it's done on the DDI for the first stream and
	 * here for the following ones.
	 */
	if (DISPLAY_VER(dev_priv) < 12 || !first_mst_stream)
		intel_ddi_enable_pipe_clock(encoder, pipe_config);

	intel_ddi_set_dp_msa(pipe_config, conn_state);
}

static void intel_mst_enable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder trans = pipe_config->cpu_transcoder;

	drm_WARN_ON(&dev_priv->drm, pipe_config->has_pch_encoder);

	clear_act_sent(encoder, pipe_config);

	if (intel_dp_is_uhbr(pipe_config)) {
		const struct drm_display_mode *adjusted_mode =
			&pipe_config->hw.adjusted_mode;
		u64 crtc_clock_hz = KHz(adjusted_mode->crtc_clock);

		intel_de_write(dev_priv, TRANS_DP2_VFREQHIGH(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz >> 24));
		intel_de_write(dev_priv, TRANS_DP2_VFREQLOW(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz & 0xffffff));
	}

	intel_ddi_enable_transcoder_func(encoder, pipe_config);

	intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(trans), 0,
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC);

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	wait_for_act_sent(encoder, pipe_config);

	drm_dp_update_payload_part2(&intel_dp->mst_mgr);

	if (DISPLAY_VER(dev_priv) >= 12 && pipe_config->fec_enable)
		intel_de_rmw(dev_priv, CHICKEN_TRANS(trans), 0,
			     FECSTALL_DIS_DPTSTREAM_DPTTG);

	intel_enable_transcoder(pipe_config);

	intel_crtc_vblank_on(pipe_config);

	intel_audio_codec_enable(encoder, pipe_config, conn_state);

	/* Enable hdcp if it's desired */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED)
		intel_hdcp_enable(to_intel_connector(conn_state->connector),
				  pipe_config,
				  (u8)conn_state->hdcp_content_type);
}

static bool intel_dp_mst_enc_get_hw_state(struct intel_encoder *encoder,
				      enum pipe *pipe)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	*pipe = intel_mst->pipe;
	if (intel_mst->connector)
		return true;
	return false;
}

static void intel_dp_mst_enc_get_config(struct intel_encoder *encoder,
					struct intel_crtc_state *pipe_config)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	dig_port->base.get_config(&dig_port->base, pipe_config);
}

static bool intel_dp_mst_initial_fastset_check(struct intel_encoder *encoder,
					       struct intel_crtc_state *crtc_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	return intel_dp_initial_fastset_check(&dig_port->base, crtc_state);
}

static int intel_dp_mst_get_ddc_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct edid *edid;
	int ret;

	if (drm_connector_is_unregistered(connector))
		return intel_connector_update_modes(connector, NULL);

	edid = drm_dp_mst_get_edid(connector, &intel_dp->mst_mgr, intel_connector->port);
	ret = intel_connector_update_modes(connector, edid);
	kfree(edid);

	return ret;
}

static int
intel_dp_mst_connector_late_register(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	int ret;

	ret = drm_dp_mst_connector_late_register(connector,
						 intel_connector->port);
	if (ret < 0)
		return ret;

	ret = intel_connector_register(connector);
	if (ret < 0)
		drm_dp_mst_connector_early_unregister(connector,
						      intel_connector->port);

	return ret;
}

static void
intel_dp_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_connector_unregister(connector);
	drm_dp_mst_connector_early_unregister(connector,
					      intel_connector->port);
}

static const struct drm_connector_funcs intel_dp_mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_dp_mst_connector_late_register,
	.early_unregister = intel_dp_mst_connector_early_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static int intel_dp_mst_get_modes(struct drm_connector *connector)
{
	return intel_dp_mst_get_ddc_modes(connector);
}

static int
intel_dp_mst_mode_valid_ctx(struct drm_connector *connector,
			    struct drm_display_mode *mode,
			    struct drm_modeset_acquire_ctx *ctx,
			    enum drm_mode_status *status)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst_mgr;
	struct drm_dp_mst_port *port = intel_connector->port;
	const int min_bpp = 18;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int ret;

	if (drm_connector_is_unregistered(connector)) {
		*status = MODE_ERROR;
		return 0;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		*status = MODE_NO_DBLESCAN;
		return 0;
	}

	max_link_clock = intel_dp_max_link_rate(intel_dp);
	max_lanes = intel_dp_max_lane_count(intel_dp);

	max_rate = intel_dp_max_data_rate(max_link_clock, max_lanes);
	mode_rate = intel_dp_link_required(mode->clock, min_bpp);

	ret = drm_modeset_lock(&mgr->base.lock, ctx);
	if (ret)
		return ret;

	if (mode_rate > max_rate || mode->clock > max_dotclk ||
	    drm_dp_calc_pbn_mode(mode->clock, min_bpp, false) > port->full_pbn) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (mode->clock < 10000) {
		*status = MODE_CLOCK_LOW;
		return 0;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		*status = MODE_H_ILLEGAL;
		return 0;
	}

	*status = intel_mode_valid_max_plane_size(dev_priv, mode, false);
	return 0;
}

static struct drm_encoder *intel_mst_atomic_best_encoder(struct drm_connector *connector,
							 struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state = drm_atomic_get_new_connector_state(state,
											 connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct intel_crtc *crtc = to_intel_crtc(connector_state->crtc);

	return &intel_dp->mst_encoders[crtc->pipe]->base.base;
}

static int
intel_dp_mst_detect(struct drm_connector *connector,
		    struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;

	if (!INTEL_DISPLAY_ENABLED(i915))
		return connector_status_disconnected;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	return drm_dp_mst_detect_port(connector, ctx, &intel_dp->mst_mgr,
				      intel_connector->port);
}

static const struct drm_connector_helper_funcs intel_dp_mst_connector_helper_funcs = {
	.get_modes = intel_dp_mst_get_modes,
	.mode_valid_ctx = intel_dp_mst_mode_valid_ctx,
	.atomic_best_encoder = intel_mst_atomic_best_encoder,
	.atomic_check = intel_dp_mst_atomic_check,
	.detect_ctx = intel_dp_mst_detect,
};

static void intel_dp_mst_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(to_intel_encoder(encoder));

	drm_encoder_cleanup(encoder);
	kfree(intel_mst);
}

static const struct drm_encoder_funcs intel_dp_mst_enc_funcs = {
	.destroy = intel_dp_mst_encoder_destroy,
};

static bool intel_dp_mst_get_hw_state(struct intel_connector *connector)
{
	if (intel_attached_encoder(connector) && connector->base.state->crtc) {
		enum pipe pipe;
		if (!intel_attached_encoder(connector)->get_hw_state(intel_attached_encoder(connector), &pipe))
			return false;
		return true;
	}
	return false;
}

static struct drm_connector *intel_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port, const char *pathprop)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	enum pipe pipe;
	int ret;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		return NULL;

	intel_connector->get_hw_state = intel_dp_mst_get_hw_state;
	intel_connector->mst_port = intel_dp;
	intel_connector->port = port;
	drm_dp_mst_get_port_malloc(port);

	connector = &intel_connector->base;
	ret = drm_connector_init(dev, connector, &intel_dp_mst_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		drm_dp_mst_put_port_malloc(port);
		intel_connector_free(intel_connector);
		return NULL;
	}

	drm_connector_helper_add(connector, &intel_dp_mst_connector_helper_funcs);

	for_each_pipe(dev_priv, pipe) {
		struct drm_encoder *enc =
			&intel_dp->mst_encoders[pipe]->base.base;

		ret = drm_connector_attach_encoder(&intel_connector->base, enc);
		if (ret)
			goto err;
	}

	drm_object_attach_property(&connector->base, dev->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base, dev->mode_config.tile_property, 0);

	ret = drm_connector_set_path_property(connector, pathprop);
	if (ret)
		goto err;

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);

	ret = intel_dp_hdcp_init(dig_port, intel_connector);
	if (ret)
		drm_dbg_kms(&dev_priv->drm, "[%s:%d] HDCP MST init failed, skipping.\n",
			    connector->name, connector->base.id);
	/*
	 * Reuse the prop from the SST connector because we're
	 * not allowed to create new props after device registration.
	 */
	connector->max_bpc_property =
		intel_dp->attached_connector->base.max_bpc_property;
	if (connector->max_bpc_property)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

	return connector;

err:
	drm_connector_cleanup(connector);
	return NULL;
}

static void
intel_dp_mst_poll_hpd_irq(struct drm_dp_mst_topology_mgr *mgr)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);

	intel_hpd_trigger_irq(dp_to_dig_port(intel_dp));
}

static const struct drm_dp_mst_topology_cbs mst_cbs = {
	.add_connector = intel_dp_add_mst_connector,
	.poll_hpd_irq = intel_dp_mst_poll_hpd_irq,
};

static struct intel_dp_mst_encoder *
intel_dp_create_fake_mst_encoder(struct intel_digital_port *dig_port, enum pipe pipe)
{
	struct intel_dp_mst_encoder *intel_mst;
	struct intel_encoder *intel_encoder;
	struct drm_device *dev = dig_port->base.base.dev;

	intel_mst = kzalloc(sizeof(*intel_mst), GFP_KERNEL);

	if (!intel_mst)
		return NULL;

	intel_mst->pipe = pipe;
	intel_encoder = &intel_mst->base;
	intel_mst->primary = dig_port;

	drm_encoder_init(dev, &intel_encoder->base, &intel_dp_mst_enc_funcs,
			 DRM_MODE_ENCODER_DPMST, "DP-MST %c", pipe_name(pipe));

	intel_encoder->type = INTEL_OUTPUT_DP_MST;
	intel_encoder->power_domain = dig_port->base.power_domain;
	intel_encoder->port = dig_port->base.port;
	intel_encoder->cloneable = 0;
	/*
	 * This is wrong, but broken userspace uses the intersection
	 * of possible_crtcs of all the encoders of a given connector
	 * to figure out which crtcs can drive said connector. What
	 * should be used instead is the union of possible_crtcs.
	 * To keep such userspace functioning we must misconfigure
	 * this to make sure the intersection is not empty :(
	 */
	intel_encoder->pipe_mask = ~0;

	intel_encoder->compute_config = intel_dp_mst_compute_config;
	intel_encoder->compute_config_late = intel_dp_mst_compute_config_late;
	intel_encoder->disable = intel_mst_disable_dp;
	intel_encoder->post_disable = intel_mst_post_disable_dp;
	intel_encoder->update_pipe = intel_ddi_update_pipe;
	intel_encoder->pre_pll_enable = intel_mst_pre_pll_enable_dp;
	intel_encoder->pre_enable = intel_mst_pre_enable_dp;
	intel_encoder->enable = intel_mst_enable_dp;
	intel_encoder->get_hw_state = intel_dp_mst_enc_get_hw_state;
	intel_encoder->get_config = intel_dp_mst_enc_get_config;
	intel_encoder->initial_fastset_check = intel_dp_mst_initial_fastset_check;

	return intel_mst;

}

static bool
intel_dp_create_fake_mst_encoders(struct intel_digital_port *dig_port)
{
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		intel_dp->mst_encoders[pipe] = intel_dp_create_fake_mst_encoder(dig_port, pipe);
	return true;
}

int
intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port)
{
	return dig_port->dp.active_mst_links;
}

int
intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_base_id)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum port port = dig_port->base.port;
	int ret;
	int max_source_rate =
		intel_dp->source_rates[intel_dp->num_source_rates - 1];

	if (!HAS_DP_MST(i915) || intel_dp_is_edp(intel_dp))
		return 0;

	if (DISPLAY_VER(i915) < 12 && port == PORT_A)
		return 0;

	if (DISPLAY_VER(i915) < 11 && port == PORT_E)
		return 0;

	intel_dp->mst_mgr.cbs = &mst_cbs;

	/* create encoders */
	intel_dp_create_fake_mst_encoders(dig_port);
	ret = drm_dp_mst_topology_mgr_init(&intel_dp->mst_mgr, &i915->drm,
					   &intel_dp->aux, 16, 3,
					   dig_port->max_lanes,
					   max_source_rate,
					   conn_base_id);
	if (ret) {
		intel_dp->mst_mgr.cbs = NULL;
		return ret;
	}

	return 0;
}

bool intel_dp_mst_source_support(struct intel_dp *intel_dp)
{
	return intel_dp->mst_mgr.cbs;
}

void
intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port)
{
	struct intel_dp *intel_dp = &dig_port->dp;

	if (!intel_dp_mst_source_support(intel_dp))
		return;

	drm_dp_mst_topology_mgr_destroy(&intel_dp->mst_mgr);
	/* encoders will get killed by normal cleanup */

	intel_dp->mst_mgr.cbs = NULL;
}

bool intel_dp_mst_is_master_trans(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->mst_master_transcoder == crtc_state->cpu_transcoder;
}

bool intel_dp_mst_is_slave_trans(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->mst_master_transcoder != INVALID_TRANSCODER &&
	       crtc_state->mst_master_transcoder != crtc_state->cpu_transcoder;
}
