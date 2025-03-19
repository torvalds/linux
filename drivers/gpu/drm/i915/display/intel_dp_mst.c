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
#include <drm/drm_fixed.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_driver.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_hdcp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dp_test.h"
#include "intel_dp_tunnel.h"
#include "intel_dpio_phy.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_link_bw.h"
#include "intel_pfit.h"
#include "intel_psr.h"
#include "intel_vdsc.h"
#include "skl_scaler.h"

/*
 * DP MST (DisplayPort Multi-Stream Transport)
 *
 * MST support on the source depends on the platform and port. DP initialization
 * sets up MST for each MST capable encoder. This will become the primary
 * encoder for the port.
 *
 * MST initialization of each primary encoder creates MST stream encoders, one
 * per pipe, and initializes the MST topology manager. The MST stream encoders
 * are sometimes called "fake encoders", because they're virtual, not
 * physical. Thus there are (number of MST capable ports) x (number of pipes)
 * MST stream encoders in total.
 *
 * Decision to use MST for a sink happens at detect on the connector attached to
 * the primary encoder, and this will not change while the sink is connected. We
 * always use MST when possible, including for SST sinks with sideband messaging
 * support.
 *
 * The connectors for the MST streams are added and removed dynamically by the
 * topology manager. Their connection status is also determined by the topology
 * manager.
 *
 * On hardware, each transcoder may be associated with a single DDI
 * port. Multiple transcoders may be associated with the same DDI port only if
 * the port is in MST mode.
 *
 * On TGL+, all the transcoders streaming on the same DDI port will indicate a
 * primary transcoder; the TGL_DP_TP_CTL and TGL_DP_TP_STATUS registers are
 * relevant only on the primary transcoder. Prior to that, they are port
 * registers.
 */

/* From fake MST stream encoder to primary encoder */
static struct intel_encoder *to_primary_encoder(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	return &dig_port->base;
}

/* From fake MST stream encoder to primary DP */
static struct intel_dp *to_primary_dp(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	return &dig_port->dp;
}

static int intel_dp_mst_max_dpt_bpp(const struct intel_crtc_state *crtc_state,
				    bool dsc)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!intel_dp_is_uhbr(crtc_state) || DISPLAY_VER(display) >= 20 || !dsc)
		return 0;

	/*
	 * DSC->DPT interface width:
	 *   ICL-MTL: 72 bits (each branch has 72 bits, only left branch is used)
	 *   LNL+:    144 bits (not a bottleneck in any config)
	 *
	 * Bspec/49259 suggests that the FEC overhead needs to be
	 * applied here, though HW people claim that neither this FEC
	 * or any other overhead is applicable here (that is the actual
	 * available_bw is just symbol_clock * 72). However based on
	 * testing on MTL-P the
	 * - DELL U3224KBA display
	 * - Unigraf UCD-500 CTS test sink
	 * devices the
	 * - 5120x2880/995.59Mhz
	 * - 6016x3384/1357.23Mhz
	 * - 6144x3456/1413.39Mhz
	 * modes (all the ones having a DPT limit on the above devices),
	 * both the channel coding efficiency and an additional 3%
	 * overhead needs to be accounted for.
	 */
	return div64_u64(mul_u32_u32(intel_dp_link_symbol_clock(crtc_state->port_clock) * 72,
				     drm_dp_bw_channel_coding_efficiency(true)),
			 mul_u32_u32(adjusted_mode->crtc_clock, 1030000));
}

static int intel_dp_mst_bw_overhead(const struct intel_crtc_state *crtc_state,
				    bool ssc, int dsc_slice_count, int bpp_x16)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	unsigned long flags = DRM_DP_BW_OVERHEAD_MST;
	int overhead;

	flags |= intel_dp_is_uhbr(crtc_state) ? DRM_DP_BW_OVERHEAD_UHBR : 0;
	flags |= ssc ? DRM_DP_BW_OVERHEAD_SSC_REF_CLK : 0;
	flags |= crtc_state->fec_enable ? DRM_DP_BW_OVERHEAD_FEC : 0;

	if (dsc_slice_count)
		flags |= DRM_DP_BW_OVERHEAD_DSC;

	overhead = drm_dp_bw_overhead(crtc_state->lane_count,
				      adjusted_mode->hdisplay,
				      dsc_slice_count,
				      bpp_x16,
				      flags);

	/*
	 * TODO: clarify whether a minimum required by the fixed FEC overhead
	 * in the bspec audio programming sequence is required here.
	 */
	return max(overhead, intel_dp_bw_fec_overhead(crtc_state->fec_enable));
}

static void intel_dp_mst_compute_m_n(const struct intel_crtc_state *crtc_state,
				     int overhead,
				     int bpp_x16,
				     struct intel_link_m_n *m_n)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/* TODO: Check WA 14013163432 to set data M/N for full BW utilization. */
	intel_link_compute_m_n(bpp_x16, crtc_state->lane_count,
			       adjusted_mode->crtc_clock,
			       crtc_state->port_clock,
			       overhead,
			       m_n);

	m_n->tu = DIV_ROUND_UP_ULL(mul_u32_u32(m_n->data_m, 64), m_n->data_n);
}

static int intel_dp_mst_calc_pbn(int pixel_clock, int bpp_x16, int bw_overhead)
{
	int effective_data_rate =
		intel_dp_effective_data_rate(pixel_clock, bpp_x16, bw_overhead);

	/*
	 * TODO: Use drm_dp_calc_pbn_mode() instead, once it's converted
	 * to calculate PBN with the BW overhead passed to it.
	 */
	return DIV_ROUND_UP(effective_data_rate * 64, 54 * 1000);
}

static int intel_dp_mst_dsc_get_slice_count(const struct intel_connector *connector,
					    const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int num_joined_pipes = intel_crtc_num_joined_pipes(crtc_state);

	return intel_dp_dsc_get_slice_count(connector,
					    adjusted_mode->clock,
					    adjusted_mode->hdisplay,
					    num_joined_pipes);
}

static void intel_dp_mst_compute_min_hblank(struct intel_crtc_state *crtc_state,
					    int bpp_x16)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *adjusted_mode =
					&crtc_state->hw.adjusted_mode;
	int symbol_size = intel_dp_is_uhbr(crtc_state) ? 32 : 8;
	int hblank;

	if (DISPLAY_VER(display) < 20)
		return;

	/* Calculate min Hblank Link Layer Symbol Cycle Count for 8b/10b MST & 128b/132b */
	hblank = DIV_ROUND_UP((DIV_ROUND_UP
			       (adjusted_mode->htotal - adjusted_mode->hdisplay, 4) * bpp_x16),
			      symbol_size);

	crtc_state->min_hblank = hblank;
}

