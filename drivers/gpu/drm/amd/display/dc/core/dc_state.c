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
#include "core_types.h"
#include "core_status.h"
#include "dc_state.h"
#include "dc_state_priv.h"
#include "dc_stream_priv.h"
#include "dc_plane_priv.h"

#include "dm_services.h"
#include "resource.h"
#include "link_enc_cfg.h"

#include "dml2/dml2_wrapper.h"
#include "dml2/dml2_internal_types.h"

#define DC_LOGGER \
	dc->ctx->logger
#define DC_LOGGER_INIT(logger)

/* Private dc_state helper functions */
static bool dc_state_track_phantom_stream(struct dc_state *state,
		struct dc_stream_state *phantom_stream)
{
	if (state->phantom_stream_count >= MAX_PHANTOM_PIPES)
		return false;

	state->phantom_streams[state->phantom_stream_count++] = phantom_stream;

	return true;
}

static bool dc_state_untrack_phantom_stream(struct dc_state *state, struct dc_stream_state *phantom_stream)
{
	bool res = false;
	int i;

	/* first find phantom stream in the dc_state */
	for (i = 0; i < state->phantom_stream_count; i++) {
		if (state->phantom_streams[i] == phantom_stream) {
			state->phantom_streams[i] = NULL;
			res = true;
			break;
		}
	}

	/* failed to find stream in state */
	if (!res)
		return res;

	/* trim back phantom streams */
	state->phantom_stream_count--;
	for (; i < state->phantom_stream_count; i++)
		state->phantom_streams[i] = state->phantom_streams[i + 1];

	return res;
}

static bool dc_state_is_phantom_stream_tracked(struct dc_state *state, struct dc_stream_state *phantom_stream)
{
	int i;

	for (i = 0; i < state->phantom_stream_count; i++) {
		if (state->phantom_streams[i] == phantom_stream)
			return true;
	}

	return false;
}

static bool dc_state_track_phantom_plane(struct dc_state *state,
		struct dc_plane_state *phantom_plane)
{
	if (state->phantom_plane_count >= MAX_PHANTOM_PIPES)
		return false;

	state->phantom_planes[state->phantom_plane_count++] = phantom_plane;

	return true;
}

static bool dc_state_untrack_phantom_plane(struct dc_state *state, struct dc_plane_state *phantom_plane)
{
	bool res = false;
	int i;

	/* first find phantom plane in the dc_state */
	for (i = 0; i < state->phantom_plane_count; i++) {
		if (state->phantom_planes[i] == phantom_plane) {
			state->phantom_planes[i] = NULL;
			res = true;
			break;
		}
	}

	/* failed to find plane in state */
	if (!res)
		return res;

	/* trim back phantom planes */
	state->phantom_plane_count--;
	for (; i < state->phantom_plane_count; i++)
		state->phantom_planes[i] = state->phantom_planes[i + 1];

	return res;
}

static bool dc_state_is_phantom_plane_tracked(struct dc_state *state, struct dc_plane_state *phantom_plane)
{
	int i;

	for (i = 0; i < state->phantom_plane_count; i++) {
		if (state->phantom_planes[i] == phantom_plane)
			return true;
	}

	return false;
}

static void dc_state_copy_internal(struct dc_state *dst_state, struct dc_state *src_state)
{
	int i, j;

	memcpy(dst_state, src_state, sizeof(struct dc_state));

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *cur_pipe = &dst_state->res_ctx.pipe_ctx[i];

		if (cur_pipe->top_pipe)
			cur_pipe->top_pipe =  &dst_state->res_ctx.pipe_ctx[cur_pipe->top_pipe->pipe_idx];

		if (cur_pipe->bottom_pipe)
			cur_pipe->bottom_pipe = &dst_state->res_ctx.pipe_ctx[cur_pipe->bottom_pipe->pipe_idx];

		if (cur_pipe->prev_odm_pipe)
			cur_pipe->prev_odm_pipe =  &dst_state->res_ctx.pipe_ctx[cur_pipe->prev_odm_pipe->pipe_idx];

		if (cur_pipe->next_odm_pipe)
			cur_pipe->next_odm_pipe = &dst_state->res_ctx.pipe_ctx[cur_pipe->next_odm_pipe->pipe_idx];
	}

	/* retain phantoms */
	for (i = 0; i < dst_state->phantom_stream_count; i++)
		dc_stream_retain(dst_state->phantom_streams[i]);

	for (i = 0; i < dst_state->phantom_plane_count; i++)
		dc_plane_state_retain(dst_state->phantom_planes[i]);

	/* retain streams and planes */
	for (i = 0; i < dst_state->stream_count; i++) {
		dc_stream_retain(dst_state->streams[i]);
		for (j = 0; j < dst_state->stream_status[i].plane_count; j++)
			dc_plane_state_retain(
					dst_state->stream_status[i].plane_states[j]);
	}

}

