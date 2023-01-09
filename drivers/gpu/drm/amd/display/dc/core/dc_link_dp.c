/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 */
#include "dm_services.h"
#include "dc.h"
#include "dc_link_dp.h"
#include "dm_helpers.h"
#include "opp.h"
#include "dsc.h"
#include "resource.h"

#include "inc/core_types.h"
#include "link_hwss.h"
#include "link/protocols/link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"

#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "link/protocols/link_dp_dpia.h"
#include "inc/link_enc_cfg.h"
#include "clk_mgr.h"
#include "link/accessories/link_dp_trace.h"
#include "link/protocols/link_dp_training.h"
#include "link/protocols/link_dp_training_fixed_vs_pe_retimer.h"
#include "link/protocols/link_dp_training_dpia.h"
#include "link/protocols/link_dp_training_auxless.h"
#include "link/protocols/link_dp_phy.h"
#include "link/protocols/link_dp_capability.h"
#define DC_LOGGER \
	link->ctx->logger

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */
#include "link/protocols/link_dpcd.h"

bool dp_validate_mode_timing(
	struct dc_link *link,
	const struct dc_crtc_timing *timing)
{
	uint32_t req_bw;
	uint32_t max_bw;

	const struct dc_link_settings *link_setting;

	/* According to spec, VSC SDP should be used if pixel format is YCbCr420 */
	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 &&
			!link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED &&
			dal_graphics_object_id_get_connector_id(link->link_id) != CONNECTOR_ID_VIRTUAL)
		return false;

	/*always DP fail safe mode*/
	if ((timing->pix_clk_100hz / 10) == (uint32_t) 25175 &&
		timing->h_addressable == (uint32_t) 640 &&
		timing->v_addressable == (uint32_t) 480)
		return true;

	link_setting = dc_link_get_link_cap(link);

	/* TODO: DYNAMIC_VALIDATION needs to be implemented */
	/*if (flags.DYNAMIC_VALIDATION == 1 &&
		link->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN)
		link_setting = &link->verified_link_cap;
	*/

	req_bw = dc_bandwidth_in_kbps_from_timing(timing);
	max_bw = dc_link_bandwidth_kbps(link, link_setting);

	if (req_bw <= max_bw) {
		/* remember the biggest mode here, during
		 * initial link training (to get
		 * verified_link_cap), LS sends event about
		 * cannot train at reported cap to upper
		 * layer and upper layer will re-enumerate modes.
		 * this is not necessary if the lower
		 * verified_link_cap is enough to drive
		 * all the modes */

		/* TODO: DYNAMIC_VALIDATION needs to be implemented */
		/* if (flags.DYNAMIC_VALIDATION == 1)
			dpsst->max_req_bw_for_verified_linkcap = dal_max(
				dpsst->max_req_bw_for_verified_linkcap, req_bw); */
		return true;
	} else
		return false;
}

void dp_enable_mst_on_sink(struct dc_link *link, bool enable)
{
	unsigned char mstmCntl;

	core_link_read_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
	if (enable)
		mstmCntl |= DP_MST_EN;
	else
		mstmCntl &= (~DP_MST_EN);

	core_link_write_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
}

enum dc_status dp_set_fec_ready(struct dc_link *link, const struct link_resource *link_res, bool ready)
{
	/* FEC has to be "set ready" before the link training.
	 * The policy is to always train with FEC
	 * if the sink supports it and leave it enabled on link.
	 * If FEC is not supported, disable it.
	 */
	struct link_encoder *link_enc = NULL;
	enum dc_status status = DC_OK;
	uint8_t fec_config = 0;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (!dc_link_should_enable_fec(link))
		return status;

	if (link_enc->funcs->fec_set_ready &&
			link->dpcd_caps.fec_cap.bits.FEC_CAPABLE) {
		if (ready) {
			fec_config = 1;
			status = core_link_write_dpcd(link,
					DP_FEC_CONFIGURATION,
					&fec_config,
					sizeof(fec_config));
			if (status == DC_OK) {
				link_enc->funcs->fec_set_ready(link_enc, true);
				link->fec_state = dc_link_fec_ready;
			} else {
				link_enc->funcs->fec_set_ready(link_enc, false);
				link->fec_state = dc_link_fec_not_ready;
				dm_error("dpcd write failed to set fec_ready");
			}
		} else if (link->fec_state == dc_link_fec_ready) {
			fec_config = 0;
			status = core_link_write_dpcd(link,
					DP_FEC_CONFIGURATION,
					&fec_config,
					sizeof(fec_config));
			link_enc->funcs->fec_set_ready(link_enc, false);
			link->fec_state = dc_link_fec_not_ready;
		}
	}

