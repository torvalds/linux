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
#include "i915_reg.h"
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

static int intel_dp_mst_check_constraints(struct drm_i915_private *i915, int bpp,
					  const struct drm_display_mode *adjusted_mode,
					  struct intel_crtc_state *crtc_state,
					  bool dsc)
{
	if (intel_dp_is_uhbr(crtc_state) && DISPLAY_VER(i915) <= 13 && dsc) {
		int output_bpp = bpp;
		/* DisplayPort 2 128b/132b, bits per lane is always 32 */
		int symbol_clock = crtc_state->port_clock / 32;

		if (output_bpp * adjusted_mode->crtc_clock >=
		    symbol_clock * 72) {
			drm_dbg_kms(&i915->drm, "UHBR check failed(required bw %d available %d)\n",
				    output_bpp * adjusted_mode->crtc_clock, symbol_clock * 72);
			return -EINVAL;
		}
	}

	return 0;
}

static int intel_dp_mst_find_vcpi_slots_for_bpp(struct intel_encoder *encoder,
						struct intel_crtc_state *crtc_state,
						int max_bpp,
						int min_bpp,
						struct link_config_limits *limits,
						struct drm_connector_state *conn_state,
						int step,
						bool dsc)
{
	struct drm_atomic_state *state = crtc_state->uapi.state;
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct drm_dp_mst_topology_state *mst_state;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int bpp, slots = -EINVAL;
	int ret = 0;

	mst_state = drm_atomic_get_mst_topology_state(state, &intel_dp->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	crtc_state->lane_count = limits->max_lane_count;
	crtc_state->port_clock = limits->max_rate;

	mst_state->pbn_div = drm_dp_get_vc_payload_bw(&intel_dp->mst_mgr,
						      crtc_state->port_clock,
						      crtc_state->lane_count);

	for (bpp = max_bpp; bpp >= min_bpp; bpp -= step) {
		drm_dbg_kms(&i915->drm, "Trying bpp %d\n", bpp);

		ret = intel_dp_mst_check_constraints(i915, bpp, adjusted_mode, crtc_state, dsc);
		if (ret)
			continue;

		crtc_state->pbn = drm_dp_calc_pbn_mode(adjusted_mode->crtc_clock,
						       dsc ? bpp << 4 : bpp,
						       dsc);

		slots = drm_dp_atomic_find_time_slots(state, &intel_dp->mst_mgr,
						      connector->port,
						      crtc_state->pbn);
		if (slots == -EDEADLK)
			return slots;

		if (slots >= 0) {
			ret = drm_dp_mst_atomic_check(state);
			/*
			 * If we got slots >= 0 and we can fit those based on check
			 * then we can exit the loop. Otherwise keep trying.
			 */
			if (!ret)
				break;
		}
	}

	/* We failed to find a proper bpp/timeslots, return error */
	if (ret)
		slots = ret;

	if (slots < 0) {
		drm_dbg_kms(&i915->drm, "failed finding vcpi slots:%d\n",
			    slots);
	} else {
		if (!dsc)
			crtc_state->pipe_bpp = bpp;
		else
			crtc_state->dsc.compressed_bpp = bpp;
		drm_dbg_kms(&i915->drm, "Got %d slots for pipe bpp %d dsc %d\n", slots, bpp, dsc);
	}

	return slots;
}

static int intel_dp_mst_compute_link_config(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    struct link_config_limits *limits)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int slots = -EINVAL;
	int link_bpp;

	/*
	 * FIXME: allocate the BW according to link_bpp, which in the case of
	 * YUV420 is only half of the pipe bpp value.
	 */
	slots = intel_dp_mst_find_vcpi_slots_for_bpp(encoder, crtc_state,
						     to_bpp_int(limits->link.max_bpp_x16),
						     to_bpp_int(limits->link.min_bpp_x16),
						     limits,
						     conn_state, 2 * 3, false);

	if (slots < 0)
		return slots;

	link_bpp = intel_dp_output_bpp(crtc_state->output_format, crtc_state->pipe_bpp);

	intel_link_compute_m_n(link_bpp,
			       crtc_state->lane_count,
			       adjusted_mode->crtc_clock,
			       crtc_state->port_clock,
			       &crtc_state->dp_m_n,
			       crtc_state->fec_enable);
	crtc_state->dp_m_n.tu = slots;

	return 0;
}

