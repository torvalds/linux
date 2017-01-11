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

/*******************************************************************************
 * Private definitions
 ******************************************************************************/

struct stream {
	struct core_stream protected;
	int ref_count;
};

#define DC_STREAM_TO_STREAM(dc_stream) container_of(dc_stream, struct stream, protected.public)

/*******************************************************************************
 * Private functions
 ******************************************************************************/

static bool construct(struct core_stream *stream,
	const struct dc_sink *dc_sink_data)
{
	uint32_t i = 0;

	stream->sink = DC_SINK_TO_CORE(dc_sink_data);
	stream->ctx = stream->sink->ctx;
	stream->public.sink = dc_sink_data;

	dc_sink_retain(dc_sink_data);

	/* Copy audio modes */
	/* TODO - Remove this translation */
	for (i = 0; i < (dc_sink_data->edid_caps.audio_mode_count); i++)
	{
		stream->public.audio_info.modes[i].channel_count = dc_sink_data->edid_caps.audio_modes[i].channel_count;
		stream->public.audio_info.modes[i].format_code = dc_sink_data->edid_caps.audio_modes[i].format_code;
		stream->public.audio_info.modes[i].sample_rates.all = dc_sink_data->edid_caps.audio_modes[i].sample_rate;
		stream->public.audio_info.modes[i].sample_size = dc_sink_data->edid_caps.audio_modes[i].sample_size;
	}
	stream->public.audio_info.mode_count = dc_sink_data->edid_caps.audio_mode_count;
	stream->public.audio_info.audio_latency = dc_sink_data->edid_caps.audio_latency;
	stream->public.audio_info.video_latency = dc_sink_data->edid_caps.video_latency;
	memmove(
		stream->public.audio_info.display_name,
		dc_sink_data->edid_caps.display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);
	stream->public.audio_info.manufacture_id = dc_sink_data->edid_caps.manufacturer_id;
	stream->public.audio_info.product_id = dc_sink_data->edid_caps.product_id;
	stream->public.audio_info.flags.all = dc_sink_data->edid_caps.speaker_flags;

	/* TODO - Unhardcode port_id */
	stream->public.audio_info.port_id[0] = 0x5558859e;
	stream->public.audio_info.port_id[1] = 0xd989449;

	/* EDID CAP translation for HDMI 2.0 */
	stream->public.timing.flags.LTE_340MCSC_SCRAMBLE = dc_sink_data->edid_caps.lte_340mcsc_scramble;

	stream->status.link = &stream->sink->link->public;
	return true;
}

static void destruct(struct core_stream *stream)
{
	dc_sink_release(&stream->sink->public);
	if (stream->public.out_transfer_func != NULL) {
		dc_transfer_func_release(
				stream->public.out_transfer_func);
		stream->public.out_transfer_func = NULL;
	}
}

void dc_stream_retain(const struct dc_stream *dc_stream)
{
	struct stream *stream = DC_STREAM_TO_STREAM(dc_stream);

	ASSERT(stream->ref_count > 0);
	stream->ref_count++;
}

void dc_stream_release(const struct dc_stream *public)
{
	struct stream *stream = DC_STREAM_TO_STREAM(public);
	struct core_stream *protected = DC_STREAM_TO_CORE(public);

	if (public != NULL) {
		ASSERT(stream->ref_count > 0);
		stream->ref_count--;

		if (stream->ref_count == 0) {
			destruct(protected);
			dm_free(stream);
		}
	}
}

struct dc_stream *dc_create_stream_for_sink(
		const struct dc_sink *dc_sink)
{
	struct core_sink *sink = DC_SINK_TO_CORE(dc_sink);
	struct stream *stream;

	if (sink == NULL)
		goto alloc_fail;

	stream = dm_alloc(sizeof(struct stream));

	if (NULL == stream)
		goto alloc_fail;

	if (false == construct(&stream->protected, dc_sink))
			goto construct_fail;

	stream->ref_count++;

	return &stream->protected.public;

construct_fail:
	dm_free(stream);

alloc_fail:
	return NULL;
}

const struct dc_stream_status *dc_stream_get_status(
	const struct dc_stream *dc_stream)
{
	uint8_t i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct core_dc *dc = DC_TO_CORE(stream->ctx->dc);

	for (i = 0; i < dc->current_context->stream_count; i++)
		if (stream == dc->current_context->streams[i])
			return &dc->current_context->stream_status[i];

	return NULL;
}

/**
 * Update the cursor attributes and set cursor surface address
 */