	return status;
}

void dp_set_fec_enable(struct dc_link *link, bool enable)
{
	struct link_encoder *link_enc = NULL;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (!dc_link_should_enable_fec(link))
		return;

	if (link_enc->funcs->fec_set_enable &&
			link->dpcd_caps.fec_cap.bits.FEC_CAPABLE) {
		if (link->fec_state == dc_link_fec_ready && enable) {
			/* Accord to DP spec, FEC enable sequence can first
			 * be transmitted anytime after 1000 LL codes have
			 * been transmitted on the link after link training
			 * completion. Using 1 lane RBR should have the maximum
			 * time for transmitting 1000 LL codes which is 6.173 us.
			 * So use 7 microseconds delay instead.
			 */
			udelay(7);
			link_enc->funcs->fec_set_enable(link_enc, true);
			link->fec_state = dc_link_fec_enabled;
		} else if (link->fec_state == dc_link_fec_enabled && !enable) {
			link_enc->funcs->fec_set_enable(link_enc, false);
			link->fec_state = dc_link_fec_ready;
		}
	}
}

// TODO - DP2.0 Link: Fix get_lane_status to handle LTTPR offset (SST and MST)
static void get_lane_status(
	struct dc_link *link,
	uint32_t lane_count,
	union lane_status *status,
	union lane_align_status_updated *status_updated)
{
	unsigned int lane;
	uint8_t dpcd_buf[3] = {0};

	if (status == NULL || status_updated == NULL) {
		return;
	}

	core_link_read_dpcd(
			link,
			DP_LANE0_1_STATUS,
			dpcd_buf,
			sizeof(dpcd_buf));

	for (lane = 0; lane < lane_count; lane++) {
		status[lane].raw = dp_get_nibble_at_index(&dpcd_buf[0], lane);
	}

	status_updated->raw = dpcd_buf[2];
}

bool dpcd_write_128b_132b_sst_payload_allocation_table(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		struct link_mst_stream_allocation_table *proposed_table,
		bool allocate)
{
	const uint8_t vc_id = 1; /// VC ID always 1 for SST
	const uint8_t start_time_slot = 0; /// Always start at time slot 0 for SST
	bool result = false;
	uint8_t req_slot_count = 0;
	struct fixed31_32 avg_time_slots_per_mtp = { 0 };
	union payload_table_update_status update_status = { 0 };
	const uint32_t max_retries = 30;
	uint32_t retries = 0;

	if (allocate)	{
		avg_time_slots_per_mtp = calculate_sst_avg_time_slots_per_mtp(stream, link);
		req_slot_count = dc_fixpt_ceil(avg_time_slots_per_mtp);
		/// Validation should filter out modes that exceed link BW
		ASSERT(req_slot_count <= MAX_MTP_SLOT_COUNT);
		if (req_slot_count > MAX_MTP_SLOT_COUNT)
			return false;
	} else {
		/// Leave req_slot_count = 0 if allocate is false.
	}

	proposed_table->stream_count = 1; /// Always 1 stream for SST
	proposed_table->stream_allocations[0].slot_count = req_slot_count;
	proposed_table->stream_allocations[0].vcp_id = vc_id;

	if (link->aux_access_disabled)
		return true;

	/// Write DPCD 2C0 = 1 to start updating
	update_status.bits.VC_PAYLOAD_TABLE_UPDATED = 1;
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_TABLE_UPDATE_STATUS,
			&update_status.raw,
			1);

	/// Program the changes in DPCD 1C0 - 1C2
	ASSERT(vc_id == 1);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_SET,
			&vc_id,
			1);

	ASSERT(start_time_slot == 0);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_START_TIME_SLOT,
			&start_time_slot,
			1);

	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_TIME_SLOT_COUNT,
			&req_slot_count,
			1);

	/// Poll till DPCD 2C0 read 1
	/// Try for at least 150ms (30 retries, with 5ms delay after each attempt)

	while (retries < max_retries) {
		if (core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1) == DC_OK) {
			if (update_status.bits.VC_PAYLOAD_TABLE_UPDATED == 1) {
				DC_LOG_DP2("SST Update Payload: downstream payload table updated.");
				result = true;
				break;
			}
		} else {
			union dpcd_rev dpcdRev;

			if (core_link_read_dpcd(
					link,
					DP_DPCD_REV,
					&dpcdRev.raw,
					1) != DC_OK) {
				DC_LOG_ERROR("SST Update Payload: Unable to read DPCD revision "
						"of sink while polling payload table "
						"updated status bit.");
				break;
			}
		}
		retries++;
		msleep(5);
	}

	if (!result && retries == max_retries) {
		DC_LOG_ERROR("SST Update Payload: Payload table not updated after retries, "
				"continue on. Something is wrong with the branch.");
		// TODO - DP2.0 Payload: Read and log the payload table from downstream branch
	}

	return result;
}

