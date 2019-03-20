/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dc.h"
#include "core_types.h"
#include "resource.h"
#include "ipp.h"
#include "timing_generator.h"
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "dcn10/dcn10_hw_sequencer.h"
#endif

#define DC_LOGGER dc->ctx->logger

/*******************************************************************************
 * Private functions
 ******************************************************************************/
void update_stream_signal(struct dc_stream_state *stream, struct dc_sink *sink)
{
	if (sink->sink_signal == SIGNAL_TYPE_NONE)
		stream->signal = stream->link->connector_signal;
	else
		stream->signal = sink->sink_signal;

	if (dc_is_dvi_signal(stream->signal)) {
		if (stream->ctx->dc->caps.dual_link_dvi &&
		    (stream->timing.pix_clk_100hz / 10) > TMDS_MAX_PIXEL_CLOCK &&
		    sink->sink_signal != SIGNAL_TYPE_DVI_SINGLE_LINK)
			stream->signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		else
			stream->signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
}

static void construct(struct dc_stream_state *stream,
	struct dc_sink *dc_sink_data)
{
	uint32_t i = 0;

	stream->sink = dc_sink_data;
	dc_sink_retain(dc_sink_data);

	stream->ctx = dc_sink_data->ctx;
	stream->link = dc_sink_data->link;
	stream->sink_patches = dc_sink_data->edid_caps.panel_patch;
	stream->converter_disable_audio = dc_sink_data->converter_disable_audio;
	stream->qs_bit = dc_sink_data->edid_caps.qs_bit;
	stream->qy_bit = dc_sink_data->edid_caps.qy_bit;

	/* Copy audio modes */
	/* TODO - Remove this translation */
	for (i = 0; i < (dc_sink_data->edid_caps.audio_mode_count); i++)
	{
		stream->audio_info.modes[i].channel_count = dc_sink_data->edid_caps.audio_modes[i].channel_count;
		stream->audio_info.modes[i].format_code = dc_sink_data->edid_caps.audio_modes[i].format_code;
		stream->audio_info.modes[i].sample_rates.all = dc_sink_data->edid_caps.audio_modes[i].sample_rate;
		stream->audio_info.modes[i].sample_size = dc_sink_data->edid_caps.audio_modes[i].sample_size;
	}
	stream->audio_info.mode_count = dc_sink_data->edid_caps.audio_mode_count;
	stream->audio_info.audio_latency = dc_sink_data->edid_caps.audio_latency;
	stream->audio_info.video_latency = dc_sink_data->edid_caps.video_latency;
	memmove(
		stream->audio_info.display_name,
		dc_sink_data->edid_caps.display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);
	stream->audio_info.manufacture_id = dc_sink_data->edid_caps.manufacturer_id;
	stream->audio_info.product_id = dc_sink_data->edid_caps.product_id;
	stream->audio_info.flags.all = dc_sink_data->edid_caps.speaker_flags;

	if (dc_sink_data->dc_container_id != NULL) {
		struct dc_container_id *dc_container_id = dc_sink_data->dc_container_id;

		stream->audio_info.port_id[0] = dc_container_id->portId[0];
		stream->audio_info.port_id[1] = dc_container_id->portId[1];
	} else {
		/* TODO - WindowDM has implemented,
		other DMs need Unhardcode port_id */
		stream->audio_info.port_id[0] = 0x5558859e;
		stream->audio_info.port_id[1] = 0xd989449;
	}

	/* EDID CAP translation for HDMI 2.0 */
	stream->timing.flags.LTE_340MCSC_SCRAMBLE = dc_sink_data->edid_caps.lte_340mcsc_scramble;

	update_stream_signal(stream, dc_sink_data);

	stream->out_transfer_func = dc_create_transfer_func();
	stream->out_transfer_func->type = TF_TYPE_BYPASS;
	stream->out_transfer_func->ctx = stream->ctx;

	stream->stream_id = stream->ctx->dc_stream_id_count;
	stream->ctx->dc_stream_id_count++;
}

static void destruct(struct dc_stream_state *stream)
{
	dc_sink_release(stream->sink);
	if (stream->out_transfer_func != NULL) {
		dc_transfer_func_release(stream->out_transfer_func);
		stream->out_transfer_func = NULL;
	}
}

void dc_stream_retain(struct dc_stream_state *stream)
{
	kref_get(&stream->refcount);
}

static void dc_stream_free(struct kref *kref)
{
	struct dc_stream_state *stream = container_of(kref, struct dc_stream_state, refcount);

	destruct(stream);
	kfree(stream);
}

void dc_stream_release(struct dc_stream_state *stream)
{
	if (stream != NULL) {
		kref_put(&stream->refcount, dc_stream_free);
	}
}

struct dc_stream_state *dc_create_stream_for_sink(
		struct dc_sink *sink)
{
	struct dc_stream_state *stream;

	if (sink == NULL)
		return NULL;

	stream = kzalloc(sizeof(struct dc_stream_state), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	construct(stream, sink);

	kref_init(&stream->refcount);

	return stream;
}

/**
 * dc_stream_get_status_from_state - Get stream status from given dc state
 * @state: DC state to find the stream status in
 * @stream: The stream to get the stream status for
 *
 * The given stream is expected to exist in the given dc state. Otherwise, NULL
 * will be returned.
 */
struct dc_stream_status *dc_stream_get_status_from_state(
	struct dc_state *state,
	struct dc_stream_state *stream)
{
	uint8_t i;

	for (i = 0; i < state->stream_count; i++) {
		if (stream == state->streams[i])
			return &state->stream_status[i];
	}

	return NULL;
}

/**
 * dc_stream_get_status() - Get current stream status of the given stream state
 * @stream: The stream to get the stream status for.
 *
 * The given stream is expected to exist in dc->current_state. Otherwise, NULL
 * will be returned.
 */
struct dc_stream_status *dc_stream_get_status(
	struct dc_stream_state *stream)
{
	struct dc *dc = stream->ctx->dc;
	return dc_stream_get_status_from_state(dc->current_state, stream);
}

static void delay_cursor_until_vupdate(struct pipe_ctx *pipe_ctx, struct dc *dc)
{
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	unsigned int vupdate_line;
	unsigned int lines_to_vupdate, us_to_vupdate, vpos, nvpos;
	struct dc_stream_state *stream = pipe_ctx->stream;
	unsigned int us_per_line;

	if (stream->ctx->asic_id.chip_family == FAMILY_RV &&
			ASIC_REV_IS_RAVEN(stream->ctx->asic_id.hw_internal_rev)) {

		vupdate_line = get_vupdate_offset_from_vsync(pipe_ctx);
		dc_stream_get_crtc_position(dc, &stream, 1, &vpos, &nvpos);

		if (vpos >= vupdate_line)
			return;

		us_per_line = stream->timing.h_total * 10000 / stream->timing.pix_clk_100hz;
		lines_to_vupdate = vupdate_line - vpos;
		us_to_vupdate = lines_to_vupdate * us_per_line;

		/* 70 us is a conservative estimate of cursor update time*/
		if (us_to_vupdate < 70)
			udelay(us_to_vupdate);
	}
#endif
}

/**
 * dc_stream_set_cursor_attributes() - Update cursor attributes and set cursor surface address
 */
bool dc_stream_set_cursor_attributes(
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes)
{
	int i;
	struct dc  *core_dc;
	struct resource_context *res_ctx;
	struct pipe_ctx *pipe_to_program = NULL;

	if (NULL == stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}
	if (NULL == attributes) {
		dm_error("DC: attributes is NULL!\n");
		return false;
	}

	if (attributes->address.quad_part == 0) {
		dm_output_to_console("DC: Cursor address is 0!\n");
		return false;
	}

	core_dc = stream->ctx->dc;
	res_ctx = &core_dc->current_state->res_ctx;
	stream->cursor_attributes = *attributes;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		if (!pipe_to_program) {
			pipe_to_program = pipe_ctx;

			delay_cursor_until_vupdate(pipe_ctx, core_dc);
			core_dc->hwss.pipe_control_lock(core_dc, pipe_to_program, true);
		}

		core_dc->hwss.set_cursor_attribute(pipe_ctx);
		if (core_dc->hwss.set_cursor_sdr_white_level)
			core_dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
	}

	if (pipe_to_program)
		core_dc->hwss.pipe_control_lock(core_dc, pipe_to_program, false);

	return true;
}

bool dc_stream_set_cursor_position(
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position)
{
	int i;
	struct dc  *core_dc;
	struct resource_context *res_ctx;
	struct pipe_ctx *pipe_to_program = NULL;

	if (NULL == stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	core_dc = stream->ctx->dc;
	res_ctx = &core_dc->current_state->res_ctx;
	stream->cursor_position = *position;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream ||
				(!pipe_ctx->plane_res.mi  && !pipe_ctx->plane_res.hubp) ||
				!pipe_ctx->plane_state ||
				(!pipe_ctx->plane_res.xfm && !pipe_ctx->plane_res.dpp) ||
				!pipe_ctx->plane_res.ipp)
			continue;

		if (!pipe_to_program) {
			pipe_to_program = pipe_ctx;

			delay_cursor_until_vupdate(pipe_ctx, core_dc);
			core_dc->hwss.pipe_control_lock(core_dc, pipe_to_program, true);
		}

		core_dc->hwss.set_cursor_position(pipe_ctx);
	}

	if (pipe_to_program)
		core_dc->hwss.pipe_control_lock(core_dc, pipe_to_program, false);

	return true;
}