int intel_dp_mtp_tu_compute_config(struct intel_dp *intel_dp,
				   struct intel_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state,
				   int min_bpp_x16, int max_bpp_x16, int bpp_step_x16, bool dsc)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_atomic_state *state = crtc_state->uapi.state;
	struct drm_dp_mst_topology_state *mst_state = NULL;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	bool is_mst = intel_dp->is_mst;
	int bpp_x16, slots = -EINVAL;
	int dsc_slice_count = 0;
	int max_dpt_bpp_x16;

	/* shouldn't happen, sanity check */
	drm_WARN_ON(display->drm, !dsc && (fxp_q4_to_frac(min_bpp_x16) ||
					   fxp_q4_to_frac(max_bpp_x16) ||
					   fxp_q4_to_frac(bpp_step_x16)));

	if (is_mst) {
		mst_state = drm_atomic_get_mst_topology_state(state, &intel_dp->mst.mgr);
		if (IS_ERR(mst_state))
			return PTR_ERR(mst_state);

		mst_state->pbn_div = drm_dp_get_vc_payload_bw(crtc_state->port_clock,
							      crtc_state->lane_count);
	}

	if (dsc) {
		if (!intel_dp_supports_fec(intel_dp, connector, crtc_state))
			return -EINVAL;

		crtc_state->fec_enable = !intel_dp_is_uhbr(crtc_state);
	}

	max_dpt_bpp_x16 = fxp_q4_from_int(intel_dp_mst_max_dpt_bpp(crtc_state, dsc));
	if (max_dpt_bpp_x16 && max_bpp_x16 > max_dpt_bpp_x16) {
		drm_dbg_kms(display->drm, "Limiting bpp to max DPT bpp (" FXP_Q4_FMT " -> " FXP_Q4_FMT ")\n",
			    FXP_Q4_ARGS(max_bpp_x16), FXP_Q4_ARGS(max_dpt_bpp_x16));
		max_bpp_x16 = max_dpt_bpp_x16;
	}

	drm_dbg_kms(display->drm, "Looking for slots in range min bpp " FXP_Q4_FMT " max bpp " FXP_Q4_FMT "\n",
		    FXP_Q4_ARGS(min_bpp_x16), FXP_Q4_ARGS(max_bpp_x16));

	if (dsc) {
		dsc_slice_count = intel_dp_mst_dsc_get_slice_count(connector, crtc_state);
		if (!dsc_slice_count) {
			drm_dbg_kms(display->drm, "Can't get valid DSC slice count\n");

			return -ENOSPC;
		}
	}

	for (bpp_x16 = max_bpp_x16; bpp_x16 >= min_bpp_x16; bpp_x16 -= bpp_step_x16) {
		int local_bw_overhead;
		int link_bpp_x16;

		drm_dbg_kms(display->drm, "Trying bpp " FXP_Q4_FMT "\n", FXP_Q4_ARGS(bpp_x16));

		link_bpp_x16 = dsc ? bpp_x16 :
			fxp_q4_from_int(intel_dp_output_bpp(crtc_state->output_format,
							    fxp_q4_to_int(bpp_x16)));

		local_bw_overhead = intel_dp_mst_bw_overhead(crtc_state,
							     false, dsc_slice_count, link_bpp_x16);

		intel_dp_mst_compute_min_hblank(crtc_state, link_bpp_x16);

		intel_dp_mst_compute_m_n(crtc_state,
					 local_bw_overhead,
					 link_bpp_x16,
					 &crtc_state->dp_m_n);

		if (is_mst) {
			int remote_bw_overhead;
			int remote_tu;
			fixed20_12 pbn;

			remote_bw_overhead = intel_dp_mst_bw_overhead(crtc_state,
								      true, dsc_slice_count, link_bpp_x16);

			/*
			 * The TU size programmed to the HW determines which slots in
			 * an MTP frame are used for this stream, which needs to match
			 * the payload size programmed to the first downstream branch
			 * device's payload table.
			 *
			 * Note that atm the payload's PBN value DRM core sends via
			 * the ALLOCATE_PAYLOAD side-band message matches the payload
			 * size (which it calculates from the PBN value) it programs
			 * to the first branch device's payload table. The allocation
			 * in the payload table could be reduced though (to
			 * crtc_state->dp_m_n.tu), provided that the driver doesn't
			 * enable SSC on the corresponding link.
			 */
			pbn.full = dfixed_const(intel_dp_mst_calc_pbn(adjusted_mode->crtc_clock,
								      link_bpp_x16,
								      remote_bw_overhead));
			remote_tu = DIV_ROUND_UP(pbn.full, mst_state->pbn_div.full);

			/*
			 * Aligning the TUs ensures that symbols consisting of multiple
			 * (4) symbol cycles don't get split between two consecutive
			 * MTPs, as required by Bspec.
			 * TODO: remove the alignment restriction for 128b/132b links
			 * on some platforms, where Bspec allows this.
			 */
			remote_tu = ALIGN(remote_tu, 4 / crtc_state->lane_count);

			/*
			 * Also align PBNs accordingly, since MST core will derive its
			 * own copy of TU from the PBN in drm_dp_atomic_find_time_slots().
			 * The above comment about the difference between the PBN
			 * allocated for the whole path and the TUs allocated for the
			 * first branch device's link also applies here.
			 */
			pbn.full = remote_tu * mst_state->pbn_div.full;

			drm_WARN_ON(display->drm, remote_tu < crtc_state->dp_m_n.tu);
			crtc_state->dp_m_n.tu = remote_tu;

			slots = drm_dp_atomic_find_time_slots(state, &intel_dp->mst.mgr,
							      connector->mst.port,
							      dfixed_trunc(pbn));
		} else {
			/* Same as above for remote_tu */
			crtc_state->dp_m_n.tu = ALIGN(crtc_state->dp_m_n.tu,
						      4 / crtc_state->lane_count);

			if (crtc_state->dp_m_n.tu <= 64)
				slots = crtc_state->dp_m_n.tu;
			else
				slots = -EINVAL;
		}

		if (slots == -EDEADLK)
			return slots;

		if (slots >= 0) {
			drm_WARN_ON(display->drm, slots != crtc_state->dp_m_n.tu);

			break;
		}

		/* Allow using zero step to indicate one try */
		if (!bpp_step_x16)
			break;
	}

	if (slots < 0) {
		drm_dbg_kms(display->drm, "failed finding vcpi slots:%d\n",
			    slots);
		return slots;
	}

	if (!dsc)
		crtc_state->pipe_bpp = fxp_q4_to_int(bpp_x16);
	else
		crtc_state->dsc.compressed_bpp_x16 = bpp_x16;

	drm_dbg_kms(display->drm, "Got %d slots for pipe bpp " FXP_Q4_FMT " dsc %d\n",
		    slots, FXP_Q4_ARGS(bpp_x16), dsc);

	return 0;
}

static int mst_stream_compute_link_config(struct intel_dp *intel_dp,
					  struct intel_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state,
					  const struct link_config_limits *limits)
{
	crtc_state->lane_count = limits->max_lane_count;
	crtc_state->port_clock = limits->max_rate;

	/*
	 * FIXME: allocate the BW according to link_bpp, which in the case of
	 * YUV420 is only half of the pipe bpp value.
	 */
	return intel_dp_mtp_tu_compute_config(intel_dp, crtc_state, conn_state,
					      limits->link.min_bpp_x16,
					      limits->link.max_bpp_x16,
					      fxp_q4_from_int(2 * 3), false);
}

static int mst_stream_dsc_compute_link_config(struct intel_dp *intel_dp,
					      struct intel_crtc_state *crtc_state,
					      struct drm_connector_state *conn_state,
					      const struct link_config_limits *limits)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	int num_bpc;
	u8 dsc_bpc[3] = {};
	int min_bpp, max_bpp, sink_min_bpp, sink_max_bpp;
	int min_compressed_bpp, max_compressed_bpp;

	max_bpp = limits->pipe.max_bpp;
	min_bpp = limits->pipe.min_bpp;

	num_bpc = drm_dp_dsc_sink_supported_input_bpcs(connector->dp.dsc_dpcd,
						       dsc_bpc);

	drm_dbg_kms(display->drm, "DSC Source supported min bpp %d max bpp %d\n",
		    min_bpp, max_bpp);

	sink_min_bpp = min_array(dsc_bpc, num_bpc) * 3;
	sink_max_bpp = max_array(dsc_bpc, num_bpc) * 3;

	drm_dbg_kms(display->drm, "DSC Sink supported min bpp %d max bpp %d\n",
		    sink_min_bpp, sink_max_bpp);

	if (min_bpp < sink_min_bpp)
		min_bpp = sink_min_bpp;

	if (max_bpp > sink_max_bpp)
		max_bpp = sink_max_bpp;

	crtc_state->pipe_bpp = max_bpp;

	max_compressed_bpp = fxp_q4_to_int(limits->link.max_bpp_x16);
	min_compressed_bpp = fxp_q4_to_int_roundup(limits->link.min_bpp_x16);

	drm_dbg_kms(display->drm, "DSC Sink supported compressed min bpp %d compressed max bpp %d\n",
		    min_compressed_bpp, max_compressed_bpp);

	/* Align compressed bpps according to our own constraints */
	max_compressed_bpp = intel_dp_dsc_nearest_valid_bpp(display, max_compressed_bpp,
							    crtc_state->pipe_bpp);
	min_compressed_bpp = intel_dp_dsc_nearest_valid_bpp(display, min_compressed_bpp,
							    crtc_state->pipe_bpp);

	crtc_state->lane_count = limits->max_lane_count;
	crtc_state->port_clock = limits->max_rate;

	return intel_dp_mtp_tu_compute_config(intel_dp, crtc_state, conn_state,
					      fxp_q4_from_int(min_compressed_bpp),
					      fxp_q4_from_int(max_compressed_bpp),
					      fxp_q4_from_int(1), true);
}