static int intel_dp_dsc_mst_compute_link_config(struct intel_encoder *encoder,
						struct intel_crtc_state *crtc_state,
						struct drm_connector_state *conn_state,
						struct link_config_limits *limits)
{
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int slots = -EINVAL;
	int i, num_bpc;
	u8 dsc_bpc[3] = {};
	int min_bpp, max_bpp, sink_min_bpp, sink_max_bpp;
	u8 dsc_max_bpc;
	bool need_timeslot_recalc = false;
	u32 last_compressed_bpp;

	/* Max DSC Input BPC for ICL is 10 and for TGL+ is 12 */
	if (DISPLAY_VER(i915) >= 12)
		dsc_max_bpc = min_t(u8, 12, conn_state->max_requested_bpc);
	else
		dsc_max_bpc = min_t(u8, 10, conn_state->max_requested_bpc);

	max_bpp = min_t(u8, dsc_max_bpc * 3, limits->pipe.max_bpp);
	min_bpp = limits->pipe.min_bpp;

	num_bpc = drm_dp_dsc_sink_supported_input_bpcs(connector->dp.dsc_dpcd,
						       dsc_bpc);

	drm_dbg_kms(&i915->drm, "DSC Source supported min bpp %d max bpp %d\n",
		    min_bpp, max_bpp);

	sink_max_bpp = dsc_bpc[0] * 3;
	sink_min_bpp = sink_max_bpp;

	for (i = 1; i < num_bpc; i++) {
		if (sink_min_bpp > dsc_bpc[i] * 3)
			sink_min_bpp = dsc_bpc[i] * 3;
		if (sink_max_bpp < dsc_bpc[i] * 3)
			sink_max_bpp = dsc_bpc[i] * 3;
	}

	drm_dbg_kms(&i915->drm, "DSC Sink supported min bpp %d max bpp %d\n",
		    sink_min_bpp, sink_max_bpp);

	if (min_bpp < sink_min_bpp)
		min_bpp = sink_min_bpp;

	if (max_bpp > sink_max_bpp)
		max_bpp = sink_max_bpp;

	min_bpp = max(min_bpp, to_bpp_int_roundup(limits->link.min_bpp_x16));
	max_bpp = min(max_bpp, to_bpp_int(limits->link.max_bpp_x16));

	slots = intel_dp_mst_find_vcpi_slots_for_bpp(encoder, crtc_state, max_bpp,
						     min_bpp, limits,
						     conn_state, 2 * 3, true);

	if (slots < 0)
		return slots;

	last_compressed_bpp = crtc_state->dsc.compressed_bpp;

	crtc_state->dsc.compressed_bpp = intel_dp_dsc_nearest_valid_bpp(i915,
									last_compressed_bpp,
									crtc_state->pipe_bpp);

	if (crtc_state->dsc.compressed_bpp != last_compressed_bpp)
		need_timeslot_recalc = true;

	/*
	 * Apparently some MST hubs dislike if vcpi slots are not matching precisely
	 * the actual compressed bpp we use.
	 */
	if (need_timeslot_recalc) {
		slots = intel_dp_mst_find_vcpi_slots_for_bpp(encoder, crtc_state,
							     crtc_state->dsc.compressed_bpp,
							     crtc_state->dsc.compressed_bpp,
							     limits, conn_state, 2 * 3, true);
		if (slots < 0)
			return slots;
	}

	intel_link_compute_m_n(crtc_state->dsc.compressed_bpp,
			       crtc_state->lane_count,
			       adjusted_mode->crtc_clock,
			       crtc_state->port_clock,
			       &crtc_state->dp_m_n,
			       crtc_state->fec_enable);
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

static bool
intel_dp_mst_compute_config_limits(struct intel_dp *intel_dp,
				   struct intel_crtc_state *crtc_state,
				   bool dsc,
				   struct link_config_limits *limits)
{
	/*
	 * for MST we always configure max link bw - the spec doesn't
	 * seem to suggest we should do otherwise.
	 */
	limits->min_rate = limits->max_rate =
		intel_dp_max_link_rate(intel_dp);

	limits->min_lane_count = limits->max_lane_count =
		intel_dp_max_lane_count(intel_dp);

	limits->pipe.min_bpp = intel_dp_min_bpp(crtc_state->output_format);
	/*
	 * FIXME: If all the streams can't fit into the link with
	 * their current pipe_bpp we should reduce pipe_bpp across
	 * the board until things start to fit. Until then we
	 * limit to <= 8bpc since that's what was hardcoded for all
	 * MST streams previously. This hack should be removed once
	 * we have the proper retry logic in place.
	 */
	limits->pipe.max_bpp = min(crtc_state->pipe_bpp, 24);