static void init_state(struct dc *dc, struct dc_state *state)
{
	/* Each context must have their own instance of VBA and in order to
	 * initialize and obtain IP and SOC the base DML instance from DC is
	 * initially copied into every context
	 */
	memcpy(&state->bw_ctx.dml, &dc->dml, sizeof(struct display_mode_lib));
}

/* Public dc_state functions */
struct dc_state *dc_state_create(struct dc *dc)
{
	struct dc_state *state = kvzalloc(sizeof(struct dc_state),
			GFP_KERNEL);

	if (!state)
		return NULL;

	init_state(dc, state);
	dc_state_construct(dc, state);

#ifdef CONFIG_DRM_AMD_DC_FP
	if (dc->debug.using_dml2)
		dml2_create(dc, &dc->dml2_options, &state->bw_ctx.dml2);
#endif

	kref_init(&state->refcount);

	return state;
}

void dc_state_copy(struct dc_state *dst_state, struct dc_state *src_state)
{
	struct kref refcount = dst_state->refcount;
#ifdef CONFIG_DRM_AMD_DC_FP
	struct dml2_context *dst_dml2 = dst_state->bw_ctx.dml2;
#endif

	dc_state_copy_internal(dst_state, src_state);

#ifdef CONFIG_DRM_AMD_DC_FP
	dst_state->bw_ctx.dml2 = dst_dml2;
	if (src_state->bw_ctx.dml2)
		dml2_copy(dst_state->bw_ctx.dml2, src_state->bw_ctx.dml2);
#endif

	/* context refcount should not be overridden */
	dst_state->refcount = refcount;
}

struct dc_state *dc_state_create_copy(struct dc_state *src_state)
{
	struct dc_state *new_state;

	new_state = kvmalloc(sizeof(struct dc_state),
			GFP_KERNEL);
	if (!new_state)
		return NULL;

	dc_state_copy_internal(new_state, src_state);

#ifdef CONFIG_DRM_AMD_DC_FP
	if (src_state->bw_ctx.dml2 &&
			!dml2_create_copy(&new_state->bw_ctx.dml2, src_state->bw_ctx.dml2)) {
		dc_state_release(new_state);
		return NULL;
	}
#endif

	kref_init(&new_state->refcount);

	return new_state;
}

void dc_state_copy_current(struct dc *dc, struct dc_state *dst_state)
{
	dc_state_copy(dst_state, dc->current_state);
}

struct dc_state *dc_state_create_current_copy(struct dc *dc)
{
	return dc_state_create_copy(dc->current_state);
}

void dc_state_construct(struct dc *dc, struct dc_state *state)
{
	state->clk_mgr = dc->clk_mgr;

	/* Initialise DIG link encoder resource tracking variables. */
	if (dc->res_pool)
		link_enc_cfg_init(dc, state);
}

