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

#ifndef _DC_STATE_PRIV_H_
#define _DC_STATE_PRIV_H_

#include "dc_state.h"
#include "dc_stream.h"

struct dc_stream_state *dc_state_get_stream_from_id(const struct dc_state *state, unsigned int id);

/* Get the type of the provided resource (none, phantom, main) based on the provided
 * context. If the context is unavailable, determine only if phantom or not.
 */
enum mall_stream_type dc_state_get_pipe_subvp_type(const struct dc_state *state,
		const struct pipe_ctx *pipe_ctx);
enum mall_stream_type dc_state_get_stream_subvp_type(const struct dc_state *state,
		const struct dc_stream_state *stream);

/* Gets the phantom stream if main is provided, gets the main if phantom is provided.*/
struct dc_stream_state *dc_state_get_paired_subvp_stream(const struct dc_state *state,
		const struct dc_stream_state *stream);

/* allocate's phantom stream or plane and returns pointer to the object */
struct dc_stream_state *dc_state_create_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *main_stream);
struct dc_plane_state *dc_state_create_phantom_plane(const struct dc *dc,
		struct dc_state *state,
		struct dc_plane_state *main_plane);

/* deallocate's phantom stream or plane */
void dc_state_release_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream);
void dc_state_release_phantom_plane(const struct dc *dc,
		struct dc_state *state,
		struct dc_plane_state *phantom_plane);

/* add/remove phantom stream to context and generate subvp meta data */
enum dc_status dc_state_add_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream,
		struct dc_stream_state *main_stream);
enum dc_status dc_state_remove_phantom_stream(const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream);

bool dc_state_add_phantom_plane(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state *phantom_plane,
		struct dc_state *state);

bool dc_state_remove_phantom_plane(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state *phantom_plane,
		struct dc_state *state);

bool dc_state_rem_all_phantom_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_state *state,
		bool should_release_planes);

bool dc_state_add_all_phantom_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *phantom_stream,
		struct dc_plane_state * const *phantom_planes,
		int plane_count,
		struct dc_state *state);

bool dc_state_remove_phantom_streams_and_planes(
		const struct dc *dc,
		struct dc_state *state);

void dc_state_release_phantom_streams_and_planes(
		const struct dc *dc,
		struct dc_state *state);

bool dc_state_is_fams2_in_use(
		const struct dc *dc,
		const struct dc_state *state);


void dc_state_set_stream_subvp_cursor_limit(const struct dc_stream_state *stream,
		struct dc_state *state,
		bool limit);

bool dc_state_get_stream_subvp_cursor_limit(const struct dc_stream_state *stream,
		struct dc_state *state);

void dc_state_set_stream_cursor_subvp_limit(const struct dc_stream_state *stream,
		struct dc_state *state,
		bool limit);

bool dc_state_get_stream_cursor_subvp_limit(const struct dc_stream_state *stream,
		struct dc_state *state);

bool dc_state_can_clear_stream_cursor_subvp_limit(const struct dc_stream_state *stream,
		struct dc_state *state);

bool dc_state_is_subvp_in_use(struct dc_state *state);

#endif /* _DC_STATE_PRIV_H_ */