	intel_dp_adjust_compliance_config(intel_dp, crtc_state, limits);

	return intel_dp_compute_config_link_bpp_limits(intel_dp,
						       crtc_state,
						       dsc,
						       limits);
}

static int intel_dp_mst_compute_config(struct intel_encoder *encoder,
				       struct intel_crtc_state *pipe_config,
				       struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct link_config_limits limits;
	bool dsc_needed;
	int ret = 0;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->has_pch_encoder = false;

	dsc_needed = intel_dp->force_dsc_en ||
		     !intel_dp_mst_compute_config_limits(intel_dp,
							 pipe_config,
							 false,
							 &limits);

	if (!dsc_needed) {
		ret = intel_dp_mst_compute_link_config(encoder, pipe_config,
						       conn_state, &limits);

		if (ret == -EDEADLK)
			return ret;

		if (ret)
			dsc_needed = true;
	}

	/* enable compression if the mode doesn't fit available BW */
	if (dsc_needed) {
		drm_dbg_kms(&dev_priv->drm, "Try DSC (fallback=%s, force=%s)\n",
			    str_yes_no(ret),
			    str_yes_no(intel_dp->force_dsc_en));

		if (!intel_dp_mst_compute_config_limits(intel_dp,
							pipe_config,
							true,
							&limits))
			return -EINVAL;

		/*
		 * FIXME: As bpc is hardcoded to 8, as mentioned above,
		 * WARN and ignore the debug flag force_dsc_bpc for now.
		 */
		drm_WARN(&dev_priv->drm, intel_dp->force_dsc_bpc, "Cannot Force BPC for MST\n");
		/*
		 * Try to get at least some timeslots and then see, if
		 * we can fit there with DSC.
		 */
		drm_dbg_kms(&dev_priv->drm, "Trying to find VCPI slots in DSC mode\n");

		ret = intel_dp_dsc_mst_compute_link_config(encoder, pipe_config,
							   conn_state, &limits);
		if (ret < 0)
			return ret;

		ret = intel_dp_dsc_compute_config(intel_dp, pipe_config,
						  conn_state, &limits,
						  pipe_config->dp_m_n.tu, false);
	}

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

	intel_dp_audio_compute_config(encoder, pipe_config, conn_state);

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
	struct intel_connector *intel_connector =
		to_intel_connector(connector);
	int ret;

	ret = intel_digital_connector_atomic_check(connector, &state->base);
	if (ret)
		return ret;

	ret = intel_dp_mst_atomic_master_trans_check(intel_connector, state);
	if (ret)
		return ret;

	return drm_dp_atomic_release_time_slots(&state->base,
						&intel_connector->mst_port->mst_mgr,
						intel_connector->port);
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
	struct drm_dp_mst_topology_state *new_mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	struct drm_dp_mst_atomic_payload *new_payload =
		drm_atomic_get_mst_payload_state(new_mst_state, connector->port);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	drm_dbg_kms(&i915->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	intel_hdcp_disable(intel_mst->connector);

	drm_dp_remove_payload_part1(&intel_dp->mst_mgr, new_mst_state, new_payload);

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
	struct drm_dp_mst_topology_state *old_mst_state =
		drm_atomic_get_old_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	struct drm_dp_mst_topology_state *new_mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	const struct drm_dp_mst_atomic_payload *old_payload =
		drm_atomic_get_mst_payload_state(old_mst_state, connector->port);
	struct drm_dp_mst_atomic_payload *new_payload =
		drm_atomic_get_mst_payload_state(new_mst_state, connector->port);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	bool last_mst_stream;

	intel_dp->active_mst_links--;
	last_mst_stream = intel_dp->active_mst_links == 0;
	drm_WARN_ON(&dev_priv->drm,
		    DISPLAY_VER(dev_priv) >= 12 && last_mst_stream &&
		    !intel_dp_mst_is_master_trans(old_crtc_state));

	intel_crtc_vblank_off(old_crtc_state);

	intel_disable_transcoder(old_crtc_state);

	clear_act_sent(encoder, old_crtc_state);

	intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(old_crtc_state->cpu_transcoder),
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC, 0);

	wait_for_act_sent(encoder, old_crtc_state);

	drm_dp_remove_payload_part2(&intel_dp->mst_mgr, new_mst_state,
				    old_payload, new_payload);

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
		intel_ddi_disable_transcoder_clock(old_crtc_state);


	intel_mst->connector = NULL;
	if (last_mst_stream)
		dig_port->base.post_disable(state, &dig_port->base,
						  old_crtc_state, NULL);

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);
}