static int mst_stream_update_slots(struct intel_dp *intel_dp,
				   struct intel_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst.mgr;
	struct drm_dp_mst_topology_state *topology_state;
	u8 link_coding_cap = intel_dp_is_uhbr(crtc_state) ?
		DP_CAP_ANSI_128B132B : DP_CAP_ANSI_8B10B;

	topology_state = drm_atomic_get_mst_topology_state(conn_state->state, mgr);
	if (IS_ERR(topology_state)) {
		drm_dbg_kms(display->drm, "slot update failed\n");
		return PTR_ERR(topology_state);
	}

	drm_dp_mst_update_slots(topology_state, link_coding_cap);

	return 0;
}

static int mode_hblank_period_ns(const struct drm_display_mode *mode)
{
	return DIV_ROUND_CLOSEST_ULL(mul_u32_u32(mode->htotal - mode->hdisplay,
						 NSEC_PER_SEC / 1000),
				     mode->crtc_clock);
}

static bool
hblank_expansion_quirk_needs_dsc(const struct intel_connector *connector,
				 const struct intel_crtc_state *crtc_state,
				 const struct link_config_limits *limits)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	bool is_uhbr_sink = connector->mst.dp &&
			    drm_dp_128b132b_supported(connector->mst.dp->dpcd);
	int hblank_limit = is_uhbr_sink ? 500 : 300;

	if (!connector->dp.dsc_hblank_expansion_quirk)
		return false;

	if (is_uhbr_sink && !drm_dp_is_uhbr_rate(limits->max_rate))
		return false;

	if (mode_hblank_period_ns(adjusted_mode) > hblank_limit)
		return false;

	if (!intel_dp_mst_dsc_get_slice_count(connector, crtc_state))
		return false;

	return true;
}

static bool
adjust_limits_for_dsc_hblank_expansion_quirk(struct intel_dp *intel_dp,
					     const struct intel_connector *connector,
					     const struct intel_crtc_state *crtc_state,
					     struct link_config_limits *limits,
					     bool dsc)
{
	struct intel_display *display = to_intel_display(connector);
	const struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int min_bpp_x16 = limits->link.min_bpp_x16;

	if (!hblank_expansion_quirk_needs_dsc(connector, crtc_state, limits))
		return true;

	if (!dsc) {
		if (intel_dp_supports_dsc(intel_dp, connector, crtc_state)) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s][CONNECTOR:%d:%s] DSC needed by hblank expansion quirk\n",
				    crtc->base.base.id, crtc->base.name,
				    connector->base.base.id, connector->base.name);
			return false;
		}

		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s][CONNECTOR:%d:%s] Increasing link min bpp to 24 due to hblank expansion quirk\n",
			    crtc->base.base.id, crtc->base.name,
			    connector->base.base.id, connector->base.name);

		if (limits->link.max_bpp_x16 < fxp_q4_from_int(24))
			return false;

		limits->link.min_bpp_x16 = fxp_q4_from_int(24);

		return true;
	}

	drm_WARN_ON(display->drm, limits->min_rate != limits->max_rate);

	if (limits->max_rate < 540000)
		min_bpp_x16 = fxp_q4_from_int(13);
	else if (limits->max_rate < 810000)
		min_bpp_x16 = fxp_q4_from_int(10);

	if (limits->link.min_bpp_x16 >= min_bpp_x16)
		return true;

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s][CONNECTOR:%d:%s] Increasing link min bpp to " FXP_Q4_FMT " in DSC mode due to hblank expansion quirk\n",
		    crtc->base.base.id, crtc->base.name,
		    connector->base.base.id, connector->base.name,
		    FXP_Q4_ARGS(min_bpp_x16));

	if (limits->link.max_bpp_x16 < min_bpp_x16)
		return false;

	limits->link.min_bpp_x16 = min_bpp_x16;

	return true;
}

static bool
mst_stream_compute_config_limits(struct intel_dp *intel_dp,
				 const struct intel_connector *connector,
				 struct intel_crtc_state *crtc_state,
				 bool dsc,
				 struct link_config_limits *limits)
{
	if (!intel_dp_compute_config_limits(intel_dp, crtc_state, false, dsc,
					    limits))
		return false;

	return adjust_limits_for_dsc_hblank_expansion_quirk(intel_dp,
							    connector,
							    crtc_state,
							    limits,
							    dsc);
}

static int mst_stream_compute_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config,
				     struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_atomic_state *state = to_intel_atomic_state(conn_state->state);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_dp *intel_dp = to_primary_dp(encoder);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct link_config_limits limits;
	bool dsc_needed, joiner_needs_dsc;
	int num_joined_pipes;
	int ret = 0;

	if (pipe_config->fec_enable &&
	    !intel_dp_supports_fec(intel_dp, connector, pipe_config))
		return -EINVAL;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	num_joined_pipes = intel_dp_num_joined_pipes(intel_dp, connector,
						     adjusted_mode->crtc_hdisplay,
						     adjusted_mode->crtc_clock);
	if (num_joined_pipes > 1)
		pipe_config->joiner_pipes = GENMASK(crtc->pipe + num_joined_pipes - 1, crtc->pipe);

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->has_pch_encoder = false;

	joiner_needs_dsc = intel_dp_joiner_needs_dsc(display, num_joined_pipes);

	dsc_needed = joiner_needs_dsc || intel_dp->force_dsc_en ||
		!mst_stream_compute_config_limits(intel_dp, connector,
						  pipe_config, false, &limits);

	if (!dsc_needed) {
		ret = mst_stream_compute_link_config(intel_dp, pipe_config,
						     conn_state, &limits);

		if (ret == -EDEADLK)
			return ret;

		if (ret)
			dsc_needed = true;
	}

	if (dsc_needed && !intel_dp_supports_dsc(intel_dp, connector, pipe_config)) {
		drm_dbg_kms(display->drm, "DSC required but not available\n");
		return -EINVAL;
	}

	/* enable compression if the mode doesn't fit available BW */
	if (dsc_needed) {
		drm_dbg_kms(display->drm, "Try DSC (fallback=%s, joiner=%s, force=%s)\n",
			    str_yes_no(ret), str_yes_no(joiner_needs_dsc),
			    str_yes_no(intel_dp->force_dsc_en));


		if (!mst_stream_compute_config_limits(intel_dp, connector,
						      pipe_config, true,
						      &limits))
			return -EINVAL;

		/*
		 * FIXME: As bpc is hardcoded to 8, as mentioned above,
		 * WARN and ignore the debug flag force_dsc_bpc for now.
		 */
		drm_WARN(display->drm, intel_dp->force_dsc_bpc,
			 "Cannot Force BPC for MST\n");
		/*
		 * Try to get at least some timeslots and then see, if
		 * we can fit there with DSC.
		 */
		drm_dbg_kms(display->drm, "Trying to find VCPI slots in DSC mode\n");

		ret = mst_stream_dsc_compute_link_config(intel_dp, pipe_config,
							 conn_state, &limits);
		if (ret < 0)
			return ret;

		ret = intel_dp_dsc_compute_config(intel_dp, pipe_config,
						  conn_state, &limits,
						  pipe_config->dp_m_n.tu);
	}

	if (ret)
		return ret;

	ret = mst_stream_update_slots(intel_dp, pipe_config, conn_state);
	if (ret)
		return ret;

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (display->platform.geminilake || display->platform.broxton)
		pipe_config->lane_lat_optim_mask =
			bxt_dpio_phy_calc_lane_lat_optim_mask(pipe_config->lane_count);

	intel_dp_audio_compute_config(encoder, pipe_config, conn_state);

	intel_ddi_compute_min_voltage_level(pipe_config);

	intel_psr_compute_config(intel_dp, pipe_config, conn_state);

	return intel_dp_tunnel_atomic_compute_stream_bw(state, intel_dp, connector,
							pipe_config);
}

