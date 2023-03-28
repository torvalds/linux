/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 *
 */
#include "link_fpga.h"
#include "link/link_dpms.h"
#include "dm_helpers.h"
#include "link_hwss.h"
#include "dccg.h"
#include "resource.h"

#define DC_LOGGER_INIT(logger)

void dp_fpga_hpo_enable_link_and_stream(struct dc_state *state, struct pipe_ctx *pipe_ctx)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct link_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp;
	uint8_t req_slot_count = 0;
	uint8_t vc_id = 1; /// VC ID always 1 for SST
	struct dc_link_settings link_settings = pipe_ctx->link_config.dp_link_settings;
	const struct link_hwss *link_hwss = get_link_hwss(stream->link, &pipe_ctx->link_res);
	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	stream->link->cur_link_settings = link_settings;

	if (link_hwss->ext.enable_dp_link_output)
		link_hwss->ext.enable_dp_link_output(stream->link, &pipe_ctx->link_res,
				stream->signal, pipe_ctx->clock_source->id,
				&link_settings);

	/* Enable DP_STREAM_ENC */
	dc->hwss.enable_stream(pipe_ctx);

	/* Set DPS PPS SDP (AKA "info frames") */
	if (pipe_ctx->stream->timing.flags.DSC) {
		link_set_dsc_pps_packet(pipe_ctx, true, true);
	}

	/* Allocate Payload */
	if ((stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) && (state->stream_count > 1)) {
		// MST case
		uint8_t i;

		proposed_table.stream_count = state->stream_count;
		for (i = 0; i < state->stream_count; i++) {
			avg_time_slots_per_mtp = link_calculate_sst_avg_time_slots_per_mtp(state->streams[i], state->streams[i]->link);
			req_slot_count = dc_fixpt_ceil(avg_time_slots_per_mtp);
			proposed_table.stream_allocations[i].slot_count = req_slot_count;
			proposed_table.stream_allocations[i].vcp_id = i+1;
			/* NOTE: This makes assumption that pipe_ctx index is same as stream index */
			proposed_table.stream_allocations[i].hpo_dp_stream_enc = state->res_ctx.pipe_ctx[i].stream_res.hpo_dp_stream_enc;
		}
	} else {
		// SST case
		avg_time_slots_per_mtp = link_calculate_sst_avg_time_slots_per_mtp(stream, stream->link);
		req_slot_count = dc_fixpt_ceil(avg_time_slots_per_mtp);
		proposed_table.stream_count = 1; /// Always 1 stream for SST
		proposed_table.stream_allocations[0].slot_count = req_slot_count;
		proposed_table.stream_allocations[0].vcp_id = vc_id;
		proposed_table.stream_allocations[0].hpo_dp_stream_enc = pipe_ctx->stream_res.hpo_dp_stream_enc;
	}

	link_hwss->ext.update_stream_allocation_table(stream->link,
			&pipe_ctx->link_res,
			&proposed_table);

	if (link_hwss->ext.set_throttled_vcp_size)
		link_hwss->ext.set_throttled_vcp_size(pipe_ctx, avg_time_slots_per_mtp);

	dc->hwss.unblank_stream(pipe_ctx, &stream->link->cur_link_settings);
	dc->hwss.enable_audio_stream(pipe_ctx);
}

