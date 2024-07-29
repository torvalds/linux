/* Copyright 2021 Advanced Micro Devices, Inc. All rights reserved.
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
#include "link.h"

#define DC_LOGGER dc->ctx->logger

/* Check whether stream is supported by DIG link encoders. */
static bool is_dig_link_enc_stream(struct dc_stream_state *stream)
{
	bool is_dig_stream = false;
	struct link_encoder *link_enc = NULL;
	int i;

	/* Loop over created link encoder objects. */
	if (stream) {
		for (i = 0; i < stream->ctx->dc->res_pool->res_cap->num_dig_link_enc; i++) {
			link_enc = stream->ctx->dc->res_pool->link_encoders[i];

			/* Need to check link signal type rather than stream signal type which may not
			 * yet match.
			 */
			if (link_enc && ((uint32_t)stream->link->connector_signal & link_enc->output_signals)) {
				if (dc_is_dp_signal(stream->signal)) {
					/* DIGs do not support DP2.0 streams with 128b/132b encoding. */
					struct dc_link_settings link_settings = {0};

					stream->ctx->dc->link_srv->dp_decide_link_settings(stream, &link_settings);
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
	}
	return is_dig_stream;
}

static struct link_enc_assignment get_assignment(struct dc *dc, int i)
{
	struct link_enc_assignment assignment;

	if (dc->current_state->res_ctx.link_enc_cfg_ctx.mode == LINK_ENC_CFG_TRANSIENT)
		assignment = dc->current_state->res_ctx.link_enc_cfg_ctx.transient_assignments[i];
	else /* LINK_ENC_CFG_STEADY */
		assignment = dc->current_state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

	return assignment;
}

/* Return stream using DIG link encoder resource. NULL if unused. */
static struct dc_stream_state *get_stream_using_link_enc(
		struct dc_state *state,
		enum engine_id eng_id)
{
	struct dc_stream_state *stream = NULL;
	int i;

	for (i = 0; i < state->stream_count; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if ((assignment.valid == true) && (assignment.eng_id == eng_id)) {
			stream = state->streams[i];
			break;
		}
	}

	return stream;
}

static void remove_link_enc_assignment(
		struct dc_state *state,
		struct dc_stream_state *stream,
		enum engine_id eng_id)
{
	int eng_idx;
	int i;

	if (eng_id != ENGINE_ID_UNKNOWN) {
		eng_idx = eng_id - ENGINE_ID_DIGA;

		/* stream ptr of stream in dc_state used to update correct entry in
		 * link_enc_assignments table.
		 */
		for (i = 0; i < MAX_PIPES; i++) {
			struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

			if (assignment.valid && assignment.stream == stream) {
				state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].valid = false;
				/* Only add link encoder back to availability pool if not being
				 * used by any other stream (i.e. removing SST stream or last MST stream).
				 */
				if (get_stream_using_link_enc(state, eng_id) == NULL)
					state->res_ctx.link_enc_cfg_ctx.link_enc_avail[eng_idx] = eng_id;

				stream->link_enc = NULL;
				state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].eng_id = ENGINE_ID_UNKNOWN;
				state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].stream = NULL;
				dc_stream_release(stream);
				break;
			}
		}
	}
}

static void add_link_enc_assignment(
		struct dc_state *state,
		struct dc_stream_state *stream,
		enum engine_id eng_id)
{
	int eng_idx;
	int i;

	if (eng_id != ENGINE_ID_UNKNOWN) {
		eng_idx = eng_id - ENGINE_ID_DIGA;

		/* stream ptr of stream in dc_state used to update correct entry in
		 * link_enc_assignments table.
		 */
		for (i = 0; i < state->stream_count; i++) {
			if (stream == state->streams[i]) {
				state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i] = (struct link_enc_assignment){
					.valid = true,
					.ep_id = (struct display_endpoint_id) {
						.link_id = stream->link->link_id,
						.ep_type = stream->link->ep_type},
					.eng_id = eng_id,
					.stream = stream};
				dc_stream_retain(stream);
				state->res_ctx.link_enc_cfg_ctx.link_enc_avail[eng_idx] = ENGINE_ID_UNKNOWN;
				stream->link_enc = stream->ctx->dc->res_pool->link_encoders[eng_idx];
				break;
			}
		}