/*
 * Iterate over all connectors and return a mask of
 * all CPU transcoders streaming over the same DP link.
 */
static unsigned int
intel_dp_mst_transcoder_mask(struct intel_atomic_state *state,
			     struct intel_dp *mst_port)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	u8 transcoders = 0;
	int i;

	if (DISPLAY_VER(display) < 12)
		return 0;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		const struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector->mst.dp != mst_port || !conn_state->base.crtc)
			continue;

		crtc = to_intel_crtc(conn_state->base.crtc);
		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		if (!crtc_state->hw.active)
			continue;

		transcoders |= BIT(crtc_state->cpu_transcoder);
	}

	return transcoders;
}

static u8 get_pipes_downstream_of_mst_port(struct intel_atomic_state *state,
					   struct drm_dp_mst_topology_mgr *mst_mgr,
					   struct drm_dp_mst_port *parent_port)
{
	const struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	u8 mask = 0;
	int i;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		if (!conn_state->base.crtc)
			continue;

		if (&connector->mst.dp->mst.mgr != mst_mgr)
			continue;

		if (connector->mst.port != parent_port &&
		    !drm_dp_mst_port_downstream_of_parent(mst_mgr,
							  connector->mst.port,
							  parent_port))
			continue;

		mask |= BIT(to_intel_crtc(conn_state->base.crtc)->pipe);
	}

	return mask;
}

static int intel_dp_mst_check_fec_change(struct intel_atomic_state *state,
					 struct drm_dp_mst_topology_mgr *mst_mgr,
					 struct intel_link_bw_limits *limits)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;
	u8 mst_pipe_mask;
	u8 fec_pipe_mask = 0;
	int ret;

	mst_pipe_mask = get_pipes_downstream_of_mst_port(state, mst_mgr, NULL);

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, mst_pipe_mask) {
		struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state, crtc);

		/* Atomic connector check should've added all the MST CRTCs. */
		if (drm_WARN_ON(display->drm, !crtc_state))
			return -EINVAL;

		if (crtc_state->fec_enable)
			fec_pipe_mask |= BIT(crtc->pipe);
	}

	if (!fec_pipe_mask || mst_pipe_mask == fec_pipe_mask)
		return 0;

	limits->force_fec_pipes |= mst_pipe_mask;

	ret = intel_modeset_pipes_in_mask_early(state, "MST FEC",
						mst_pipe_mask);

	return ret ? : -EAGAIN;
}

static int intel_dp_mst_check_bw(struct intel_atomic_state *state,
				 struct drm_dp_mst_topology_mgr *mst_mgr,
				 struct drm_dp_mst_topology_state *mst_state,
				 struct intel_link_bw_limits *limits)
{
	struct drm_dp_mst_port *mst_port;
	u8 mst_port_pipes;
	int ret;

	ret = drm_dp_mst_atomic_check_mgr(&state->base, mst_mgr, mst_state, &mst_port);
	if (ret != -ENOSPC)
		return ret;

	mst_port_pipes = get_pipes_downstream_of_mst_port(state, mst_mgr, mst_port);

	ret = intel_link_bw_reduce_bpp(state, limits,
				       mst_port_pipes, "MST link BW");

	return ret ? : -EAGAIN;
}

/**
 * intel_dp_mst_atomic_check_link - check all modeset MST link configuration
 * @state: intel atomic state
 * @limits: link BW limits
 *
 * Check the link configuration for all modeset MST outputs. If the
 * configuration is invalid @limits will be updated if possible to
 * reduce the total BW, after which the configuration for all CRTCs in
 * @state must be recomputed with the updated @limits.
 *
 * Returns:
 *   - 0 if the configuration is valid
 *   - %-EAGAIN, if the configuration is invalid and @limits got updated
 *     with fallback values with which the configuration of all CRTCs in
 *     @state must be recomputed
 *   - Other negative error, if the configuration is invalid without a
 *     fallback possibility, or the check failed for another reason
 */
int intel_dp_mst_atomic_check_link(struct intel_atomic_state *state,
				   struct intel_link_bw_limits *limits)
{
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	int ret;
	int i;

	for_each_new_mst_mgr_in_state(&state->base, mgr, mst_state, i) {
		ret = intel_dp_mst_check_fec_change(state, mgr, limits);
		if (ret)
			return ret;

		ret = intel_dp_mst_check_bw(state, mgr, mst_state,
					    limits);
		if (ret)
			return ret;
	}

	return 0;
}