void dc_state_destruct(struct dc_state *state)
{
	int i, j;

	for (i = 0; i < state->stream_count; i++) {
		for (j = 0; j < state->stream_status[i].plane_count; j++)
			dc_plane_state_release(
					state->stream_status[i].plane_states[j]);

		state->stream_status[i].plane_count = 0;
		dc_stream_release(state->streams[i]);
		state->streams[i] = NULL;
	}
	state->stream_count = 0;

	/* release tracked phantoms */
	for (i = 0; i < state->phantom_stream_count; i++) {
		dc_stream_release(state->phantom_streams[i]);
		state->phantom_streams[i] = NULL;
	}
	state->phantom_stream_count = 0;

	for (i = 0; i < state->phantom_plane_count; i++) {
		dc_plane_state_release(state->phantom_planes[i]);
		state->phantom_planes[i] = NULL;
	}
	state->phantom_plane_count = 0;

	state->stream_mask = 0;
	memset(&state->res_ctx, 0, sizeof(state->res_ctx));
	memset(&state->pp_display_cfg, 0, sizeof(state->pp_display_cfg));
	memset(&state->dcn_bw_vars, 0, sizeof(state->dcn_bw_vars));
	state->clk_mgr = NULL;
	memset(&state->bw_ctx.bw, 0, sizeof(state->bw_ctx.bw));
	memset(state->block_sequence, 0, sizeof(state->block_sequence));
	state->block_sequence_steps = 0;
	memset(state->dc_dmub_cmd, 0, sizeof(state->dc_dmub_cmd));
	state->dmub_cmd_count = 0;
	memset(&state->perf_params, 0, sizeof(state->perf_params));
	memset(&state->scratch, 0, sizeof(state->scratch));
}

void dc_state_retain(struct dc_state *state)
{
	kref_get(&state->refcount);
}

static void dc_state_free(struct kref *kref)
{
	struct dc_state *state = container_of(kref, struct dc_state, refcount);

	dc_state_destruct(state);

#ifdef CONFIG_DRM_AMD_DC_FP
	dml2_destroy(state->bw_ctx.dml2);
	state->bw_ctx.dml2 = 0;
#endif

	kvfree(state);
}

void dc_state_release(struct dc_state *state)
{
	if (state != NULL)
		kref_put(&state->refcount, dc_state_free);
}
/*
 * dc_state_add_stream() - Add a new dc_stream_state to a dc_state.
 */