		/* Attempted to add an encoder assignment for a stream not in dc_state. */
		ASSERT(i != state->stream_count);
	}
}

/* Return first available DIG link encoder. */
static enum engine_id find_first_avail_link_enc(
		const struct dc_context *ctx,
		const struct dc_state *state,
		enum engine_id eng_id_requested)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;
	int i;

	if (eng_id_requested != ENGINE_ID_UNKNOWN) {

		for (i = 0; i < ctx->dc->res_pool->res_cap->num_dig_link_enc; i++) {
			eng_id = state->res_ctx.link_enc_cfg_ctx.link_enc_avail[i];
			if (eng_id == eng_id_requested)
				return eng_id;
		}
	}

	eng_id = ENGINE_ID_UNKNOWN;

	for (i = 0; i < ctx->dc->res_pool->res_cap->num_dig_link_enc; i++) {
		eng_id = state->res_ctx.link_enc_cfg_ctx.link_enc_avail[i];
		if (eng_id != ENGINE_ID_UNKNOWN)
			break;
	}

	return eng_id;
}

/* Check for availability of link encoder eng_id. */
static bool is_avail_link_enc(struct dc_state *state, enum engine_id eng_id, struct dc_stream_state *stream)
{
	bool is_avail = false;
	int eng_idx = eng_id - ENGINE_ID_DIGA;

	/* An encoder is available if it is still in the availability pool. */
	if (eng_id != ENGINE_ID_UNKNOWN && state->res_ctx.link_enc_cfg_ctx.link_enc_avail[eng_idx] != ENGINE_ID_UNKNOWN) {
		is_avail = true;
	} else {
		struct dc_stream_state *stream_assigned = NULL;

		/* MST streams share the same link and should share the same encoder.
		 * If a stream that has already been assigned a link encoder uses as the
		 * same link as the stream checking for availability, it is an MST stream
		 * and should use the same link encoder.
		 */
		stream_assigned = get_stream_using_link_enc(state, eng_id);
		if (stream_assigned && stream != stream_assigned && stream->link == stream_assigned->link)
			is_avail = true;
	}

	return is_avail;
}

/* Test for display_endpoint_id equality. */
static bool are_ep_ids_equal(struct display_endpoint_id *lhs, struct display_endpoint_id *rhs)
{
	bool are_equal = false;

	if (lhs->link_id.id == rhs->link_id.id &&
			lhs->link_id.enum_id == rhs->link_id.enum_id &&
			lhs->link_id.type == rhs->link_id.type &&
			lhs->ep_type == rhs->ep_type)
		are_equal = true;

	return are_equal;
}

static struct link_encoder *get_link_enc_used_by_link(
		struct dc_state *state,
		const struct dc_link *link)
{
	struct link_encoder *link_enc = NULL;
	struct display_endpoint_id ep_id;
	int i;

	ep_id = (struct display_endpoint_id) {
		.link_id = link->link_id,
		.ep_type = link->ep_type};

	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];
		if (assignment.eng_id == ENGINE_ID_UNKNOWN)
			continue;

		if (assignment.valid == true && are_ep_ids_equal(&assignment.ep_id, &ep_id))
			link_enc = link->dc->res_pool->link_encoders[assignment.eng_id - ENGINE_ID_DIGA];
	}

	return link_enc;
}
/* Clear all link encoder assignments. */
static void clear_enc_assignments(const struct dc *dc, struct dc_state *state)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].valid = false;
		state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].eng_id = ENGINE_ID_UNKNOWN;
		if (state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].stream != NULL) {
			dc_stream_release(state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].stream);
			state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].stream = NULL;
		}
	}

	for (i = 0; i < dc->res_pool->res_cap->num_dig_link_enc; i++) {
		if (dc->res_pool->link_encoders[i])
			state->res_ctx.link_enc_cfg_ctx.link_enc_avail[i] = (enum engine_id) i;
		else
			state->res_ctx.link_enc_cfg_ctx.link_enc_avail[i] = ENGINE_ID_UNKNOWN;
	}
}