static int mst_stream_compute_config_late(struct intel_encoder *encoder,
					  struct intel_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(conn_state->state);
	struct intel_dp *intel_dp = to_primary_dp(encoder);

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
 *
 * On TGL+ this is required since each stream go through a master transcoder,
 * so if the master transcoder needs modeset, all other streams in the
 * topology need a modeset. All platforms need to add the atomic state
 * for all streams in the topology, since a modeset on one may require
 * changing the MST link BW usage of the others, which in turn needs a
 * recomputation of the corresponding CRTC states.
 */
static int
mst_connector_atomic_topology_check(struct intel_connector *connector,
				    struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_connector_list_iter connector_list_iter;
	struct intel_connector *connector_iter;
	int ret = 0;

	if (!intel_connector_needs_modeset(state, &connector->base))
		return 0;

	drm_connector_list_iter_begin(display->drm, &connector_list_iter);
	for_each_intel_connector_iter(connector_iter, &connector_list_iter) {
		struct intel_digital_connector_state *conn_iter_state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector_iter->mst.dp != connector->mst.dp ||
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
mst_connector_atomic_check(struct drm_connector *_connector,
			   struct drm_atomic_state *_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	struct intel_connector *connector = to_intel_connector(_connector);
	int ret;

	ret = intel_digital_connector_atomic_check(&connector->base, &state->base);
	if (ret)
		return ret;

	ret = mst_connector_atomic_topology_check(connector, state);
	if (ret)
		return ret;

	if (intel_connector_needs_modeset(state, &connector->base)) {
		ret = intel_dp_tunnel_atomic_check_state(state,
							 connector->mst.dp,
							 connector);
		if (ret)
			return ret;
	}

	return drm_dp_atomic_release_time_slots(&state->base,
						&connector->mst.dp->mst.mgr,
						connector->mst.port);
}

static void mst_stream_disable(struct intel_atomic_state *state,
			       struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	enum transcoder trans = old_crtc_state->cpu_transcoder;

	drm_dbg_kms(display->drm, "active links %d\n",
		    intel_dp->mst.active_links);

	if (intel_dp->mst.active_links == 1)
		intel_dp->link_trained = false;

	intel_hdcp_disable(intel_mst->connector);

	intel_dp_sink_disable_decompression(state, connector, old_crtc_state);

	if (DISPLAY_VER(display) >= 20)
		intel_de_write(display, DP_MIN_HBLANK_CTL(trans), 0);
}

static void mst_stream_post_disable(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	struct drm_dp_mst_topology_state *old_mst_state =
		drm_atomic_get_old_mst_topology_state(&state->base, &intel_dp->mst.mgr);
	struct drm_dp_mst_topology_state *new_mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst.mgr);
	const struct drm_dp_mst_atomic_payload *old_payload =
		drm_atomic_get_mst_payload_state(old_mst_state, connector->mst.port);
	struct drm_dp_mst_atomic_payload *new_payload =
		drm_atomic_get_mst_payload_state(new_mst_state, connector->mst.port);
	struct intel_crtc *pipe_crtc;
	bool last_mst_stream;
	int i;

	intel_dp->mst.active_links--;
	last_mst_stream = intel_dp->mst.active_links == 0;
	drm_WARN_ON(display->drm, DISPLAY_VER(display) >= 12 && last_mst_stream &&
		    !intel_dp_mst_is_master_trans(old_crtc_state));

	for_each_pipe_crtc_modeset_disable(display, pipe_crtc, old_crtc_state, i) {
		const struct intel_crtc_state *old_pipe_crtc_state =
			intel_atomic_get_old_crtc_state(state, pipe_crtc);

		intel_crtc_vblank_off(old_pipe_crtc_state);
	}

	intel_disable_transcoder(old_crtc_state);

	drm_dp_remove_payload_part1(&intel_dp->mst.mgr, new_mst_state, new_payload);

	intel_ddi_clear_act_sent(encoder, old_crtc_state);

	intel_de_rmw(display,
		     TRANS_DDI_FUNC_CTL(display, old_crtc_state->cpu_transcoder),
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC, 0);

	intel_ddi_wait_for_act_sent(encoder, old_crtc_state);
	drm_dp_check_act_status(&intel_dp->mst.mgr);

	drm_dp_remove_payload_part2(&intel_dp->mst.mgr, new_mst_state,
				    old_payload, new_payload);

	intel_ddi_disable_transcoder_func(old_crtc_state);

	for_each_pipe_crtc_modeset_disable(display, pipe_crtc, old_crtc_state, i) {
		const struct intel_crtc_state *old_pipe_crtc_state =
			intel_atomic_get_old_crtc_state(state, pipe_crtc);

		intel_dsc_disable(old_pipe_crtc_state);

		if (DISPLAY_VER(display) >= 9)
			skl_scaler_disable(old_pipe_crtc_state);
		else
			ilk_pfit_disable(old_pipe_crtc_state);
	}

	/*
	 * Power down mst path before disabling the port, otherwise we end
	 * up getting interrupts from the sink upon detecting link loss.
	 */
	drm_dp_send_power_updown_phy(&intel_dp->mst.mgr, connector->mst.port,
				     false);

	/*
	 * BSpec 4287: disable DIP after the transcoder is disabled and before
	 * the transcoder clock select is set to none.
	 */
	intel_dp_set_infoframes(primary_encoder, false, old_crtc_state, NULL);
	/*
	 * From TGL spec: "If multi-stream slave transcoder: Configure
	 * Transcoder Clock Select to direct no clock to the transcoder"
	 *
	 * From older GENs spec: "Configure Transcoder Clock Select to direct
	 * no clock to the transcoder"
	 */
	if (DISPLAY_VER(display) < 12 || !last_mst_stream)
		intel_ddi_disable_transcoder_clock(old_crtc_state);


	intel_mst->connector = NULL;
	if (last_mst_stream)
		primary_encoder->post_disable(state, primary_encoder,
					      old_crtc_state, NULL);

	drm_dbg_kms(display->drm, "active links %d\n",
		    intel_dp->mst.active_links);
}

static void mst_stream_post_pll_disable(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *old_crtc_state,
					const struct drm_connector_state *old_conn_state)
{
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);

	if (intel_dp->mst.active_links == 0 &&
	    primary_encoder->post_pll_disable)
		primary_encoder->post_pll_disable(state, primary_encoder, old_crtc_state, old_conn_state);
}

static void mst_stream_pre_pll_enable(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *pipe_config,
				      const struct drm_connector_state *conn_state)
{
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);

	if (intel_dp->mst.active_links == 0)
		primary_encoder->pre_pll_enable(state, primary_encoder,
						pipe_config, NULL);
	else
		/*
		 * The port PLL state needs to get updated for secondary
		 * streams as for the primary stream.
		 */
		intel_ddi_update_active_dpll(state, primary_encoder,
					     to_intel_crtc(pipe_config->uapi.crtc));
}

static bool intel_mst_probed_link_params_valid(struct intel_dp *intel_dp,
					       int link_rate, int lane_count)
{
	return intel_dp->link.mst_probed_rate == link_rate &&
		intel_dp->link.mst_probed_lane_count == lane_count;
}

static void intel_mst_set_probed_link_params(struct intel_dp *intel_dp,
					     int link_rate, int lane_count)
{
	intel_dp->link.mst_probed_rate = link_rate;
	intel_dp->link.mst_probed_lane_count = lane_count;
}

static void intel_mst_reprobe_topology(struct intel_dp *intel_dp,
				       const struct intel_crtc_state *crtc_state)
{
	if (intel_mst_probed_link_params_valid(intel_dp,
					       crtc_state->port_clock, crtc_state->lane_count))
		return;

	drm_dp_mst_topology_queue_probe(&intel_dp->mst.mgr);

	intel_mst_set_probed_link_params(intel_dp,
					 crtc_state->port_clock, crtc_state->lane_count);
}

static void mst_stream_pre_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst.mgr);
	int ret;
	bool first_mst_stream;

	/* MST encoders are bound to a crtc, not to a connector,
	 * force the mapping here for get_hw_state.
	 */
	connector->encoder = encoder;
	intel_mst->connector = connector;
	first_mst_stream = intel_dp->mst.active_links == 0;
	drm_WARN_ON(display->drm, DISPLAY_VER(display) >= 12 && first_mst_stream &&
		    !intel_dp_mst_is_master_trans(pipe_config));

	drm_dbg_kms(display->drm, "active links %d\n",
		    intel_dp->mst.active_links);

	if (first_mst_stream)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);

	drm_dp_send_power_updown_phy(&intel_dp->mst.mgr, connector->mst.port, true);

	intel_dp_sink_enable_decompression(state, connector, pipe_config);

	if (first_mst_stream) {
		primary_encoder->pre_enable(state, primary_encoder,
					    pipe_config, NULL);

		intel_mst_reprobe_topology(intel_dp, pipe_config);
	}

	intel_dp->mst.active_links++;

	ret = drm_dp_add_payload_part1(&intel_dp->mst.mgr, mst_state,
				       drm_atomic_get_mst_payload_state(mst_state, connector->mst.port));
	if (ret < 0)
		intel_dp_queue_modeset_retry_for_link(state, primary_encoder, pipe_config);

	/*
	 * Before Gen 12 this is not done as part of
	 * primary_encoder->pre_enable() and should be done here. For
	 * Gen 12+ the step in which this should be done is different for the
	 * first MST stream, so it's done on the DDI for the first stream and
	 * here for the following ones.
	 */
	if (DISPLAY_VER(display) < 12 || !first_mst_stream)
		intel_ddi_enable_transcoder_clock(encoder, pipe_config);

	if (DISPLAY_VER(display) >= 13 && !first_mst_stream)
		intel_ddi_config_transcoder_func(encoder, pipe_config);

	intel_dsc_dp_pps_write(primary_encoder, pipe_config);
	intel_ddi_set_dp_msa(pipe_config, conn_state);
}

static void enable_bs_jitter_was(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	u32 clear = 0;
	u32 set = 0;

	if (!display->platform.alderlake_p)
		return;

	if (!IS_DISPLAY_STEP(display, STEP_D0, STEP_FOREVER))
		return;

	/* Wa_14013163432:adlp */
	if (crtc_state->fec_enable || intel_dp_is_uhbr(crtc_state))
		set |= DP_MST_FEC_BS_JITTER_WA(crtc_state->cpu_transcoder);

	/* Wa_14014143976:adlp */
	if (IS_DISPLAY_STEP(display, STEP_E0, STEP_FOREVER)) {
		if (intel_dp_is_uhbr(crtc_state))
			set |= DP_MST_SHORT_HBLANK_WA(crtc_state->cpu_transcoder);
		else if (crtc_state->fec_enable)
			clear |= DP_MST_SHORT_HBLANK_WA(crtc_state->cpu_transcoder);

		if (crtc_state->fec_enable || intel_dp_is_uhbr(crtc_state))
			set |= DP_MST_DPT_DPTP_ALIGN_WA(crtc_state->cpu_transcoder);
	}

	if (!clear && !set)
		return;

	intel_de_rmw(display, CHICKEN_MISC_3, clear, set);
}

