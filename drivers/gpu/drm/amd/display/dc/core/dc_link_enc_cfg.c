/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "link_enc_cfg.h"
#include "resource.h"
#include "dc_link_dp.h"

/* Check whether stream is supported by DIG link encoders. */
static bool is_dig_link_enc_stream(struct dc_stream_state *stream)
{
	bool is_dig_stream = false;
	struct link_encoder *link_enc = NULL;
	int i;

	/* Loop over created link encoder objects. */
	for (i = 0; i < stream->ctx->dc->res_pool->res_cap->num_dig_link_enc; i++) {
		link_enc = stream->ctx->dc->res_pool->link_encoders[i];

		if (link_enc &&
				((uint32_t)stream->signal & link_enc->output_signals)) {
			if (dc_is_dp_signal(stream->signal)) {
				/* DIGs do not support DP2.0 streams with 128b/132b encoding. */
				struct dc_link_settings link_settings = {0};

				decide_link_settings(stream, &link_settings);
				if ((link_settings.link_rate >= LINK_RATE_LOW) &&
						link_settings.link_rate <= LINK_RATE_HIGH3) {
					is_dig_stream = true;
					break;
				}
			} else {
				is_dig_stream = true;
				break;
			}
		}
	}

	return is_dig_stream;
}

/* Update DIG link encoder resource tracking variables in dc_state. */
static void update_link_enc_assignment(
		struct dc_state *state,
		struct dc_stream_state *stream,
		enum engine_id eng_id,
		bool add_enc)
{
	int eng_idx;
	int stream_idx;
	int i;

	if (eng_id != ENGINE_ID_UNKNOWN) {
		eng_idx = eng_id - ENGINE_ID_DIGA;
		stream_idx = -1;

		/* Index of stream in dc_state used to update correct entry in
		 * link_enc_assignments table.
		 */
		for (i = 0; i < state->stream_count; i++) {
			if (stream == state->streams[i]) {
				stream_idx = i;
				break;
			}
		}

		/* Update link encoder assignments table, link encoder availability
		 * pool and link encoder assigned to stream in state.
		 * Add/remove encoder resource to/from stream.
		 */
		if (stream_idx != -1) {
			if (add_enc) {
				state->res_ctx.link_enc_assignments[stream_idx] = (struct link_enc_assignment){
					.valid = true,
					.ep_id = (struct display_endpoint_id) {
						.link_id = stream->link->link_id,
						.ep_type = stream->link->ep_type},
					.eng_id = eng_id};
				state->res_ctx.link_enc_avail[eng_idx] = ENGINE_ID_UNKNOWN;
				stream->link_enc = stream->ctx->dc->res_pool->link_encoders[eng_idx];
			} else {
				state->res_ctx.link_enc_assignments[stream_idx].valid = false;
				state->res_ctx.link_enc_avail[eng_idx] = eng_id;
				stream->link_enc = NULL;
			}
		} else {
			dm_output_to_console("%s: Stream not found in dc_state.\n", __func__);
		}
	}
}

/* Return first available DIG link encoder. */
static enum engine_id find_first_avail_link_enc(
		const struct dc_context *ctx,
		const struct dc_state *state)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;
	int i;

	for (i = 0; i < ctx->dc->res_pool->res_cap->num_dig_link_enc; i++) {
		eng_id = state->res_ctx.link_enc_avail[i];
		if (eng_id != ENGINE_ID_UNKNOWN)
			break;
	}

	return eng_id;
}

/* Return stream using DIG link encoder resource. NULL if unused. */
static struct dc_stream_state *get_stream_using_link_enc(
		struct dc_state *state,
		enum engine_id eng_id)
{
	struct dc_stream_state *stream = NULL;
	int stream_idx = -1;
	int i;

	for (i = 0; i < state->stream_count; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_assignments[i];

		if (assignment.valid && (assignment.eng_id == eng_id)) {
			stream_idx = i;
			break;
		}
	}

	if (stream_idx != -1)
		stream = state->streams[stream_idx];
	else
		dm_output_to_console("%s: No stream using DIG(%d).\n", __func__, eng_id);

	return stream;
}

void link_enc_cfg_init(
		struct dc *dc,
		struct dc_state *state)
{
	int i;

	for (i = 0; i < dc->res_pool->res_cap->num_dig_link_enc; i++) {
		if (dc->res_pool->link_encoders[i])
			state->res_ctx.link_enc_avail[i] = (enum engine_id) i;
		else
			state->res_ctx.link_enc_avail[i] = ENGINE_ID_UNKNOWN;
	}
}