uint32_t dc_stream_get_vblank_counter(const struct dc_stream_state *stream)
{
	uint8_t i;
	struct dc  *core_dc = stream->ctx->dc;
	struct resource_context *res_ctx =
		&core_dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		return tg->funcs->get_frame_count(tg);
	}

	return 0;
}

static void build_dp_sdp_info_frame(struct pipe_ctx *pipe_ctx,
		const uint8_t  *custom_sdp_message,
		unsigned int sdp_message_size)
{
	uint8_t i;
	struct encoder_info_frame *info = &pipe_ctx->stream_res.encoder_info_frame;

	/* set valid info */
	info->dpsdp.valid = true;

	/* set sdp message header */
	info->dpsdp.hb0 = custom_sdp_message[0]; /* package id */
	info->dpsdp.hb1 = custom_sdp_message[1]; /* package type */
	info->dpsdp.hb2 = custom_sdp_message[2]; /* package specific byte 0 any data */
	info->dpsdp.hb3 = custom_sdp_message[3]; /* package specific byte 0 any data */

	/* set sdp message data */
	for (i = 0; i < 32; i++)
		info->dpsdp.sb[i] = (custom_sdp_message[i+4]);

}

static void invalid_dp_sdp_info_frame(struct pipe_ctx *pipe_ctx)
{
	struct encoder_info_frame *info = &pipe_ctx->stream_res.encoder_info_frame;

	/* in-valid info */
	info->dpsdp.valid = false;
}