static void mst_stream_enable(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);
	struct intel_dp *intel_dp = to_primary_dp(encoder);
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst.mgr);
	enum transcoder trans = pipe_config->cpu_transcoder;
	bool first_mst_stream = intel_dp->mst.active_links == 1;
	struct intel_crtc *pipe_crtc;
	int ret, i, min_hblank;

	drm_WARN_ON(display->drm, pipe_config->has_pch_encoder);

	if (intel_dp_is_uhbr(pipe_config)) {
		const struct drm_display_mode *adjusted_mode =
			&pipe_config->hw.adjusted_mode;
		u64 crtc_clock_hz = KHz(adjusted_mode->crtc_clock);

		intel_de_write(display, TRANS_DP2_VFREQHIGH(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz >> 24));
		intel_de_write(display, TRANS_DP2_VFREQLOW(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz & 0xffffff));
	}

	if (DISPLAY_VER(display) >= 20) {
		/*
		 * adjust the BlankingStart/BlankingEnd framing control from
		 * the calculated value
		 */
		min_hblank = pipe_config->min_hblank - 2;

		/* Maximum value to be programmed is limited to 0x10 */
		min_hblank = min(0x10, min_hblank);

		/*
		 * Minimum hblank accepted for 128b/132b would be 5 and for
		 * 8b/10b would be 3 symbol count
		 */
		if (intel_dp_is_uhbr(pipe_config))
			min_hblank = max(min_hblank, 5);
		else
			min_hblank = max(min_hblank, 3);

		intel_de_write(display, DP_MIN_HBLANK_CTL(trans),
			       min_hblank);
	}

	enable_bs_jitter_was(pipe_config);

	intel_ddi_enable_transcoder_func(encoder, pipe_config);

	intel_ddi_clear_act_sent(encoder, pipe_config);

	intel_de_rmw(display, TRANS_DDI_FUNC_CTL(display, trans), 0,
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC);

	drm_dbg_kms(display->drm, "active links %d\n",
		    intel_dp->mst.active_links);

	intel_ddi_wait_for_act_sent(encoder, pipe_config);
	drm_dp_check_act_status(&intel_dp->mst.mgr);

	if (first_mst_stream)
		intel_ddi_wait_for_fec_status(encoder, pipe_config, true);

	ret = drm_dp_add_payload_part2(&intel_dp->mst.mgr,
				       drm_atomic_get_mst_payload_state(mst_state,
									connector->mst.port));
	if (ret < 0)
		intel_dp_queue_modeset_retry_for_link(state, primary_encoder, pipe_config);

	if (DISPLAY_VER(display) >= 12)
		intel_de_rmw(display, CHICKEN_TRANS(display, trans),
			     FECSTALL_DIS_DPTSTREAM_DPTTG,
			     pipe_config->fec_enable ? FECSTALL_DIS_DPTSTREAM_DPTTG : 0);

	intel_audio_sdp_split_update(pipe_config);

	intel_enable_transcoder(pipe_config);

	for_each_pipe_crtc_modeset_enable(display, pipe_crtc, pipe_config, i) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		intel_crtc_vblank_on(pipe_crtc_state);
	}

	intel_hdcp_enable(state, encoder, pipe_config, conn_state);
}

static bool mst_stream_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	*pipe = intel_mst->pipe;
	if (intel_mst->connector)
		return true;
	return false;
}

static void mst_stream_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);

	primary_encoder->get_config(primary_encoder, pipe_config);
}

static bool mst_stream_initial_fastset_check(struct intel_encoder *encoder,
					     struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *primary_encoder = to_primary_encoder(encoder);

	return intel_dp_initial_fastset_check(primary_encoder, crtc_state);
}

static int mst_connector_get_ddc_modes(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = connector->mst.dp;
	const struct drm_edid *drm_edid;
	int ret;

	if (drm_connector_is_unregistered(&connector->base))
		return intel_connector_update_modes(&connector->base, NULL);

	if (!intel_display_driver_check_access(display))
		return drm_edid_connector_add_modes(&connector->base);

	drm_edid = drm_dp_mst_edid_read(&connector->base, &intel_dp->mst.mgr, connector->mst.port);

	ret = intel_connector_update_modes(&connector->base, drm_edid);

	drm_edid_free(drm_edid);

	return ret;
}

static int
mst_connector_late_register(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	int ret;

	ret = drm_dp_mst_connector_late_register(&connector->base, connector->mst.port);
	if (ret < 0)
		return ret;

	ret = intel_connector_register(&connector->base);
	if (ret < 0)
		drm_dp_mst_connector_early_unregister(&connector->base, connector->mst.port);

	return ret;
}

static void
mst_connector_early_unregister(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);

	intel_connector_unregister(&connector->base);
	drm_dp_mst_connector_early_unregister(&connector->base, connector->mst.port);
}

static const struct drm_connector_funcs mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = mst_connector_late_register,
	.early_unregister = mst_connector_early_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static int mst_connector_get_modes(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);

	return mst_connector_get_ddc_modes(&connector->base);
}

static int
mst_connector_mode_valid_ctx(struct drm_connector *_connector,
			     const struct drm_display_mode *mode,
			     struct drm_modeset_acquire_ctx *ctx,
			     enum drm_mode_status *status)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = connector->mst.dp;
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst.mgr;
	struct drm_dp_mst_port *port = connector->mst.port;
	const int min_bpp = 18;
	int max_dotclk = display->cdclk.max_dotclk_freq;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int ret;
	bool dsc = false;
	u16 dsc_max_compressed_bpp = 0;
	u8 dsc_slice_count = 0;
	int target_clock = mode->clock;
	int num_joined_pipes;

	if (drm_connector_is_unregistered(&connector->base)) {
		*status = MODE_ERROR;
		return 0;
	}

	*status = intel_cpu_transcoder_mode_valid(display, mode);
	if (*status != MODE_OK)
		return 0;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		*status = MODE_H_ILLEGAL;
		return 0;
	}

	if (mode->clock < 10000) {
		*status = MODE_CLOCK_LOW;
		return 0;
	}

	max_link_clock = intel_dp_max_link_rate(intel_dp);
	max_lanes = intel_dp_max_lane_count(intel_dp);

	max_rate = intel_dp_max_link_data_rate(intel_dp,
					       max_link_clock, max_lanes);
	mode_rate = intel_dp_link_required(mode->clock, min_bpp);

	/*
	 * TODO:
	 * - Also check if compression would allow for the mode
	 * - Calculate the overhead using drm_dp_bw_overhead() /
	 *   drm_dp_bw_channel_coding_efficiency(), similarly to the
	 *   compute config code, as drm_dp_calc_pbn_mode() doesn't
	 *   account with all the overheads.
	 * - Check here and during compute config the BW reported by
	 *   DFP_Link_Available_Payload_Bandwidth_Number (or the
	 *   corresponding link capabilities of the sink) in case the
	 *   stream is uncompressed for it by the last branch device.
	 */
	num_joined_pipes = intel_dp_num_joined_pipes(intel_dp, connector,
						     mode->hdisplay, target_clock);
	max_dotclk *= num_joined_pipes;

	ret = drm_modeset_lock(&mgr->base.lock, ctx);
	if (ret)
		return ret;

	if (mode_rate > max_rate || mode->clock > max_dotclk ||
	    drm_dp_calc_pbn_mode(mode->clock, min_bpp << 4) > port->full_pbn) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (intel_dp_has_dsc(connector)) {
		/*
		 * TBD pass the connector BPC,
		 * for now U8_MAX so that max BPC on that platform would be picked
		 */
		int pipe_bpp = intel_dp_dsc_compute_max_bpp(connector, U8_MAX);

		if (drm_dp_sink_supports_fec(connector->dp.fec_capability)) {
			dsc_max_compressed_bpp =
				intel_dp_dsc_get_max_compressed_bpp(display,
								    max_link_clock,
								    max_lanes,
								    target_clock,
								    mode->hdisplay,
								    num_joined_pipes,
								    INTEL_OUTPUT_FORMAT_RGB,
								    pipe_bpp, 64);
			dsc_slice_count =
				intel_dp_dsc_get_slice_count(connector,
							     target_clock,
							     mode->hdisplay,
							     num_joined_pipes);
		}

		dsc = dsc_max_compressed_bpp && dsc_slice_count;
	}

	if (intel_dp_joiner_needs_dsc(display, num_joined_pipes) && !dsc) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (mode_rate > max_rate && !dsc) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	*status = intel_mode_valid_max_plane_size(display, mode, num_joined_pipes);
	return 0;
}