bool dpcd_poll_for_allocation_change_trigger(struct dc_link *link)
{
	/*
	 * wait for ACT handled
	 */
	int i;
	const int act_retries = 30;
	enum act_return_status result = ACT_FAILED;
	union payload_table_update_status update_status = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated lane_status_updated;

	if (link->aux_access_disabled)
		return true;
	for (i = 0; i < act_retries; i++) {
		get_lane_status(link, link->cur_link_settings.lane_count, dpcd_lane_status, &lane_status_updated);

		if (!dp_is_cr_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_ch_eq_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_symbol_locked(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_interlane_aligned(lane_status_updated)) {
			DC_LOG_ERROR("SST Update Payload: Link loss occurred while "
					"polling for ACT handled.");
			result = ACT_LINK_LOST;
			break;
		}
		core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1);

		if (update_status.bits.ACT_HANDLED == 1) {
			DC_LOG_DP2("SST Update Payload: ACT handled by downstream.");
			result = ACT_SUCCESS;
			break;
		}

		msleep(5);
	}

	if (result == ACT_FAILED) {
		DC_LOG_ERROR("SST Update Payload: ACT still not handled after retries, "
				"continue on. Something is wrong with the branch.");
	}

	return (result == ACT_SUCCESS);
}

struct fixed31_32 calculate_sst_avg_time_slots_per_mtp(
		const struct dc_stream_state *stream,
		const struct dc_link *link)
{
	struct fixed31_32 link_bw_effective =
			dc_fixpt_from_int(
					dc_link_bandwidth_kbps(link, &link->cur_link_settings));
	struct fixed31_32 timeslot_bw_effective =
			dc_fixpt_div_int(link_bw_effective, MAX_MTP_SLOT_COUNT);
	struct fixed31_32 timing_bw =
			dc_fixpt_from_int(
					dc_bandwidth_in_kbps_from_timing(&stream->timing));
	struct fixed31_32 avg_time_slots_per_mtp =
			dc_fixpt_div(timing_bw, timeslot_bw_effective);

	return avg_time_slots_per_mtp;
}

void dc_link_clear_dprx_states(struct dc_link *link)
{
	memset(&link->dprx_states, 0, sizeof(link->dprx_states));
}

void dp_source_sequence_trace(struct dc_link *link, uint8_t dp_test_mode)
{
	if (link != NULL && link->dc->debug.enable_driver_sequence_debug)
		core_link_write_dpcd(link, DP_SOURCE_SEQUENCE,
					&dp_test_mode, sizeof(dp_test_mode));
}

#undef DC_LOGGER
#define DC_LOGGER \
	dsc->ctx->logger
static void dsc_optc_config_log(struct display_stream_compressor *dsc,
		struct dsc_optc_config *config)
{
	uint32_t precision = 1 << 28;
	uint32_t bytes_per_pixel_int = config->bytes_per_pixel / precision;
	uint32_t bytes_per_pixel_mod = config->bytes_per_pixel % precision;
	uint64_t ll_bytes_per_pix_fraq = bytes_per_pixel_mod;

	/* 7 fractional digits decimal precision for bytes per pixel is enough because DSC
	 * bits per pixel precision is 1/16th of a pixel, which means bytes per pixel precision is
	 * 1/16/8 = 1/128 of a byte, or 0.0078125 decimal
	 */
	ll_bytes_per_pix_fraq *= 10000000;
	ll_bytes_per_pix_fraq /= precision;

	DC_LOG_DSC("\tbytes_per_pixel 0x%08x (%d.%07d)",
			config->bytes_per_pixel, bytes_per_pixel_int, (uint32_t)ll_bytes_per_pix_fraq);
	DC_LOG_DSC("\tis_pixel_format_444 %d", config->is_pixel_format_444);
	DC_LOG_DSC("\tslice_width %d", config->slice_width);
}

