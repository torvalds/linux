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
#include "core_types.h"
#include "hw_sequencer.h"
#include "resource.h"
#include "ipp.h"
#include "timing_generator.h"

struct target {
	struct core_target protected;
	int ref_count;
};

#define DC_TARGET_TO_TARGET(dc_target) \
	container_of(dc_target, struct target, protected.public)
#define CORE_TARGET_TO_TARGET(core_target) \
	container_of(core_target, struct target, protected)

static void construct(
	struct core_target *target,
	struct dc_context *ctx,
	struct dc_stream *dc_streams[],
	uint8_t stream_count)
{
	uint8_t i;
	for (i = 0; i < stream_count; i++) {
		target->public.streams[i] = dc_streams[i];
		dc_stream_retain(dc_streams[i]);
	}

	target->ctx = ctx;
	target->public.stream_count = stream_count;
}

static void destruct(struct core_target *core_target)
{
	int i;

	for (i = 0; i < core_target->public.stream_count; i++) {
		dc_stream_release(
			(struct dc_stream *)core_target->public.streams[i]);
		core_target->public.streams[i] = NULL;
	}
}

void dc_target_retain(const struct dc_target *dc_target)
{
	struct target *target = DC_TARGET_TO_TARGET(dc_target);

	target->ref_count++;
}

void dc_target_release(const struct dc_target *dc_target)
{
	struct target *target = DC_TARGET_TO_TARGET(dc_target);
	struct core_target *protected = DC_TARGET_TO_CORE(dc_target);

	ASSERT(target->ref_count > 0);
	target->ref_count--;
	if (target->ref_count == 0) {
		destruct(protected);
		dm_free(target);
	}
}

const struct dc_target_status *dc_target_get_status(
					const struct dc_target* dc_target)
{
	uint8_t i;
	struct core_target* target = DC_TARGET_TO_CORE(dc_target);
	struct core_dc *dc = DC_TO_CORE(target->ctx->dc);

	for (i = 0; i < dc->current_context->target_count; i++)
		if (target == dc->current_context->targets[i])
			return &dc->current_context->target_status[i];

	return NULL;
}

struct dc_target *dc_create_target_for_streams(
		struct dc_stream *dc_streams[],
		uint8_t stream_count)
{
	struct core_stream *stream;
	struct target *target;

	if (0 == stream_count)
		goto target_alloc_fail;

	stream = DC_STREAM_TO_CORE(dc_streams[0]);

	target = dm_alloc(sizeof(struct target));

	if (NULL == target)
		goto target_alloc_fail;

	construct(&target->protected, stream->ctx, dc_streams, stream_count);

	dc_target_retain(&target->protected.public);

	return &target->protected.public;

target_alloc_fail:
	return NULL;
}

bool dc_target_is_connected_to_sink(
		const struct dc_target * dc_target,
		const struct dc_sink *dc_sink)
{
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	uint8_t i;
	for (i = 0; i < target->public.stream_count; i++) {
		if (target->public.streams[i]->sink == dc_sink)
			return true;
	}
	return false;
}

/**
 * Update the cursor attributes and set cursor surface address
 */
bool dc_target_set_cursor_attributes(
	struct dc_target *dc_target,
	const struct dc_cursor_attributes *attributes)
{
	uint8_t i, j;
	struct core_target *target;
	struct core_dc *core_dc;
	struct resource_context *res_ctx;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
			return false;

	}
	if (NULL == attributes) {
		dm_error("DC: attributes is NULL!\n");
			return false;

	}

	target = DC_TARGET_TO_CORE(dc_target);
	core_dc = DC_TO_CORE(target->ctx->dc);
	res_ctx = &core_dc->current_context->res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct input_pixel_processor *ipp =
						res_ctx->pipe_ctx[j].ipp;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			/* As of writing of this code cursor is on the top
			 * plane so we only need to set it on first pipe we
			 * find. May need to make this code dce specific later.
			 */
			if (ipp->funcs->ipp_cursor_set_attributes(
							ipp, attributes))
				return true;
		}
	}

	return false;
}

bool dc_target_set_cursor_position(
	struct dc_target *dc_target,
	const struct dc_cursor_position *position)
{
	uint8_t i, j;
	struct core_target *target;
	struct core_dc *core_dc;
	struct resource_context *res_ctx;

	if (NULL == dc_target) {
		dm_error("DC: dc_target is NULL!\n");
		return false;
	}

	if (NULL == position) {
		dm_error("DC: cursor position is NULL!\n");
		return false;
	}

	target = DC_TARGET_TO_CORE(dc_target);
	core_dc = DC_TO_CORE(target->ctx->dc);
	res_ctx = &core_dc->current_context->res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct input_pixel_processor *ipp =
						res_ctx->pipe_ctx[j].ipp;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			/* As of writing of this code cursor is on the top
			 * plane so we only need to set it on first pipe we
			 * find. May need to make this code dce specific later.
			 */
			ipp->funcs->ipp_cursor_set_position(ipp, position);
			return true;
		}
	}

	return false;
}

uint32_t dc_target_get_vblank_counter(const struct dc_target *dc_target)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct core_dc *core_dc = DC_TO_CORE(target->ctx->dc);
	struct resource_context *res_ctx =
		&core_dc->current_context->res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			return tg->funcs->get_frame_count(tg);
		}
	}

	return 0;
}

uint32_t dc_target_get_scanoutpos(
		const struct dc_target *dc_target,
		uint32_t *vbl,
		uint32_t *position)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct core_dc *core_dc = DC_TO_CORE(target->ctx->dc);
	struct resource_context *res_ctx =
		&core_dc->current_context->res_ctx;

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			return tg->funcs->get_scanoutpos(tg, vbl, position);
		}
	}

	return 0;
}

void dc_target_log(
	const struct dc_target *dc_target,
	struct dal_logger *dm_logger,
	enum dc_log_type log_type)
{
	int i;

	const struct core_target *core_target =
			CONST_DC_TARGET_TO_CORE(dc_target);

	dm_logger_write(dm_logger,
			log_type,
			"core_target 0x%x: stream_count=%d\n",
			core_target,
			core_target->public.stream_count);

	for (i = 0; i < core_target->public.stream_count; i++) {
		const struct core_stream *core_stream =
			DC_STREAM_TO_CORE(core_target->public.streams[i]);

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
}