void link_enc_cfg_init(
		const struct dc *dc,
		struct dc_state *state)
{
	clear_enc_assignments(dc, state);

	state->res_ctx.link_enc_cfg_ctx.mode = LINK_ENC_CFG_STEADY;
}

void link_enc_cfg_copy(const struct dc_state *src_ctx, struct dc_state *dst_ctx)
{
	memcpy(&dst_ctx->res_ctx.link_enc_cfg_ctx,
	       &src_ctx->res_ctx.link_enc_cfg_ctx,
	       sizeof(dst_ctx->res_ctx.link_enc_cfg_ctx));
}

void link_enc_cfg_link_encs_assign(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *streams[],
		uint8_t stream_count)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN, eng_id_req = ENGINE_ID_UNKNOWN;
	int i;
	int j;

	ASSERT(state->stream_count == stream_count);
	ASSERT(dc->current_state->res_ctx.link_enc_cfg_ctx.mode == LINK_ENC_CFG_STEADY);

	/* Release DIG link encoder resources before running assignment algorithm. */
	for (i = 0; i < dc->current_state->stream_count; i++)
		dc->res_pool->funcs->link_enc_unassign(state, dc->current_state->streams[i]);

	for (i = 0; i < MAX_PIPES; i++)
		ASSERT(state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i].valid == false);

	/* (a) Assign DIG link encoders to physical (unmappable) endpoints first. */
	for (i = 0; i < stream_count; i++) {
		struct dc_stream_state *stream = streams[i];

		/* skip it if the link is mappable endpoint. */
		if (stream->link->is_dig_mapping_flexible)
			continue;

		/* Skip stream if not supported by DIG link encoder. */
		if (!is_dig_link_enc_stream(stream))
			continue;

		/* Physical endpoints have a fixed mapping to DIG link encoders. */
		eng_id = stream->link->eng_id;
		add_link_enc_assignment(state, stream, eng_id);
	}

	/* (b) Retain previous assignments for mappable endpoints if encoders still available. */
	eng_id = ENGINE_ID_UNKNOWN;

	if (state != dc->current_state) {
		struct dc_state *prev_state = dc->current_state;

		for (i = 0; i < stream_count; i++) {
			struct dc_stream_state *stream = state->streams[i];

			/* Skip it if the link is NOT mappable endpoint. */
			if (!stream->link->is_dig_mapping_flexible)
				continue;

			/* Skip stream if not supported by DIG link encoder. */
			if (!is_dig_link_enc_stream(stream))
				continue;

			for (j = 0; j < prev_state->stream_count; j++) {
				struct dc_stream_state *prev_stream = prev_state->streams[j];

				if (stream == prev_stream && stream->link == prev_stream->link &&
						prev_state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[j].valid) {
					eng_id = prev_state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[j].eng_id;

					if (is_avail_link_enc(state, eng_id, stream))
						add_link_enc_assignment(state, stream, eng_id);
				}
			}
		}
	}

	/* (c) Then assign encoders to remaining mappable endpoints. */
	eng_id = ENGINE_ID_UNKNOWN;

	for (i = 0; i < stream_count; i++) {
		struct dc_stream_state *stream = streams[i];
		struct link_encoder *link_enc = NULL;

		/* Skip it if the link is NOT mappable endpoint. */
		if (!stream->link->is_dig_mapping_flexible)
			continue;

		/* Skip if encoder assignment retained in step (b) above. */
		if (stream->link_enc)
			continue;

		/* Skip stream if not supported by DIG link encoder. */
		if (!is_dig_link_enc_stream(stream)) {
			ASSERT(stream->link->is_dig_mapping_flexible != true);
			continue;
		}

		/* Mappable endpoints have a flexible mapping to DIG link encoders. */

		/* For MST, multiple streams will share the same link / display
		 * endpoint. These streams should use the same link encoder
		 * assigned to that endpoint.
		 */
		link_enc = get_link_enc_used_by_link(state, stream->link);
		if (link_enc == NULL) {

			if (stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA &&
					stream->link->dpia_preferred_eng_id != ENGINE_ID_UNKNOWN)
				eng_id_req = stream->link->dpia_preferred_eng_id;

			eng_id = find_first_avail_link_enc(stream->ctx, state, eng_id_req);
		}
		else
			eng_id =  link_enc->preferred_engine;

		add_link_enc_assignment(state, stream, eng_id);
	}

	link_enc_cfg_validate(dc, state);

	/* Update transient assignments. */
	for (i = 0; i < MAX_PIPES; i++) {
		dc->current_state->res_ctx.link_enc_cfg_ctx.transient_assignments[i] =
			state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];
	}

	/* Log encoder assignments. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment =
				dc->current_state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.valid)
			DC_LOG_DEBUG("%s: CUR %s(%d) - enc_id(%d)\n",
					__func__,
					assignment.ep_id.ep_type == DISPLAY_ENDPOINT_PHY ? "PHY" : "DPIA",
					assignment.ep_id.ep_type == DISPLAY_ENDPOINT_PHY ?
							assignment.ep_id.link_id.enum_id :
							assignment.ep_id.link_id.enum_id - 1,
					assignment.eng_id);
	}
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment =
				state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.valid)
			DC_LOG_DEBUG("%s: NEW %s(%d) - enc_id(%d)\n",
					__func__,
					assignment.ep_id.ep_type == DISPLAY_ENDPOINT_PHY ? "PHY" : "DPIA",
					assignment.ep_id.ep_type == DISPLAY_ENDPOINT_PHY ?
							assignment.ep_id.link_id.enum_id :
							assignment.ep_id.link_id.enum_id - 1,
					assignment.eng_id);
	}

	/* Current state mode will be set to steady once this state committed. */
	state->res_ctx.link_enc_cfg_ctx.mode = LINK_ENC_CFG_STEADY;
}

