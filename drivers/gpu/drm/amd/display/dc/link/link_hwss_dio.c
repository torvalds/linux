/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "link_hwss_dio.h"
#include "core_types.h"
#include "dc_link_dp.h"
#include "link_enc_cfg.h"

void set_dio_throttled_vcp_size(struct pipe_ctx *pipe_ctx,
		struct fixed31_32 throttled_vcp_size)
{
	struct stream_encoder *stream_encoder = pipe_ctx->stream_res.stream_enc;

	stream_encoder->funcs->set_throttled_vcp_size(
				stream_encoder,
				throttled_vcp_size);
}

void setup_dio_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);

	link_enc->funcs->connect_dig_be_to_fe(link_enc,
			pipe_ctx->stream_res.stream_enc->id, true);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_CONNECT_DIG_FE_BE);
}

void reset_dio_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);

	link_enc->funcs->connect_dig_be_to_fe(
			link_enc,
			pipe_ctx->stream_res.stream_enc->id,
			false);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_DISCONNECT_DIG_FE_BE);

}

void enable_dio_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	if (dc_is_dp_sst_signal(signal))
		link_enc->funcs->enable_dp_output(
				link_enc,
				link_settings,
				clock_source);
	else
		link_enc->funcs->enable_dp_mst_output(
				link_enc,
				link_settings,
				clock_source);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_LINK_PHY);
}

void disable_dio_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->disable_output(link_enc, signal);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);
}

void set_dio_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->dp_set_phy_pattern(link_enc, tp_params);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

void set_dio_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->dp_set_lane_settings(link_enc, link_settings, lane_settings);
}

static const struct link_hwss dio_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.disable_dp_link_output = disable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
	},
};

bool can_use_dio_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link->link_enc != NULL;
}

const struct link_hwss *get_dio_link_hwss(void)
{
	return &dio_link_hwss;
}