static struct drm_encoder *
mst_connector_atomic_best_encoder(struct drm_connector *_connector,
				  struct drm_atomic_state *state)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct drm_connector_state *connector_state =
		drm_atomic_get_new_connector_state(state, &connector->base);
	struct intel_dp *intel_dp = connector->mst.dp;
	struct intel_crtc *crtc = to_intel_crtc(connector_state->crtc);

	return &intel_dp->mst.stream_encoders[crtc->pipe]->base.base;
}

static int
mst_connector_detect_ctx(struct drm_connector *_connector,
			 struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = connector->mst.dp;

	if (!intel_display_device_enabled(display))
		return connector_status_disconnected;

	if (drm_connector_is_unregistered(&connector->base))
		return connector_status_disconnected;

	if (!intel_display_driver_check_access(display))
		return connector->base.status;

	intel_dp_flush_connector_commits(connector);

	return drm_dp_mst_detect_port(&connector->base, ctx, &intel_dp->mst.mgr,
				      connector->mst.port);
}

static const struct drm_connector_helper_funcs mst_connector_helper_funcs = {
	.get_modes = mst_connector_get_modes,
	.mode_valid_ctx = mst_connector_mode_valid_ctx,
	.atomic_best_encoder = mst_connector_atomic_best_encoder,
	.atomic_check = mst_connector_atomic_check,
	.detect_ctx = mst_connector_detect_ctx,
};

static void mst_stream_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(to_intel_encoder(encoder));

	drm_encoder_cleanup(encoder);
	kfree(intel_mst);
}

static const struct drm_encoder_funcs mst_stream_encoder_funcs = {
	.destroy = mst_stream_encoder_destroy,
};

static bool mst_connector_get_hw_state(struct intel_connector *connector)
{
	/* This is the MST stream encoder set in ->pre_enable, if any */
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	enum pipe pipe;

	if (!encoder || !connector->base.state->crtc)
		return false;

	return encoder->get_hw_state(encoder, &pipe);
}

static int mst_topology_add_connector_properties(struct intel_dp *intel_dp,
						 struct drm_connector *_connector,
						 const char *pathprop)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_connector *connector = to_intel_connector(_connector);

	drm_object_attach_property(&connector->base.base,
				   display->drm->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base.base,
				   display->drm->mode_config.tile_property, 0);

	intel_attach_force_audio_property(&connector->base);
	intel_attach_broadcast_rgb_property(&connector->base);

	/*
	 * Reuse the prop from the SST connector because we're
	 * not allowed to create new props after device registration.
	 */
	connector->base.max_bpc_property =
		intel_dp->attached_connector->base.max_bpc_property;
	if (connector->base.max_bpc_property)
		drm_connector_attach_max_bpc_property(&connector->base, 6, 12);

	return drm_connector_set_path_property(&connector->base, pathprop);
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

static bool detect_dsc_hblank_expansion_quirk(const struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_dp_aux *aux = connector->dp.dsc_decompression_aux;
	struct drm_dp_desc desc;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	if (!aux)
		return false;

	/*
	 * A logical port's OUI (at least for affected sinks) is all 0, so
	 * instead of that the parent port's OUI is used for identification.
	 */
	if (drm_dp_mst_port_is_logical(connector->mst.port)) {
		aux = drm_dp_mst_aux_for_parent(connector->mst.port);
		if (!aux)
			aux = &connector->mst.dp->aux;
	}

	if (drm_dp_read_dpcd_caps(aux, dpcd) < 0)
		return false;

	if (drm_dp_read_desc(aux, &desc, drm_dp_is_branch(dpcd)) < 0)
		return false;

	if (!drm_dp_has_quirk(&desc,
			      DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC))
		return false;

	/*
	 * UHBR (MST sink) devices requiring this quirk don't advertise the
	 * HBLANK expansion support. Presuming that they perform HBLANK
	 * expansion internally, or are affected by this issue on modes with a
	 * short HBLANK for other reasons.
	 */
	if (!drm_dp_128b132b_supported(dpcd) &&
	    !(dpcd[DP_RECEIVE_PORT_0_CAP_0] & DP_HBLANK_EXPANSION_CAPABLE))
		return false;

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] DSC HBLANK expansion quirk detected\n",
		    connector->base.base.id, connector->base.name);

	return true;
}

static struct drm_connector *
mst_topology_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			   struct drm_dp_mst_port *port,
			   const char *pathprop)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst.mgr);
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_connector *connector;
	enum pipe pipe;
	int ret;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	connector->get_hw_state = mst_connector_get_hw_state;
	connector->sync_state = intel_dp_connector_sync_state;
	connector->mst.dp = intel_dp;
	connector->mst.port = port;
	drm_dp_mst_get_port_malloc(port);

	ret = drm_connector_dynamic_init(display->drm, &connector->base, &mst_connector_funcs,
					 DRM_MODE_CONNECTOR_DisplayPort, NULL);
	if (ret)
		goto err_put_port;

	connector->dp.dsc_decompression_aux = drm_dp_mst_dsc_aux_for_port(port);
	intel_dp_mst_read_decompression_port_dsc_caps(intel_dp, connector);
	connector->dp.dsc_hblank_expansion_quirk =
		detect_dsc_hblank_expansion_quirk(connector);

	drm_connector_helper_add(&connector->base, &mst_connector_helper_funcs);

	for_each_pipe(display, pipe) {
		struct drm_encoder *enc =
			&intel_dp->mst.stream_encoders[pipe]->base.base;

		ret = drm_connector_attach_encoder(&connector->base, enc);
		if (ret)
			goto err_cleanup_connector;
	}

	ret = mst_topology_add_connector_properties(intel_dp, &connector->base, pathprop);
	if (ret)
		goto err_cleanup_connector;

	ret = intel_dp_hdcp_init(dig_port, connector);
	if (ret)
		drm_dbg_kms(display->drm, "[%s:%d] HDCP MST init failed, skipping.\n",
			    connector->base.name, connector->base.base.id);

	return &connector->base;

err_cleanup_connector:
	drm_connector_cleanup(&connector->base);
err_put_port:
	drm_dp_mst_put_port_malloc(port);
	intel_connector_free(connector);

	return NULL;
}

static void
mst_topology_poll_hpd_irq(struct drm_dp_mst_topology_mgr *mgr)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst.mgr);

	intel_hpd_trigger_irq(dp_to_dig_port(intel_dp));
}

static const struct drm_dp_mst_topology_cbs mst_topology_cbs = {
	.add_connector = mst_topology_add_connector,
	.poll_hpd_irq = mst_topology_poll_hpd_irq,
};