bool dp_set_dsc_on_rx(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	bool result = false;

	if (dc_is_virtual_signal(stream->signal) || IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		result = true;
	else
		result = dm_helpers_dp_write_dsc_enable(dc->ctx, stream, enable);
	return result;
}

/* The stream with these settings can be sent (unblanked) only after DSC was enabled on RX first,
 * i.e. after dp_enable_dsc_on_rx() had been called
 */
void dp_set_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg;
		enum optc_dsc_mode optc_dsc_mode;

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in encoder */
		if (dc_is_dp_signal(stream->signal) && !IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)
				&& !link_is_dp_128b_132b_signal(pipe_ctx)) {
			DC_LOG_DSC("Setting stream encoder DSC config for engine %d:", (int)pipe_ctx->stream_res.stream_enc->id);
			dsc_optc_config_log(dsc, &dsc_optc_cfg);
			pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(pipe_ctx->stream_res.stream_enc,
									optc_dsc_mode,
									dsc_optc_cfg.bytes_per_pixel,
									dsc_optc_cfg.slice_width);

			/* PPS SDP is set elsewhere because it has to be done after DIG FE is connected to DIG BE */
		}

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		dsc_optc_config_log(dsc, &dsc_optc_cfg);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC in stream encoder */
		if (dc_is_dp_signal(stream->signal)) {
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(
						pipe_ctx->stream_res.stream_enc,
						OPTC_DSC_DISABLED, 0, 0);
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
							pipe_ctx->stream_res.stream_enc, false, NULL, true);
			}
		}

		/* disable DSC block */
		pipe_ctx->stream_res.dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
	}
}

bool dp_set_dsc_enable(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	bool result = false;

	if (!pipe_ctx->stream->timing.flags.DSC)
		goto out;
	if (!dsc)
		goto out;

	if (enable) {
		{
			dp_set_dsc_on_stream(pipe_ctx, true);
			result = true;
		}
	} else {
		dp_set_dsc_on_rx(pipe_ctx, false);
		dp_set_dsc_on_stream(pipe_ctx, false);
		result = true;
	}
out:
	return result;
}

/*
 * For dynamic bpp change case, dsc is programmed with MASTER_UPDATE_LOCK enabled;
 * hence PPS info packet update need to use frame update instead of immediate update.
 * Added parameter immediate_update for this purpose.
 * The decision to use frame update is hard-coded in function dp_update_dsc_config(),
 * which is the only place where a "false" would be passed in for param immediate_update.
 *
 * immediate_update is only applicable when DSC is enabled.
 */
bool dp_set_dsc_pps_sdp(struct pipe_ctx *pipe_ctx, bool enable, bool immediate_update)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (!pipe_ctx->stream->timing.flags.DSC || !dsc)
		return false;

	if (enable) {
		struct dsc_config dsc_cfg;
		uint8_t dsc_packed_pps[128];

		memset(&dsc_cfg, 0, sizeof(dsc_cfg));
		memset(dsc_packed_pps, 0, 128);

		/* Enable DSC hw block */
		dsc_cfg.pic_width = stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;

		dsc->funcs->dsc_get_packed_pps(dsc, &dsc_cfg, &dsc_packed_pps[0]);
		memcpy(&stream->dsc_packed_pps[0], &dsc_packed_pps[0], sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			DC_LOG_DSC("Setting stream encoder DSC PPS SDP for engine %d\n", (int)pipe_ctx->stream_res.stream_enc->id);
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										true,
										&dsc_packed_pps[0],
										immediate_update);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc,
						true,
						&dsc_packed_pps[0],
						immediate_update);
		}
	} else {
		/* disable DSC PPS in stream encoder */
		memset(&stream->dsc_packed_pps[0], 0, sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc, false, NULL, true);
		}
	}

	return true;
}


bool dp_update_dsc_config(struct pipe_ctx *pipe_ctx)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;

	if (!pipe_ctx->stream->timing.flags.DSC)
		return false;
	if (!dsc)
		return false;

	dp_set_dsc_on_stream(pipe_ctx, true);
	dp_set_dsc_pps_sdp(pipe_ctx, true, false);
	return true;
}

#undef DC_LOGGER
#define DC_LOGGER \
	link->ctx->logger
