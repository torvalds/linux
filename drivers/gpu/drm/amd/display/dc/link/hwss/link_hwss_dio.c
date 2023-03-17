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
	struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

	link_enc->funcs->connect_dig_be_to_fe(link_enc,
			pipe_ctx->stream_res.stream_enc->id, true);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		link_dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_CONNECT_DIG_FE_BE);
	if (stream_enc->funcs->enable_fifo)
		stream_enc->funcs->enable_fifo(stream_enc);
}

void reset_dio_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);
	struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

	if (stream_enc && stream_enc->funcs->disable_fifo)
		stream_enc->funcs->disable_fifo(stream_enc);

	link_enc->funcs->connect_dig_be_to_fe(
			link_enc,
			pipe_ctx->stream_res.stream_enc->id,
			false);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		link_dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_DISCONNECT_DIG_FE_BE);

}

void setup_dio_stream_attribute(struct pipe_ctx *pipe_ctx)
{
	struct stream_encoder *stream_encoder = pipe_ctx->stream_res.stream_enc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;

	if (!dc_is_virtual_signal(stream->signal))
		stream_encoder->funcs->setup_stereo_sync(
				stream_encoder,
				pipe_ctx->stream_res.tg->inst,
				stream->timing.timing_3d_format != TIMING_3D_FORMAT_NONE);

	if (dc_is_dp_signal(stream->signal))
		stream_encoder->funcs->dp_set_stream_attribute(
				stream_encoder,
				&stream->timing,
				stream->output_color_space,
				stream->use_vsc_sdp_for_colorimetry,
				link->dpcd_caps.dprx_feature.bits.SST_SPLIT_SDP_CAP);
	else if (dc_is_hdmi_tmds_signal(stream->signal))
		stream_encoder->funcs->hdmi_set_stream_attribute(
				stream_encoder,
				&stream->timing,
				stream->phy_pix_clk,
				pipe_ctx->stream_res.audio != NULL);
	else if (dc_is_dvi_signal(stream->signal))
		stream_encoder->funcs->dvi_set_stream_attribute(
				stream_encoder,
				&stream->timing,
				(stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK) ?
						true : false);
	else if (dc_is_lvds_signal(stream->signal))
		stream_encoder->funcs->lvds_set_stream_attribute(
				stream_encoder,
				&stream->timing);

	if (dc_is_dp_signal(stream->signal))
		link_dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DP_STREAM_ATTR);
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
	link_dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_LINK_PHY);
}

void disable_dio_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->disable_output(link_enc, signal);
	link_dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);
}

void set_dio_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->dp_set_phy_pattern(link_enc, tp_params);
	link_dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

void set_dio_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->dp_set_lane_settings(link_enc, link_settings, lane_settings);
}

static void update_dio_stream_allocation_table(struct dc_link *link,
		const struct link_resource *link_res,
		const struct link_mst_stream_allocation_table *table)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	ASSERT(link_enc);
	link_enc->funcs->update_mst_stream_allocation_table(link_enc, table);
}

void setup_dio_audio_output(struct pipe_ctx *pipe_ctx,
		struct audio_output *audio_output, uint32_t audio_inst)
{
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dp_audio_setup(
				pipe_ctx->stream_res.stream_enc,
				audio_inst,
				&pipe_ctx->stream->audio_info);
	else
		pipe_ctx->stream_res.stream_enc->funcs->hdmi_audio_setup(
				pipe_ctx->stream_res.stream_enc,
				audio_inst,
				&pipe_ctx->stream->audio_info,
				&audio_output->crtc_info);
}

void enable_dio_audio_packet(struct pipe_ctx *pipe_ctx)
{
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dp_audio_enable(
				pipe_ctx->stream_res.stream_enc);

	pipe_ctx->stream_res.stream_enc->funcs->audio_mute_control(
			pipe_ctx->stream_res.stream_enc, false);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		link_dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_ENABLE_AUDIO_STREAM);
}

void disable_dio_audio_packet(struct pipe_ctx *pipe_ctx)
{
	pipe_ctx->stream_res.stream_enc->funcs->audio_mute_control(
			pipe_ctx->stream_res.stream_enc, true);

	if (pipe_ctx->stream_res.audio) {
		if (dc_is_dp_signal(pipe_ctx->stream->signal))
			pipe_ctx->stream_res.stream_enc->funcs->dp_audio_disable(
					pipe_ctx->stream_res.stream_enc);
		else
			pipe_ctx->stream_res.stream_enc->funcs->hdmi_audio_disable(
					pipe_ctx->stream_res.stream_enc);
	}

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		link_dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_DISABLE_AUDIO_STREAM);
}

static const struct link_hwss dio_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.setup_stream_attribute = setup_dio_stream_attribute,
	.disable_link_output = disable_dio_link_output,
	.setup_audio_output = setup_dio_audio_output,
	.enable_audio_packet = enable_dio_audio_packet,
	.disable_audio_packet = disable_dio_audio_packet,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
		.update_stream_allocation_table = update_dio_stream_allocation_table,
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