enum dc_status dc_state_add_stream(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	enum dc_status res;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (state->stream_count >= dc->res_pool->timing_generator_count) {
		DC_LOG_WARNING("Max streams reached, can't add stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	state->streams[state->stream_count] = stream;
	dc_stream_retain(stream);
	state->stream_count++;

	res = resource_add_otg_master_for_stream_output(
			state, dc->res_pool, stream);
	if (res != DC_OK)
		DC_LOG_WARNING("Adding stream %p to context failed with err %d!\n", stream, res);

	return res;
}

/*
 * dc_state_remove_stream() - Remove a stream from a dc_state.
 */
enum dc_status dc_state_remove_stream(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	int i;
	struct pipe_ctx *del_pipe = resource_get_otg_master_for_stream(
			&state->res_ctx, stream);

	if (!del_pipe) {
		dm_error("Pipe not found for stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	resource_update_pipes_for_stream_with_slice_count(state,
			dc->current_state, dc->res_pool, stream, 1);
	resource_remove_otg_master_for_stream_output(
			state, dc->res_pool, stream);

	for (i = 0; i < state->stream_count; i++)
		if (state->streams[i] == stream)
			break;

	if (state->streams[i] != stream) {
		dm_error("Context doesn't have stream %p !\n", stream);
		return DC_ERROR_UNEXPECTED;
	}

	dc_stream_release(state->streams[i]);
	state->stream_count--;

	/* Trim back arrays */
	for (; i < state->stream_count; i++) {
		state->streams[i] = state->streams[i + 1];
		state->stream_status[i] = state->stream_status[i + 1];
	}

	state->streams[state->stream_count] = NULL;
	memset(
			&state->stream_status[state->stream_count],
			0,
			sizeof(state->stream_status[0]));

	return DC_OK;
}

bool dc_state_add_plane(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *state)
{
	struct resource_pool *pool = dc->res_pool;
	struct pipe_ctx *otg_master_pipe;
	struct dc_stream_status *stream_status = NULL;
	bool added = false;

	stream_status = dc_state_get_stream_status(state, stream);
	if (stream_status == NULL) {
		dm_error("Existing stream not found; failed to attach surface!\n");
		goto out;
	} else if (stream_status->plane_count == MAX_SURFACE_NUM) {
		dm_error("Surface: can not attach plane_state %p! Maximum is: %d\n",
				plane_state, MAX_SURFACE_NUM);
		goto out;
	}

	otg_master_pipe = resource_get_otg_master_for_stream(
			&state->res_ctx, stream);
	if (otg_master_pipe)
		added = resource_append_dpp_pipes_for_plane_composition(state,
				dc->current_state, pool, otg_master_pipe, plane_state);

	if (added) {
		stream_status->plane_states[stream_status->plane_count] =
				plane_state;
		stream_status->plane_count++;
		dc_plane_state_retain(plane_state);
	}

out:
	return added;
}

bool dc_state_remove_plane(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *state)
{
	int i;
	struct dc_stream_status *stream_status = NULL;
	struct resource_pool *pool = dc->res_pool;

	if (!plane_state)
		return true;

	for (i = 0; i < state->stream_count; i++)
		if (state->streams[i] == stream) {
			stream_status = &state->stream_status[i];
			break;
		}

	if (stream_status == NULL) {
		dm_error("Existing stream not found; failed to remove plane.\n");
		return false;
	}

	resource_remove_dpp_pipes_for_plane_composition(
			state, pool, plane_state);

	for (i = 0; i < stream_status->plane_count; i++) {
		if (stream_status->plane_states[i] == plane_state) {
			dc_plane_state_release(stream_status->plane_states[i]);
			break;
		}
	}

	if (i == stream_status->plane_count) {
		dm_error("Existing plane_state not found; failed to detach it!\n");
		return false;
	}

	stream_status->plane_count--;

	/* Start at the plane we've just released, and move all the planes one index forward to "trim" the array */
	for (; i < stream_status->plane_count; i++)
		stream_status->plane_states[i] = stream_status->plane_states[i + 1];

	stream_status->plane_states[stream_status->plane_count] = NULL;

	if (stream_status->plane_count == 0 && dc->config.enable_windowed_mpo_odm)
		/* ODM combine could prevent us from supporting more planes
		 * we will reset ODM slice count back to 1 when all planes have
		 * been removed to maximize the amount of planes supported when
		 * new planes are added.
		 */
		resource_update_pipes_for_stream_with_slice_count(
				state, dc->current_state, dc->res_pool, stream, 1);

	return true;
}

/**
 * dc_state_rem_all_planes_for_stream - Remove planes attached to the target stream.
 *
 * @dc: Current dc state.
 * @stream: Target stream, which we want to remove the attached plans.
 * @state: context from which the planes are to be removed.
 *
 * Return:
 * Return true if DC was able to remove all planes from the target
 * stream, otherwise, return false.
 */
bool dc_state_rem_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *state)
{
	int i, old_plane_count;
	struct dc_stream_status *stream_status = NULL;
	struct dc_plane_state *del_planes[MAX_SURFACE_NUM] = { 0 };

	for (i = 0; i < state->stream_count; i++)
		if (state->streams[i] == stream) {
			stream_status = &state->stream_status[i];
			break;
		}

	if (stream_status == NULL) {
		dm_error("Existing stream %p not found!\n", stream);
		return false;
	}

	old_plane_count = stream_status->plane_count;

	for (i = 0; i < old_plane_count; i++)
		del_planes[i] = stream_status->plane_states[i];

	for (i = 0; i < old_plane_count; i++)
		if (!dc_state_remove_plane(dc, stream, del_planes[i], state))
			return false;

	return true;
}

bool dc_state_add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state * const *plane_states,
		int plane_count,
		struct dc_state *state)
{
	int i;
	bool result = true;

	for (i = 0; i < plane_count; i++)
		if (!dc_state_add_plane(dc, stream, plane_states[i], state)) {
			result = false;
			break;
		}

	return result;
}

/* Private dc_state functions */

/**
 * dc_state_get_stream_status - Get stream status from given dc state
 * @state: DC state to find the stream status in
 * @stream: The stream to get the stream status for
 *
 * The given stream is expected to exist in the given dc state. Otherwise, NULL
 * will be returned.
 */
struct dc_stream_status *dc_state_get_stream_status(
		struct dc_state *state,
		struct dc_stream_state *stream)
{
	uint8_t i;

	if (state == NULL)
		return NULL;

	for (i = 0; i < state->stream_count; i++) {
		if (stream == state->streams[i])
			return &state->stream_status[i];
	}

	return NULL;
}

enum mall_stream_type dc_state_get_pipe_subvp_type(const struct dc_state *state,
		const struct pipe_ctx *pipe_ctx)
{
	return dc_state_get_stream_subvp_type(state, pipe_ctx->stream);
}

enum mall_stream_type dc_state_get_stream_subvp_type(const struct dc_state *state,
		const struct dc_stream_state *stream)
{
	int i;

	enum mall_stream_type type = SUBVP_NONE;

	for (i = 0; i < state->stream_count; i++) {
		if (state->streams[i] == stream) {
			type = state->stream_status[i].mall_stream_config.type;
			break;
		}
	}

	return type;
}

struct dc_stream_state *dc_state_get_paired_subvp_stream(const struct dc_state *state,
		const struct dc_stream_state *stream)
{
	int i;

	struct dc_stream_state *paired_stream = NULL;

	for (i = 0; i < state->stream_count; i++) {
		if (state->streams[i] == stream) {
			paired_stream = state->stream_status[i].mall_stream_config.paired_stream;
			break;
		}
	}

	return paired_stream;
}

struct dc_stream_state *dc_state_create_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *main_stream)
{
	struct dc_stream_state *phantom_stream;