/* Create a fake encoder for an individual MST stream */
static struct intel_dp_mst_encoder *
mst_stream_encoder_create(struct intel_digital_port *dig_port, enum pipe pipe)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_encoder *primary_encoder = &dig_port->base;
	struct intel_dp_mst_encoder *intel_mst;
	struct intel_encoder *encoder;

	intel_mst = kzalloc(sizeof(*intel_mst), GFP_KERNEL);

	if (!intel_mst)
		return NULL;

	intel_mst->pipe = pipe;
	encoder = &intel_mst->base;
	intel_mst->primary = dig_port;

	drm_encoder_init(display->drm, &encoder->base, &mst_stream_encoder_funcs,
			 DRM_MODE_ENCODER_DPMST, "DP-MST %c", pipe_name(pipe));

	encoder->type = INTEL_OUTPUT_DP_MST;
	encoder->power_domain = primary_encoder->power_domain;
	encoder->port = primary_encoder->port;
	encoder->cloneable = 0;
	/*
	 * This is wrong, but broken userspace uses the intersection
	 * of possible_crtcs of all the encoders of a given connector
	 * to figure out which crtcs can drive said connector. What
	 * should be used instead is the union of possible_crtcs.
	 * To keep such userspace functioning we must misconfigure
	 * this to make sure the intersection is not empty :(
	 */
	encoder->pipe_mask = ~0;

	encoder->compute_config = mst_stream_compute_config;
	encoder->compute_config_late = mst_stream_compute_config_late;
	encoder->disable = mst_stream_disable;
	encoder->post_disable = mst_stream_post_disable;
	encoder->post_pll_disable = mst_stream_post_pll_disable;
	encoder->update_pipe = intel_ddi_update_pipe;
	encoder->pre_pll_enable = mst_stream_pre_pll_enable;
	encoder->pre_enable = mst_stream_pre_enable;
	encoder->enable = mst_stream_enable;
	encoder->audio_enable = intel_audio_codec_enable;
	encoder->audio_disable = intel_audio_codec_disable;
	encoder->get_hw_state = mst_stream_get_hw_state;
	encoder->get_config = mst_stream_get_config;
	encoder->initial_fastset_check = mst_stream_initial_fastset_check;

	return intel_mst;

}

/* Create the fake encoders for MST streams */
static bool
mst_stream_encoders_create(struct intel_digital_port *dig_port)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum pipe pipe;

	for_each_pipe(display, pipe)
		intel_dp->mst.stream_encoders[pipe] = mst_stream_encoder_create(dig_port, pipe);
	return true;
}

int
intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port)
{
	return dig_port->dp.mst.active_links;
}

int
intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_base_id)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum port port = dig_port->base.port;
	int ret;

	if (!HAS_DP_MST(display) || intel_dp_is_edp(intel_dp))
		return 0;

	if (DISPLAY_VER(display) < 12 && port == PORT_A)
		return 0;

	if (DISPLAY_VER(display) < 11 && port == PORT_E)
		return 0;

	intel_dp->mst.mgr.cbs = &mst_topology_cbs;

	/* create encoders */
	mst_stream_encoders_create(dig_port);
	ret = drm_dp_mst_topology_mgr_init(&intel_dp->mst.mgr, display->drm,
					   &intel_dp->aux, 16,
					   INTEL_NUM_PIPES(display), conn_base_id);
	if (ret) {
		intel_dp->mst.mgr.cbs = NULL;
		return ret;
	}

	return 0;
}

bool intel_dp_mst_source_support(struct intel_dp *intel_dp)
{
	return intel_dp->mst.mgr.cbs;
}

void
intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port)
{
	struct intel_dp *intel_dp = &dig_port->dp;

	if (!intel_dp_mst_source_support(intel_dp))
		return;

	drm_dp_mst_topology_mgr_destroy(&intel_dp->mst.mgr);
	/* encoders will get killed by normal cleanup */

	intel_dp->mst.mgr.cbs = NULL;
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

	if (!connector->mst.dp)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(&state->base,
						      &connector->mst.dp->mst.mgr);
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

static struct intel_connector *
get_connector_in_state_for_crtc(struct intel_atomic_state *state,
				const struct intel_crtc *crtc)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *_connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, _connector,
					   old_conn_state, new_conn_state, i) {
		struct intel_connector *connector =
			to_intel_connector(_connector);

		if (old_conn_state->crtc == &crtc->base ||
		    new_conn_state->crtc == &crtc->base)
			return connector;
	}

	return NULL;
}

/**
 * intel_dp_mst_crtc_needs_modeset - check if changes in topology need to modeset the given CRTC
 * @state: atomic state
 * @crtc: CRTC for which to check the modeset requirement
 *
 * Check if any change in a MST topology requires a forced modeset on @crtc in
 * this topology. One such change is enabling/disabling the DSC decompression
 * state in the first branch device's UFP DPCD as required by one CRTC, while
 * the other @crtc in the same topology is still active, requiring a full modeset
 * on @crtc.
 */
bool intel_dp_mst_crtc_needs_modeset(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	const struct intel_connector *crtc_connector;
	const struct drm_connector_state *conn_state;
	const struct drm_connector *_connector;
	int i;

	if (!intel_crtc_has_type(intel_atomic_get_new_crtc_state(state, crtc),
				 INTEL_OUTPUT_DP_MST))
		return false;

	crtc_connector = get_connector_in_state_for_crtc(state, crtc);

	if (!crtc_connector)
		/* None of the connectors in the topology needs modeset */
		return false;

	for_each_new_connector_in_state(&state->base, _connector, conn_state, i) {
		const struct intel_connector *connector =
			to_intel_connector(_connector);
		const struct intel_crtc_state *new_crtc_state;
		const struct intel_crtc_state *old_crtc_state;
		struct intel_crtc *crtc_iter;

		if (connector->mst.dp != crtc_connector->mst.dp ||
		    !conn_state->crtc)
			continue;

		crtc_iter = to_intel_crtc(conn_state->crtc);

		new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc_iter);
		old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc_iter);

		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		if (old_crtc_state->dsc.compression_enable ==
		    new_crtc_state->dsc.compression_enable)
			continue;
		/*
		 * Toggling the decompression flag because of this stream in
		 * the first downstream branch device's UFP DPCD may reset the
		 * whole branch device. To avoid the reset while other streams
		 * are also active modeset the whole MST topology in this
		 * case.
		 */
		if (connector->dp.dsc_decompression_aux ==
		    &connector->mst.dp->aux)
			return true;
	}

	return false;
}

/**
 * intel_dp_mst_prepare_probe - Prepare an MST link for topology probing
 * @intel_dp: DP port object
 *
 * Prepare an MST link for topology probing, programming the target
 * link parameters to DPCD. This step is a requirement of the enumeration
 * of path resources during probing.
 */
void intel_dp_mst_prepare_probe(struct intel_dp *intel_dp)
{
	int link_rate = intel_dp_max_link_rate(intel_dp);
	int lane_count = intel_dp_max_lane_count(intel_dp);
	u8 rate_select;
	u8 link_bw;

	if (intel_dp->link_trained)
		return;

	if (intel_mst_probed_link_params_valid(intel_dp, link_rate, lane_count))
		return;

	intel_dp_compute_rate(intel_dp, link_rate, &link_bw, &rate_select);

	intel_dp_link_training_set_mode(intel_dp, link_rate, false);
	intel_dp_link_training_set_bw(intel_dp, link_bw, rate_select, lane_count,
				      drm_dp_enhanced_frame_cap(intel_dp->dpcd));

	intel_mst_set_probed_link_params(intel_dp, link_rate, lane_count);
}

/*
 * intel_dp_mst_verify_dpcd_state - verify the MST SW enabled state wrt. the DPCD
 * @intel_dp: DP port object
 *
 * Verify if @intel_dp's MST enabled SW state matches the corresponding DPCD
 * state. A long HPD pulse - not long enough to be detected as a disconnected
 * state - could've reset the DPCD state, which requires tearing
 * down/recreating the MST topology.
 *
 * Returns %true if the SW MST enabled and DPCD states match, %false
 * otherwise.
 */
bool intel_dp_mst_verify_dpcd_state(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	int ret;
	u8 val;

	if (!intel_dp->is_mst)
		return true;

	ret = drm_dp_dpcd_readb(intel_dp->mst.mgr.aux, DP_MSTM_CTRL, &val);

	/* Adjust the expected register value for SST + SideBand. */
	if (ret < 0 || val != (DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC)) {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s][ENCODER:%d:%s] MST mode got reset, removing topology (ret=%d, ctrl=0x%02x)\n",
			    connector->base.base.id, connector->base.name,
			    encoder->base.base.id, encoder->base.name,
			    ret, val);

		return false;
	}

	return true;
}