void link_enc_cfg_link_enc_unassign(
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	enum engine_id eng_id = ENGINE_ID_UNKNOWN;

	if (stream->link_enc)
		eng_id = stream->link_enc->preferred_engine;

	remove_link_enc_assignment(state, stream, eng_id);
}

bool link_enc_cfg_is_transmitter_mappable(
		struct dc *dc,
		struct link_encoder *link_enc)
{
	bool is_mappable = false;
	enum engine_id eng_id = link_enc->preferred_engine;
	struct dc_stream_state *stream = link_enc_cfg_get_stream_using_link_enc(dc, eng_id);

	if (stream)
		is_mappable = stream->link->is_dig_mapping_flexible;

	return is_mappable;
}

struct dc_stream_state *link_enc_cfg_get_stream_using_link_enc(
		struct dc *dc,
		enum engine_id eng_id)
{
	struct dc_stream_state *stream = NULL;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = get_assignment(dc, i);

		if ((assignment.valid == true) && (assignment.eng_id == eng_id)) {
			stream = assignment.stream;
			break;
		}
	}

	return stream;
}

struct dc_link *link_enc_cfg_get_link_using_link_enc(
		struct dc *dc,
		enum engine_id eng_id)
{
	struct dc_link *link = NULL;
	struct dc_stream_state *stream = NULL;

	stream = link_enc_cfg_get_stream_using_link_enc(dc, eng_id);

	if (stream)
		link = stream->link;

	return link;
}