	DC_LOGGER_INIT(dc->ctx->logger);

	phantom_stream = dc_create_stream_for_sink(main_stream->sink);

	if (!phantom_stream) {
		DC_LOG_ERROR("Failed to allocate phantom stream.\n");
		return NULL;
	}

	/* track phantom stream in dc_state */
	dc_state_track_phantom_stream(state, phantom_stream);

	phantom_stream->is_phantom = true;
	phantom_stream->signal = SIGNAL_TYPE_VIRTUAL;
	phantom_stream->dpms_off = true;

	return phantom_stream;
}

void dc_state_release_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream)
{
	DC_LOGGER_INIT(dc->ctx->logger);

	if (!dc_state_untrack_phantom_stream(state, phantom_stream)) {
		DC_LOG_ERROR("Failed to free phantom stream %p in dc state %p.\n", phantom_stream, state);
		return;
	}

	dc_stream_release(phantom_stream);
}

struct dc_plane_state *dc_state_create_phantom_plane(struct dc *dc,
		struct dc_state *state,
		struct dc_plane_state *main_plane)
{
	struct dc_plane_state *phantom_plane = dc_create_plane_state(dc);

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!phantom_plane) {
		DC_LOG_ERROR("Failed to allocate phantom plane.\n");
		return NULL;
	}

	/* track phantom inside dc_state */
	dc_state_track_phantom_plane(state, phantom_plane);

	phantom_plane->is_phantom = true;

	return phantom_plane;
}

void dc_state_release_phantom_plane(const struct dc *dc,
		struct dc_state *state,
		struct dc_plane_state *phantom_plane)
{
	DC_LOGGER_INIT(dc->ctx->logger);

	if (!dc_state_untrack_phantom_plane(state, phantom_plane)) {
		DC_LOG_ERROR("Failed to free phantom plane %p in dc state %p.\n", phantom_plane, state);
		return;
	}

	dc_plane_state_release(phantom_plane);
}

/* add phantom streams to context and generate correct meta inside dc_state */
enum dc_status dc_state_add_phantom_stream(struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream,
		struct dc_stream_state *main_stream)
{
	struct dc_stream_status *main_stream_status;
	struct dc_stream_status *phantom_stream_status;
	enum dc_status res = dc_state_add_stream(dc, state, phantom_stream);

	/* check if stream is tracked */
	if (res == DC_OK && !dc_state_is_phantom_stream_tracked(state, phantom_stream)) {
		/* stream must be tracked if added to state */
		dc_state_track_phantom_stream(state, phantom_stream);
	}

