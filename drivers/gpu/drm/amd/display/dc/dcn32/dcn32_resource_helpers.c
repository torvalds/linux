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

// header file of functions being implemented
#include "dcn32_resource.h"
#include "dcn20/dcn20_resource.h"
#include "dml/dcn32/display_mode_vba_util_32.h"

/**
 * ********************************************************************************************
 * dcn32_helper_calculate_num_ways_for_subvp: Calculate number of ways needed for SubVP
 *
 * This function first checks the bytes required per pixel on the SubVP pipe, then calculates
 * the total number of pixels required in the SubVP MALL region. These are used to calculate
 * the number of cache lines used (then number of ways required) for SubVP MCLK switching.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 *
 * @return: number of ways required for SubVP
 *
 * ********************************************************************************************
 */
uint32_t dcn32_helper_calculate_num_ways_for_subvp(struct dc *dc, struct dc_state *context)
{
	uint32_t num_ways = 0;
	uint32_t mall_region_pixels = 0;
	uint32_t bytes_per_pixel = 0;
	uint32_t cache_lines_used = 0;
	uint32_t lines_per_way = 0;
	uint32_t total_cache_lines = 0;
	uint32_t bytes_in_mall = 0;
	uint32_t num_mblks = 0;
	uint32_t cache_lines_per_plane = 0;
	uint32_t i = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Find the phantom pipes
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			bytes_per_pixel = pipe->plane_state->format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 ? 8 : 4;
			mall_region_pixels = pipe->plane_state->plane_size.surface_pitch * pipe->stream->timing.v_addressable;

			// For bytes required in MALL, calculate based on number of MBlks required
			num_mblks = (mall_region_pixels * bytes_per_pixel +
					DCN3_2_MALL_MBLK_SIZE_BYTES - 1) / DCN3_2_MALL_MBLK_SIZE_BYTES;
			bytes_in_mall = num_mblks * DCN3_2_MALL_MBLK_SIZE_BYTES;
			// cache lines used is total bytes / cache_line size. Add +2 for worst case alignment
			// (MALL is 64-byte aligned)
			cache_lines_per_plane = bytes_in_mall / dc->caps.cache_line_size + 2;

			// For DCC we must cache the meat surface, so double cache lines required
			if (pipe->plane_state->dcc.enable)
				cache_lines_per_plane *= 2;
			cache_lines_used += cache_lines_per_plane;
		}
	}

	total_cache_lines = dc->caps.max_cab_allocation_bytes / dc->caps.cache_line_size;
	lines_per_way = total_cache_lines / dc->caps.cache_num_ways;
	num_ways = cache_lines_used / lines_per_way;
	if (cache_lines_used % lines_per_way > 0)
		num_ways++;

	return num_ways;
}

void dcn32_merge_pipes_for_subvp(struct dc *dc,
		struct dc_state *context)
{
	uint32_t i;

	/* merge pipes if necessary */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// For now merge all pipes for SubVP since pipe split case isn't supported yet

		/* if ODM merge we ignore mpc tree, mpo pipes will have their own flags */
		if (pipe->prev_odm_pipe) {
			/*split off odm pipe*/
			pipe->prev_odm_pipe->next_odm_pipe = pipe->next_odm_pipe;
			if (pipe->next_odm_pipe)
				pipe->next_odm_pipe->prev_odm_pipe = pipe->prev_odm_pipe;

			pipe->bottom_pipe = NULL;
			pipe->next_odm_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			pipe->top_pipe = NULL;
			pipe->prev_odm_pipe = NULL;
			if (pipe->stream_res.dsc)
				dcn20_release_dsc(&context->res_ctx, dc->res_pool, &pipe->stream_res.dsc);
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
		} else if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state) {
			struct pipe_ctx *top_pipe = pipe->top_pipe;
			struct pipe_ctx *bottom_pipe = pipe->bottom_pipe;

			top_pipe->bottom_pipe = bottom_pipe;
			if (bottom_pipe)
				bottom_pipe->top_pipe = top_pipe;

			pipe->top_pipe = NULL;
			pipe->bottom_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
		}
	}
}

bool dcn32_all_pipes_have_stream_and_plane(struct dc *dc,
		struct dc_state *context)
{
	uint32_t i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			return false;

		if (!pipe->plane_state)
			return false;
	}
	return true;
}

bool dcn32_subvp_in_use(struct dc *dc,
		struct dc_state *context)
{
	uint32_t i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->stream->mall_stream_config.type != SUBVP_NONE)
			return true;
	}
	return false;
}

bool dcn32_mpo_in_use(struct dc_state *context)
{
	uint32_t i;

	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count > 1)
			return true;
	}
	return false;
}

void dcn32_determine_det_override(struct dc_state *context, display_e2e_pipe_params_st *pipes,
		bool *is_pipe_split_expected, int pipe_cnt)
{
	int i, j, count, stream_segments, pipe_segments[MAX_PIPES];

	if (context->stream_count > 0) {
		stream_segments = 18 / context->stream_count;
		for (i = 0; i < context->stream_count; i++) {
			count = 0;
			for (j = 0; j < pipe_cnt; j++) {
				if (context->res_ctx.pipe_ctx[j].stream == context->streams[i]) {
					count++;
					if (is_pipe_split_expected[j])
						count++;
				}
			}
			pipe_segments[i] = stream_segments / count;
		}

		for (i = 0; i < pipe_cnt; i++) {
			pipes[i].pipe.src.det_size_override = 0;
			for (j = 0; j < context->stream_count; j++) {
				if (context->res_ctx.pipe_ctx[i].stream == context->streams[j]) {
					pipes[i].pipe.src.det_size_override = pipe_segments[j] * DCN3_2_DET_SEG_SIZE;
					break;
				}
			}
		}
	} else {
		for (i = 0; i < pipe_cnt; i++)
			pipes[i].pipe.src.det_size_override = 4 * DCN3_2_DET_SEG_SIZE; //DCN3_2_DEFAULT_DET_SIZE
	}
}