void link_enc_cfg_link_encs_assign(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *streams[],
		uint8_t stream_count)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;
	int i;

	/* Release DIG link encoder resources before running assignment algorithm. */
	for (i = 0; i < stream_count; i++)
		dc->res_pool->funcs->link_enc_unassign(state, streams[i]);

	/* (a) Assign DIG link encoders to physical (unmappable) endpoints first. */
	for (i = 0; i < stream_count; i++) {
		struct dc_stream_state *stream = streams[i];

		/* Skip stream if not supported by DIG link encoder. */
		if (!is_dig_link_enc_stream(stream))
			continue;

		/* Physical endpoints have a fixed mapping to DIG link encoders. */
		if (!stream->link->is_dig_mapping_flexible) {
			eng_id = stream->link->eng_id;
			update_link_enc_assignment(state, stream, eng_id, true);
		}
	}

	/* (b) Then assign encoders to mappable endpoints. */
	eng_id = ENGINE_ID_UNKNOWN;

	for (i = 0; i < stream_count; i++) {
		struct dc_stream_state *stream = streams[i];

		/* Skip stream if not supported by DIG link encoder. */
		if (!is_dig_link_enc_stream(stream))
			continue;

		/* Mappable endpoints have a flexible mapping to DIG link encoders. */
		if (stream->link->is_dig_mapping_flexible) {
			eng_id = find_first_avail_link_enc(stream->ctx, state);
			update_link_enc_assignment(state, stream, eng_id, true);
		}
	}
}

void link_enc_cfg_link_enc_unassign(
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;

	/* Only DIG link encoders. */
	if (!is_dig_link_enc_stream(stream))
		return;

	if (stream->link_enc)
		eng_id = stream->link_enc->preferred_engine;

	update_link_enc_assignment(state, stream, eng_id, false);
}

bool link_enc_cfg_is_transmitter_mappable(
		struct dc_state *state,
		struct link_encoder *link_enc)
{
	bool is_mappable = false;
	enum engine_id eng_id = link_enc->preferred_engine;
	struct dc_stream_state *stream = get_stream_using_link_enc(state, eng_id);

	if (stream)
		is_mappable = stream->link->is_dig_mapping_flexible;

	return is_mappable;
}

struct dc_link *link_enc_cfg_get_link_using_link_enc(
		struct dc_state *state,
		enum engine_id eng_id)
{
	struct dc_link *link = NULL;
	int stream_idx = -1;
	int i;

	for (i = 0; i < state->stream_count; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_assignments[i];

		if (assignment.valid && (assignment.eng_id == eng_id)) {
			stream_idx = i;
			break;
		}
	}

	if (stream_idx != -1)
		link = state->streams[stream_idx]->link;
	else
		dm_output_to_console("%s: No link using DIG(%d).\n", __func__, eng_id);

	return link;
}

struct link_encoder *link_enc_cfg_get_link_enc_used_by_link(
		struct dc_state *state,
		const struct dc_link *link)
{
	struct link_encoder *link_enc = NULL;
	struct display_endpoint_id ep_id;
	int stream_idx = -1;
	int i;

	ep_id = (struct display_endpoint_id) {
		.link_id = link->link_id,
		.ep_type = link->ep_type};

	for (i = 0; i < state->stream_count; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_assignments[i];

		if (assignment.valid &&
				assignment.ep_id.link_id.id == ep_id.link_id.id &&
				assignment.ep_id.link_id.enum_id == ep_id.link_id.enum_id &&
				assignment.ep_id.link_id.type == ep_id.link_id.type &&
				assignment.ep_id.ep_type == ep_id.ep_type) {
			stream_idx = i;
			break;
		}
	}

	if (stream_idx != -1)
		link_enc = state->streams[stream_idx]->link_enc;

	return link_enc;
}

struct link_encoder *link_enc_cfg_get_next_avail_link_enc(
	const struct dc *dc,
	const struct dc_state *state)
{
	struct link_encoder *link_enc = NULL;
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;

	eng_id = find_first_avail_link_enc(dc->ctx, state);
	if (eng_id != ENGINE_ID_UNKNOWN)
		link_enc = dc->res_pool->link_encoders[eng_id - ENGINE_ID_DIGA];

	return link_enc;
}