	/* setup subvp meta */
	main_stream_status = dc_state_get_stream_status(state, main_stream);
	phantom_stream_status = dc_state_get_stream_status(state, phantom_stream);
	phantom_stream_status->mall_stream_config.type = SUBVP_PHANTOM;
	phantom_stream_status->mall_stream_config.paired_stream = main_stream;
	main_stream_status->mall_stream_config.type = SUBVP_MAIN;
	main_stream_status->mall_stream_config.paired_stream = phantom_stream;

	return res;
}

enum dc_status dc_state_remove_phantom_stream(struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream)
{
	struct dc_stream_status *main_stream_status;
	struct dc_stream_status *phantom_stream_status;

	/* reset subvp meta */
	phantom_stream_status = dc_state_get_stream_status(state, phantom_stream);
	main_stream_status = dc_state_get_stream_status(state, phantom_stream_status->mall_stream_config.paired_stream);
	phantom_stream_status->mall_stream_config.type = SUBVP_NONE;
	phantom_stream_status->mall_stream_config.paired_stream = NULL;
	if (main_stream_status) {
		main_stream_status->mall_stream_config.type = SUBVP_NONE;
		main_stream_status->mall_stream_config.paired_stream = NULL;
	}

	/* remove stream from state */
	return dc_state_remove_stream(dc, state, phantom_stream);
}

bool dc_state_add_phantom_plane(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state *phantom_plane,
		struct dc_state *state)
{
	bool res = dc_state_add_plane(dc, phantom_stream, phantom_plane, state);

	/* check if stream is tracked */
	if (res && !dc_state_is_phantom_plane_tracked(state, phantom_plane)) {
		/* stream must be tracked if added to state */
		dc_state_track_phantom_plane(state, phantom_plane);
	}

	return res;
}

bool dc_state_remove_phantom_plane(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state *phantom_plane,
		struct dc_state *state)
{
	return dc_state_remove_plane(dc, phantom_stream, phantom_plane, state);
}

bool dc_state_rem_all_phantom_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_state *state,
		bool should_release_planes)
{
	int i, old_plane_count;
	struct dc_stream_status *stream_status = NULL;
	struct dc_plane_state *del_planes[MAX_SURFACE_NUM] = { 0 };

	for (i = 0; i < state->stream_count; i++)
		if (state->streams[i] == phantom_stream) {
			stream_status = &state->stream_status[i];
			break;
		}

	if (stream_status == NULL) {
		dm_error("Existing stream %p not found!\n", phantom_stream);
		return false;
	}

	old_plane_count = stream_status->plane_count;

	for (i = 0; i < old_plane_count; i++)
		del_planes[i] = stream_status->plane_states[i];

	for (i = 0; i < old_plane_count; i++) {
		if (!dc_state_remove_plane(dc, phantom_stream, del_planes[i], state))
			return false;
		if (should_release_planes)
			dc_state_release_phantom_plane(dc, state, del_planes[i]);
	}

	return true;
}

bool dc_state_add_all_phantom_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state * const *phantom_planes,
		int plane_count,
		struct dc_state *state)
{
	return dc_state_add_all_planes_for_stream(dc, phantom_stream, phantom_planes, plane_count, state);
}

bool dc_state_remove_phantom_streams_and_planes(
	struct dc *dc,
	struct dc_state *state)
{
	int i;
	bool removed_phantom = false;
	struct dc_stream_state *phantom_stream = NULL;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (pipe->plane_state && pipe->stream && dc_state_get_pipe_subvp_type(state, pipe) == SUBVP_PHANTOM) {
			phantom_stream = pipe->stream;

			dc_state_rem_all_phantom_planes_for_stream(dc, phantom_stream, state, false);
			dc_state_remove_phantom_stream(dc, state, phantom_stream);
			removed_phantom = true;
		}
	}
	return removed_phantom;
}

void dc_state_release_phantom_streams_and_planes(
		struct dc *dc,
		struct dc_state *state)
{
	int i;

	for (i = 0; i < state->phantom_stream_count; i++)
		dc_state_release_phantom_stream(dc, state, state->phantom_streams[i]);

	for (i = 0; i < state->phantom_plane_count; i++)
		dc_state_release_phantom_plane(dc, state, state->phantom_planes[i]);
}