struct link_encoder *link_enc_cfg_get_link_enc_used_by_link(
		struct dc *dc,
		const struct dc_link *link)
{
	struct link_encoder *link_enc = NULL;
	struct display_endpoint_id ep_id;
	int i;

	ep_id = (struct display_endpoint_id) {
		.link_id = link->link_id,
		.ep_type = link->ep_type};

	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = get_assignment(dc, i);
		if (assignment.eng_id == ENGINE_ID_UNKNOWN)
			continue;

		if (assignment.valid == true && are_ep_ids_equal(&assignment.ep_id, &ep_id)) {
			link_enc = link->dc->res_pool->link_encoders[assignment.eng_id - ENGINE_ID_DIGA];
			break;
		}
	}

	return link_enc;
}

struct link_encoder *link_enc_cfg_get_next_avail_link_enc(struct dc *dc)
{
	struct link_encoder *link_enc = NULL;
	enum engine_id encs_assigned[MAX_DIG_LINK_ENCODERS];
	int i;

	for (i = 0; i < MAX_DIG_LINK_ENCODERS; i++)
		encs_assigned[i] = ENGINE_ID_UNKNOWN;

	/* Add assigned encoders to list. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = get_assignment(dc, i);

		if (assignment.valid && assignment.eng_id != ENGINE_ID_UNKNOWN)
			encs_assigned[assignment.eng_id - ENGINE_ID_DIGA] = assignment.eng_id;
	}

	for (i = 0; i < dc->res_pool->res_cap->num_dig_link_enc; i++) {
		if (encs_assigned[i] == ENGINE_ID_UNKNOWN &&
				dc->res_pool->link_encoders[i] != NULL) {
			link_enc = dc->res_pool->link_encoders[i];
			break;
		}
	}

	return link_enc;
}

struct link_encoder *link_enc_cfg_get_link_enc_used_by_stream(
		struct dc *dc,
		const struct dc_stream_state *stream)
{
	struct link_encoder *link_enc;

	link_enc = link_enc_cfg_get_link_enc_used_by_link(dc, stream->link);

	return link_enc;
}

struct link_encoder *link_enc_cfg_get_link_enc(
		const struct dc_link *link)
{
	struct link_encoder *link_enc = NULL;

	/* Links supporting dynamically assigned link encoder will be assigned next
	 * available encoder if one not already assigned.
	 */
	if (link->is_dig_mapping_flexible &&
	    link->dc->res_pool->funcs->link_encs_assign) {
		link_enc = link_enc_cfg_get_link_enc_used_by_link(link->ctx->dc, link);
		if (link_enc == NULL)
			link_enc = link_enc_cfg_get_next_avail_link_enc(
				link->ctx->dc);
	} else
		link_enc = link->link_enc;

	return link_enc;
}

struct link_encoder *link_enc_cfg_get_link_enc_used_by_stream_current(
		struct dc *dc,
		const struct dc_stream_state *stream)
{
	struct link_encoder *link_enc = NULL;
	struct display_endpoint_id ep_id;
	int i;

	ep_id = (struct display_endpoint_id) {
		.link_id = stream->link->link_id,
		.ep_type = stream->link->ep_type};

	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment =
			dc->current_state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.eng_id == ENGINE_ID_UNKNOWN)
			continue;

		if (assignment.valid == true && are_ep_ids_equal(&assignment.ep_id, &ep_id)) {
			link_enc = stream->link->dc->res_pool->link_encoders[assignment.eng_id - ENGINE_ID_DIGA];
			break;
		}
	}

	return link_enc;
}