static void intel_mst_post_pll_disable_dp(struct intel_atomic_state *state,
					  struct intel_encoder *encoder,
					  const struct intel_crtc_state *old_crtc_state,
					  const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;

	if (intel_dp->active_mst_links == 0 &&
	    dig_port->base.post_pll_disable)
		dig_port->base.post_pll_disable(state, encoder, old_crtc_state, old_conn_state);
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
	else
		/*
		 * The port PLL state needs to get updated for secondary
		 * streams as for the primary stream.
		 */
		intel_ddi_update_active_dpll(state, &dig_port->base,
					     to_intel_crtc(pipe_config->uapi.crtc));
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
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
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

	intel_dp->active_mst_links++;

	ret = drm_dp_add_payload_part1(&intel_dp->mst_mgr, mst_state,
				       drm_atomic_get_mst_payload_state(mst_state, connector->port));
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Failed to create MST payload for %s: %d\n",
			    connector->base.name, ret);

	/*
	 * Before Gen 12 this is not done as part of
	 * dig_port->base.pre_enable() and should be done here. For
	 * Gen 12+ the step in which this should be done is different for the
	 * first MST stream, so it's done on the DDI for the first stream and
	 * here for the following ones.
	 */
	if (DISPLAY_VER(dev_priv) < 12 || !first_mst_stream)
		intel_ddi_enable_transcoder_clock(encoder, pipe_config);

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
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
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

	drm_dp_add_payload_part2(&intel_dp->mst_mgr, &state->base,
				 drm_atomic_get_mst_payload_state(mst_state, connector->port));

	if (DISPLAY_VER(dev_priv) >= 14 && pipe_config->fec_enable)
		intel_de_rmw(dev_priv, MTL_CHICKEN_TRANS(trans), 0,
			     FECSTALL_DIS_DPTSTREAM_DPTTG);
	else if (DISPLAY_VER(dev_priv) >= 12 && pipe_config->fec_enable)
		intel_de_rmw(dev_priv, CHICKEN_TRANS(trans), 0,
			     FECSTALL_DIS_DPTSTREAM_DPTTG);

	intel_audio_sdp_split_update(pipe_config);

	intel_enable_transcoder(pipe_config);

	intel_crtc_vblank_on(pipe_config);

	intel_audio_codec_enable(encoder, pipe_config, conn_state);

	/* Enable hdcp if it's desired */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED)
		intel_hdcp_enable(state, encoder, pipe_config, conn_state);
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
	const struct drm_edid *drm_edid;
	int ret;

	if (drm_connector_is_unregistered(connector))
		return intel_connector_update_modes(connector, NULL);

	drm_edid = drm_dp_mst_edid_read(connector, &intel_dp->mst_mgr, intel_connector->port);

	ret = intel_connector_update_modes(connector, drm_edid);

	drm_edid_free(drm_edid);

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
	bool dsc = false, bigjoiner = false;
	u16 dsc_max_compressed_bpp = 0;
	u8 dsc_slice_count = 0;
	int target_clock = mode->clock;

	if (drm_connector_is_unregistered(connector)) {
		*status = MODE_ERROR;
		return 0;
	}

	*status = intel_cpu_transcoder_mode_valid(dev_priv, mode);
	if (*status != MODE_OK)
		return 0;

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

	if (intel_dp_need_bigjoiner(intel_dp, mode->hdisplay, target_clock)) {
		bigjoiner = true;
		max_dotclk *= 2;

		/* TODO: add support for bigjoiner */
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (DISPLAY_VER(dev_priv) >= 10 &&
	    drm_dp_sink_supports_dsc(intel_connector->dp.dsc_dpcd)) {
		/*
		 * TBD pass the connector BPC,
		 * for now U8_MAX so that max BPC on that platform would be picked
		 */
		int pipe_bpp = intel_dp_dsc_compute_max_bpp(intel_connector, U8_MAX);

		if (drm_dp_sink_supports_fec(intel_connector->dp.fec_capability)) {
			dsc_max_compressed_bpp =
				intel_dp_dsc_get_max_compressed_bpp(dev_priv,
								    max_link_clock,
								    max_lanes,
								    target_clock,
								    mode->hdisplay,
								    bigjoiner,
								    INTEL_OUTPUT_FORMAT_RGB,
								    pipe_bpp, 64);
			dsc_slice_count =
				intel_dp_dsc_get_slice_count(intel_connector,
							     target_clock,
							     mode->hdisplay,
							     bigjoiner);
		}

		dsc = dsc_max_compressed_bpp && dsc_slice_count;
	}

	/*
	 * Big joiner configuration needs DSC for TGL which is not true for
	 * XE_LPD where uncompressed joiner is supported.
	 */
	if (DISPLAY_VER(dev_priv) < 13 && bigjoiner && !dsc) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (mode_rate > max_rate && !dsc) {
		*status = MODE_CLOCK_HIGH;
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

	if (!intel_display_device_enabled(i915))
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

static int intel_dp_mst_add_properties(struct intel_dp *intel_dp,
				       struct drm_connector *connector,
				       const char *pathprop)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);

	drm_object_attach_property(&connector->base,
				   i915->drm.mode_config.path_property, 0);
	drm_object_attach_property(&connector->base,
				   i915->drm.mode_config.tile_property, 0);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);

	/*
	 * Reuse the prop from the SST connector because we're
	 * not allowed to create new props after device registration.
	 */
	connector->max_bpc_property =
		intel_dp->attached_connector->base.max_bpc_property;
	if (connector->max_bpc_property)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

	return drm_connector_set_path_property(connector, pathprop);
}