bool dc_stream_set_cursor_attributes(
	const struct dc_stream *dc_stream,
	const struct dc_cursor_attributes *attributes)
{
	int i;
	struct core_stream *stream;
	struct core_dc *core_dc;
	struct resource_context *res_ctx;
	bool ret = false;

	if (NULL == dc_stream) {
		dm_error("DC: dc_stream is NULL!\n");
			return false;
	}
	if (NULL == attributes) {
		dm_error("DC: attributes is NULL!\n");
			return false;
	}

	stream = DC_STREAM_TO_CORE(dc_stream);
	core_dc = DC_TO_CORE(stream->ctx->dc);
	res_ctx = &core_dc->current_context->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if ((pipe_ctx->stream == stream) &&
			(pipe_ctx->ipp != NULL)) {
			struct input_pixel_processor *ipp = pipe_ctx->ipp;

			if (ipp->funcs->ipp_cursor_set_attributes(
				ipp, attributes))
				ret = true;
		}
	}

	return ret;
}

bool dc_stream_set_cursor_position(
	const struct dc_stream *dc_stream,
	const struct dc_cursor_position *position)
{
	int i;
	struct core_stream *stream;
	struct core_dc *core_dc;
	struct resource_context *res_ctx;
	bool ret = false;

	if (NULL == dc_stream) {
		dm_error("DC: dc_stream is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	stream = DC_STREAM_TO_CORE(dc_stream);
	core_dc = DC_TO_CORE(stream->ctx->dc);
	res_ctx = &core_dc->current_context->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

		if ((pipe_ctx->stream == stream) &&
				(pipe_ctx->ipp != NULL)) {
			struct input_pixel_processor *ipp = pipe_ctx->ipp;
			struct dc_cursor_mi_param param = {
				.pixel_clk_khz = dc_stream->timing.pix_clk_khz,
				.ref_clk_khz = 48000,/*todo refclk*/
				.viewport_x_start = pipe_ctx->scl_data.viewport.x,
				.viewport_width = pipe_ctx->scl_data.viewport.width,
				.h_scale_ratio = pipe_ctx->scl_data.ratios.horz,
			};

			ipp->funcs->ipp_cursor_set_position(ipp, position, &param);
			ret = true;
		}
	}

	return ret;
}

uint32_t dc_stream_get_vblank_counter(const struct dc_stream *dc_stream)
{
	uint8_t i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct core_dc *core_dc = DC_TO_CORE(stream->ctx->dc);
	struct resource_context *res_ctx =
		&core_dc->current_context->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		return tg->funcs->get_frame_count(tg);
	}

	return 0;
}

uint32_t dc_stream_get_scanoutpos(
		const struct dc_stream *dc_stream,
		uint32_t *vbl,
		uint32_t *position)
{
	uint8_t i;
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct core_dc *core_dc = DC_TO_CORE(stream->ctx->dc);
	struct resource_context *res_ctx =
		&core_dc->current_context->res_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		struct timing_generator *tg = res_ctx->pipe_ctx[i].tg;

		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		return tg->funcs->get_scanoutpos(tg, vbl, position);
	}

	return 0;
}


void dc_stream_log(
	const struct dc_stream *stream,
	struct dal_logger *dm_logger,
	enum dc_log_type log_type)
{
	const struct core_stream *core_stream =
		DC_STREAM_TO_CORE(stream);

	dm_logger_write(dm_logger,
			log_type,
			"core_stream 0x%x: src: %d, %d, %d, %d; dst: %d, %d, %d, %d;\n",
			core_stream,
			core_stream->public.src.x,
			core_stream->public.src.y,
			core_stream->public.src.width,
			core_stream->public.src.height,
			core_stream->public.dst.x,
			core_stream->public.dst.y,
			core_stream->public.dst.width,
			core_stream->public.dst.height);
	dm_logger_write(dm_logger,
			log_type,
			"\tpix_clk_khz: %d, h_total: %d, v_total: %d\n",
			core_stream->public.timing.pix_clk_khz,
			core_stream->public.timing.h_total,
			core_stream->public.timing.v_total);
	dm_logger_write(dm_logger,
			log_type,
			"\tsink name: %s, serial: %d\n",
			core_stream->sink->public.edid_caps.display_name,
			core_stream->sink->public.edid_caps.serial_number);
	dm_logger_write(dm_logger,
			log_type,
			"\tlink: %d\n",
			core_stream->sink->link->public.link_index);
}
