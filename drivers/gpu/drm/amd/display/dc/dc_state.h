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

#ifndef _DC_STATE_H_
#define _DC_STATE_H_

#include "dc.h"
#include "inc/core_status.h"

struct dc_state *dc_state_create(struct dc *dc, struct dc_state_create_params *params);
void dc_state_copy(struct dc_state *dst_state, struct dc_state *src_state);
struct dc_state *dc_state_create_copy(struct dc_state *src_state);
void dc_state_copy_current(struct dc *dc, struct dc_state *dst_state);
struct dc_state *dc_state_create_current_copy(struct dc *dc);
void dc_state_construct(struct dc *dc, struct dc_state *state);
void dc_state_destruct(struct dc_state *state);
void dc_state_retain(struct dc_state *state);
void dc_state_release(struct dc_state *state);

enum dc_status dc_state_add_stream(const struct dc *dc,
				    struct dc_state *state,
				    struct dc_stream_state *stream);

enum dc_status dc_state_remove_stream(
		const struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream);

bool dc_state_add_plane(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *state);

bool dc_state_remove_plane(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *state);

bool dc_state_rem_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *state);

bool dc_state_add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state * const *plane_states,
		int plane_count,
		struct dc_state *state);

struct dc_stream_status *dc_state_get_stream_status(
	struct dc_state *state,
	const struct dc_stream_state *stream);
#endif /* _DC_STATE_H_ */