static void
intel_dp_mst_read_decompression_port_dsc_caps(struct intel_dp *intel_dp,
					      struct intel_connector *connector)
{
	u8 dpcd_caps[DP_RECEIVER_CAP_SIZE];

	if (!connector->dp.dsc_decompression_aux)
		return;

	if (drm_dp_read_dpcd_caps(connector->dp.dsc_decompression_aux, dpcd_caps) < 0)
		return;

	intel_dp_get_dsc_sink_cap(dpcd_caps[DP_DPCD_REV], connector);
}

static struct drm_connector *intel_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
							struct drm_dp_mst_port *port,
							const char *pathprop)
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

	/*
	 * TODO: set the AUX for the actual MST port decompressing the stream.
	 * At the moment the driver only supports enabling this globally in the
	 * first downstream MST branch, via intel_dp's (root port) AUX.
	 */
	intel_connector->dp.dsc_decompression_aux = &intel_dp->aux;
	intel_dp_mst_read_decompression_port_dsc_caps(intel_dp, intel_connector);

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

	ret = intel_dp_mst_add_properties(intel_dp, connector, pathprop);
	if (ret)
		goto err;

	ret = intel_dp_hdcp_init(dig_port, intel_connector);
	if (ret)
		drm_dbg_kms(&dev_priv->drm, "[%s:%d] HDCP MST init failed, skipping.\n",
			    connector->name, connector->base.id);

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
	intel_encoder->post_pll_disable = intel_mst_post_pll_disable_dp;
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
					   &intel_dp->aux, 16, 3, conn_base_id);
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

/**
 * intel_dp_mst_add_topology_state_for_connector - add MST topology state for a connector
 * @state: atomic state
 * @connector: connector to add the state for
 * @crtc: the CRTC @connector is attached to
 *
 * Add the MST topology state for @connector to @state.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int
intel_dp_mst_add_topology_state_for_connector(struct intel_atomic_state *state,
					      struct intel_connector *connector,
					      struct intel_crtc *crtc)
{
	struct drm_dp_mst_topology_state *mst_state;

	if (!connector->mst_port)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(&state->base,
						      &connector->mst_port->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	mst_state->pending_crtc_mask |= drm_crtc_mask(&crtc->base);

	return 0;
}

/**
 * intel_dp_mst_add_topology_state_for_crtc - add MST topology state for a CRTC
 * @state: atomic state
 * @crtc: CRTC to add the state for
 *
 * Add the MST topology state for @crtc to @state.
 *
 * Returns 0 on success, negative error code on failure.
 */
int intel_dp_mst_add_topology_state_for_crtc(struct intel_atomic_state *state,
					     struct intel_crtc *crtc)
{
	struct drm_connector *_connector;
	struct drm_connector_state *conn_state;
	int i;

	for_each_new_connector_in_state(&state->base, _connector, conn_state, i) {
		struct intel_connector *connector = to_intel_connector(_connector);
		int ret;

		if (conn_state->crtc != &crtc->base)
			continue;

		ret = intel_dp_mst_add_topology_state_for_connector(state, connector, crtc);
		if (ret)
			return ret;
	}

	return 0;
}