bool dc_stream_send_dp_sdp(const struct dc_stream_state *stream,
		const uint8_t *custom_sdp_message,
		unsigned int sdp_message_size)
{
	int i;
	struct dc  *core_dc;
	struct resource_context *res_ctx;

	if (stream == NULL) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	core_dc = stream->ctx->dc;
	res_ctx = &core_dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		build_dp_sdp_info_frame(pipe_ctx, custom_sdp_message, sdp_message_size);

		core_dc->hwss.update_info_frame(pipe_ctx);

		invalid_dp_sdp_info_frame(pipe_ctx);
	}

	return true;
}

bool dc_stream_get_scanoutpos(const struct dc_stream_state *stream,
				  uint32_t *v_blank_start,
				  uint32_t *v_blank_end,
				  uint32_t *h_position,
				  uint32_t *v_position)
{
	uint8_t i;
	bool ret = false;
	struct dc  *core_dc = stream->ctx->dc;
	struct resource_context *res_ctx =
		&core_dc->current_state->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].stream_res.tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		tg->funcs->get_scanoutpos(tg,
					  v_blank_start,
					  v_blank_end,
					  h_position,
					  v_position);

		ret = true;
		break;
	}

	return ret;
}

void dc_stream_log(const struct dc *dc, const struct dc_stream_state *stream)
{
	DC_LOG_DC(
			"core_stream 0x%p: src: %d, %d, %d, %d; dst: %d, %d, %d, %d, colorSpace:%d\n",
			stream,
			stream->src.x,
			stream->src.y,
			stream->src.width,
			stream->src.height,
			stream->dst.x,
			stream->dst.y,
			stream->dst.width,
			stream->dst.height,
			stream->output_color_space);
	DC_LOG_DC(
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d, pixelencoder:%d, displaycolorDepth:%d\n",
			stream->timing.pix_clk_100hz / 10,
			stream->timing.h_total,
			stream->timing.v_total,
			stream->timing.pixel_encoding,
			stream->timing.display_color_depth);
	DC_LOG_DC(
			"\tlink: %d\n",
			stream->link->link_index);
}