bool link_enc_cfg_is_link_enc_avail(struct dc *dc, enum engine_id eng_id, struct dc_link *link)
{
	bool is_avail = true;
	int i;

	/* An encoder is not available if it has already been assigned to a different endpoint. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = get_assignment(dc, i);
		struct display_endpoint_id ep_id = (struct display_endpoint_id) {
				.link_id = link->link_id,
				.ep_type = link->ep_type};

		if (assignment.valid && assignment.eng_id == eng_id && !are_ep_ids_equal(&ep_id, &assignment.ep_id)) {
			is_avail = false;
			break;
		}
	}

	return is_avail;
}

bool link_enc_cfg_validate(struct dc *dc, struct dc_state *state)
{
	bool is_valid = false;
	bool valid_entries = true;
	bool valid_stream_ptrs = true;
	bool valid_uniqueness = true;
	bool valid_avail = true;
	bool valid_streams = true;
	int i, j;
	uint8_t valid_count = 0;
	uint8_t dig_stream_count = 0;
	int eng_ids_per_ep_id[MAX_PIPES] = {0};
	int ep_ids_per_eng_id[MAX_PIPES] = {0};
	int valid_bitmap = 0;

	/* (1) No. valid entries same as stream count. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.valid)
			valid_count++;

		if (is_dig_link_enc_stream(state->streams[i]))
			dig_stream_count++;
	}
	if (valid_count != dig_stream_count)
		valid_entries = false;

	/* (2) Matching stream ptrs. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.valid) {
			if (assignment.stream != state->streams[i])
				valid_stream_ptrs = false;
		}
	}

	/* (3) Each endpoint assigned unique encoder. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment_i = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment_i.valid) {
			struct display_endpoint_id ep_id_i = assignment_i.ep_id;

			eng_ids_per_ep_id[i]++;
			ep_ids_per_eng_id[i]++;
			for (j = 0; j < MAX_PIPES; j++) {
				struct link_enc_assignment assignment_j =
					state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[j];

				if (j == i)
					continue;

				if (assignment_j.valid) {
					struct display_endpoint_id ep_id_j = assignment_j.ep_id;

					if (are_ep_ids_equal(&ep_id_i, &ep_id_j) &&
							assignment_i.eng_id != assignment_j.eng_id) {
						valid_uniqueness = false;
						eng_ids_per_ep_id[i]++;
					} else if (!are_ep_ids_equal(&ep_id_i, &ep_id_j) &&
							assignment_i.eng_id == assignment_j.eng_id) {
						valid_uniqueness = false;
						ep_ids_per_eng_id[i]++;
					}
				}
			}
		}
	}

	/* (4) Assigned encoders not in available pool. */
	for (i = 0; i < MAX_PIPES; i++) {
		struct link_enc_assignment assignment = state->res_ctx.link_enc_cfg_ctx.link_enc_assignments[i];

		if (assignment.valid) {
			for (j = 0; j < dc->res_pool->res_cap->num_dig_link_enc; j++) {
				if (state->res_ctx.link_enc_cfg_ctx.link_enc_avail[j] == assignment.eng_id) {
					valid_avail = false;
					break;
				}
			}
		}
	}

	/* (5) All streams have valid link encoders. */
	for (i = 0; i < state->stream_count; i++) {
		struct dc_stream_state *stream = state->streams[i];

		if (is_dig_link_enc_stream(stream) && stream->link_enc == NULL) {
			valid_streams = false;
			break;
		}
	}

	is_valid = valid_entries && valid_stream_ptrs && valid_uniqueness && valid_avail && valid_streams;
	ASSERT(is_valid);

	if (is_valid == false) {
		valid_bitmap =
			(valid_entries & 0x1) |
			((valid_stream_ptrs & 0x1) << 1) |
			((valid_uniqueness & 0x1) << 2) |
			((valid_avail & 0x1) << 3) |
			((valid_streams & 0x1) << 4);
		DC_LOG_ERROR("%s: Invalid link encoder assignments - 0x%x\n", __func__, valid_bitmap);
	}

	return is_valid;
}

void link_enc_cfg_set_transient_mode(struct dc *dc, struct dc_state *current_state, struct dc_state *new_state)
{
	int i = 0;
	int num_transient_assignments = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		if (current_state->res_ctx.link_enc_cfg_ctx.transient_assignments[i].valid)
			num_transient_assignments++;
	}

	/* Only enter transient mode if the new encoder assignments are valid. */
	if (new_state->stream_count == num_transient_assignments) {
		current_state->res_ctx.link_enc_cfg_ctx.mode = LINK_ENC_CFG_TRANSIENT;
		DC_LOG_DEBUG("%s: current_state(%p) mode(%d)\n", __func__, current_state, LINK_ENC_CFG_TRANSIENT);
	}
}
