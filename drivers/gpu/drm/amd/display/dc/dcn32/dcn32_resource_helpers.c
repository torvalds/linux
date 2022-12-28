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

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

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
	uint32_t bytes_per_pixel = 0;
	uint32_t cache_lines_used = 0;
	uint32_t lines_per_way = 0;
	uint32_t total_cache_lines = 0;
	uint32_t bytes_in_mall = 0;
	uint32_t num_mblks = 0;
	uint32_t cache_lines_per_plane = 0;
	uint32_t i = 0, j = 0;
	uint16_t mblk_width = 0;
	uint16_t mblk_height = 0;
	uint32_t full_vp_width_blk_aligned = 0;
	uint32_t full_vp_height_blk_aligned = 0;
	uint32_t mall_alloc_width_blk_aligned = 0;
	uint32_t mall_alloc_height_blk_aligned = 0;
	uint16_t full_vp_height = 0;
	bool subvp_in_use = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/* Find the phantom pipes.
		 * - For pipe split case we need to loop through the bottom and next ODM
		 *   pipes or only half the viewport size is counted
		 */
		if (pipe->stream && pipe->plane_state &&
				pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			struct pipe_ctx *main_pipe = NULL;

			subvp_in_use = true;
			/* Get full viewport height from main pipe (required for MBLK calculation) */
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				main_pipe = &context->res_ctx.pipe_ctx[j];
				if (main_pipe->stream == pipe->stream->mall_stream_config.paired_stream) {
					full_vp_height = main_pipe->plane_res.scl_data.viewport.height;
					break;
				}
			}

			bytes_per_pixel = pipe->plane_state->format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 ? 8 : 4;
			mblk_width = DCN3_2_MBLK_WIDTH;
			mblk_height = bytes_per_pixel == 4 ? DCN3_2_MBLK_HEIGHT_4BPE : DCN3_2_MBLK_HEIGHT_8BPE;

			/* full_vp_width_blk_aligned = FLOOR(vp_x_start + full_vp_width + blk_width - 1, blk_width) -
			 * FLOOR(vp_x_start, blk_width)
			 */
			full_vp_width_blk_aligned = ((pipe->plane_res.scl_data.viewport.x +
					pipe->plane_res.scl_data.viewport.width + mblk_width - 1) / mblk_width * mblk_width) -
					(pipe->plane_res.scl_data.viewport.x / mblk_width * mblk_width);

			/* full_vp_height_blk_aligned = FLOOR(vp_y_start + full_vp_height + blk_height - 1, blk_height) -
			 * FLOOR(vp_y_start, blk_height)
			 */
			full_vp_height_blk_aligned = ((pipe->plane_res.scl_data.viewport.y +
					full_vp_height + mblk_height - 1) / mblk_height * mblk_height) -
					(pipe->plane_res.scl_data.viewport.y / mblk_height * mblk_height);

			/* mall_alloc_width_blk_aligned_l/c = full_vp_width_blk_aligned_l/c */
			mall_alloc_width_blk_aligned = full_vp_width_blk_aligned;

			/* mall_alloc_height_blk_aligned_l/c = CEILING(sub_vp_height_l/c - 1, blk_height_l/c) + blk_height_l/c */
			mall_alloc_height_blk_aligned = (pipe->plane_res.scl_data.viewport.height - 1 + mblk_height - 1) /
					mblk_height * mblk_height + mblk_height;

			/* full_mblk_width_ub_l/c = mall_alloc_width_blk_aligned_l/c;
			 * full_mblk_height_ub_l/c = mall_alloc_height_blk_aligned_l/c;
			 * num_mblk_l/c = (full_mblk_width_ub_l/c / mblk_width_l/c) * (full_mblk_height_ub_l/c / mblk_height_l/c);
			 * (Should be divisible, but round up if not)
			 */
			num_mblks = ((mall_alloc_width_blk_aligned + mblk_width - 1) / mblk_width) *
					((mall_alloc_height_blk_aligned + mblk_height - 1) / mblk_height);

			/*For DCC:
			 * meta_num_mblk = CEILING(meta_pitch*full_vp_height*Bpe/256/mblk_bytes, 1)
			 */
			if (pipe->plane_state->dcc.enable)
				num_mblks += (pipe->plane_state->dcc.meta_pitch * pipe->plane_res.scl_data.viewport.height * bytes_per_pixel +
								(256 * DCN3_2_MALL_MBLK_SIZE_BYTES) - 1) / (256 * DCN3_2_MALL_MBLK_SIZE_BYTES);

			bytes_in_mall = num_mblks * DCN3_2_MALL_MBLK_SIZE_BYTES;
			// cache lines used is total bytes / cache_line size. Add +2 for worst case alignment
			// (MALL is 64-byte aligned)
			cache_lines_per_plane = bytes_in_mall / dc->caps.cache_line_size + 2;

			cache_lines_used += cache_lines_per_plane;
		}
	}

	total_cache_lines = dc->caps.max_cab_allocation_bytes / dc->caps.cache_line_size;
	lines_per_way = total_cache_lines / dc->caps.cache_num_ways;
	num_ways = cache_lines_used / lines_per_way;
	if (cache_lines_used % lines_per_way > 0)
		num_ways++;

	if (subvp_in_use && dc->debug.force_subvp_num_ways > 0)
		num_ways = dc->debug.force_subvp_num_ways;

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
			continue;

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


bool dcn32_any_surfaces_rotated(struct dc *dc, struct dc_state *context)
{
	uint32_t i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && pipe->plane_state->rotation != ROTATION_ANGLE_0)
			return true;
	}
	return false;
}

/**
 * *******************************************************************************************
 * dcn32_determine_det_override: Determine DET allocation for each pipe
 *
 * This function determines how much DET to allocate for each pipe. The total number of
 * DET segments will be split equally among each of the streams, and after that the DET
 * segments per stream will be split equally among the planes for the given stream.
 *
 * If there is a plane that's driven by more than 1 pipe (i.e. pipe split), then the
 * number of DET for that given plane will be split among the pipes driving that plane.
 *
 *
 * High level algorithm:
 * 1. Split total DET among number of streams
 * 2. For each stream, split DET among the planes
 * 3. For each plane, check if there is a pipe split. If yes, split the DET allocation
 *    among those pipes.
 * 4. Assign the DET override to the DML pipes.
 *
 * @param [in]: dc: Current DC state
 * @param [in]: context: New DC state to be programmed
 * @param [in]: pipes: Array of DML pipes
 *
 * @return: void
 *
 * *******************************************************************************************
 */
void dcn32_determine_det_override(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes)
{
	uint32_t i, j, k;
	uint8_t pipe_plane_count, stream_segments, plane_segments, pipe_segments[MAX_PIPES] = {0};
	uint8_t pipe_counted[MAX_PIPES] = {0};
	uint8_t pipe_cnt = 0;
	struct dc_plane_state *current_plane = NULL;
	uint8_t stream_count = 0;

	for (i = 0; i < context->stream_count; i++) {
		/* Don't count SubVP streams for DET allocation */
		if (context->streams[i]->mall_stream_config.type != SUBVP_PHANTOM) {
			stream_count++;
		}
	}

	if (stream_count > 0) {
		stream_segments = 18 / stream_count;
		for (i = 0; i < context->stream_count; i++) {
			if (context->streams[i]->mall_stream_config.type == SUBVP_PHANTOM)
				continue;
			if (context->stream_status[i].plane_count > 0)
				plane_segments = stream_segments / context->stream_status[i].plane_count;
			else
				plane_segments = stream_segments;
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				pipe_plane_count = 0;
				if (context->res_ctx.pipe_ctx[j].stream == context->streams[i] &&
						pipe_counted[j] != 1) {
					/* Note: pipe_plane_count indicates the number of pipes to be used for a
					 * given plane. e.g. pipe_plane_count = 1 means single pipe (i.e. not split),
					 * pipe_plane_count = 2 means 2:1 split, etc.
					 */
					pipe_plane_count++;
					pipe_counted[j] = 1;
					current_plane = context->res_ctx.pipe_ctx[j].plane_state;
					for (k = 0; k < dc->res_pool->pipe_count; k++) {
						if (k != j && context->res_ctx.pipe_ctx[k].stream == context->streams[i] &&
								context->res_ctx.pipe_ctx[k].plane_state == current_plane) {
							pipe_plane_count++;
							pipe_counted[k] = 1;
						}
					}

					pipe_segments[j] = plane_segments / pipe_plane_count;
					for (k = 0; k < dc->res_pool->pipe_count; k++) {
						if (k != j && context->res_ctx.pipe_ctx[k].stream == context->streams[i] &&
								context->res_ctx.pipe_ctx[k].plane_state == current_plane) {
							pipe_segments[k] = plane_segments / pipe_plane_count;
						}
					}
				}
			}
		}

		for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
			if (!context->res_ctx.pipe_ctx[i].stream)
				continue;
			pipes[pipe_cnt].pipe.src.det_size_override = pipe_segments[i] * DCN3_2_DET_SEG_SIZE;
			pipe_cnt++;
		}
	} else {
		for (i = 0; i < dc->res_pool->pipe_count; i++)
			pipes[i].pipe.src.det_size_override = 4 * DCN3_2_DET_SEG_SIZE; //DCN3_2_DEFAULT_DET_SIZE
	}
}

void dcn32_set_det_allocations(struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		pipe = &res_ctx->pipe_ctx[i];
		pipe_cnt++;
	}

	/* For DET allocation, we don't want to use DML policy (not optimal for utilizing all
	 * the DET available for each pipe). Use the DET override input to maintain our driver
	 * policy.
	 */
	if (pipe_cnt == 1) {
		pipes[0].pipe.src.det_size_override = DCN3_2_MAX_DET_SIZE;
		if (pipe->plane_state && !dc->debug.disable_z9_mpc && pipe->plane_state->tiling_info.gfx9.swizzle != DC_SW_LINEAR) {
			if (!is_dual_plane(pipe->plane_state->format)) {
				pipes[0].pipe.src.det_size_override = DCN3_2_DEFAULT_DET_SIZE;
				pipes[0].pipe.src.unbounded_req_mode = true;
				if (pipe->plane_state->src_rect.width >= 5120 &&
					pipe->plane_state->src_rect.height >= 2880)
					pipes[0].pipe.src.det_size_override = 320; // 5K or higher
			}
		}
	} else
		dcn32_determine_det_override(dc, context, pipes);
}

/**
 * *******************************************************************************************
 * dcn32_save_mall_state: Save MALL (SubVP) state for fast validation cases
 *
 * This function saves the MALL (SubVP) case for fast validation cases. For fast validation,
 * there are situations where a shallow copy of the dc->current_state is created for the
 * validation. In this case we want to save and restore the mall config because we always
 * teardown subvp at the beginning of validation (and don't attempt to add it back if it's
 * fast validation). If we don't restore the subvp config in cases of fast validation +
 * shallow copy of the dc->current_state, the dc->current_state will have a partially
 * removed subvp state when we did not intend to remove it.
 *
 * NOTE: This function ONLY works if the streams are not moved to a different pipe in the
 *       validation. We don't expect this to happen in fast_validation=1 cases.
 *
 * @param [in]: dc: Current DC state
 * @param [in]: context: New DC state to be programmed
 * @param [out]: temp_config: struct used to cache the existing MALL state
 *
 * @return: void
 *
 * *******************************************************************************************
 */
void dcn32_save_mall_state(struct dc *dc,
		struct dc_state *context,
		struct mall_temp_config *temp_config)
{
	uint32_t i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream)
			temp_config->mall_stream_config[i] = pipe->stream->mall_stream_config;

		if (pipe->plane_state)
			temp_config->is_phantom_plane[i] = pipe->plane_state->is_phantom;
	}
}

/**
 * *******************************************************************************************
 * dcn32_restore_mall_state: Restore MALL (SubVP) state for fast validation cases
 *
 * Restore the MALL state based on the previously saved state from dcn32_save_mall_state
 *
 * @param [in]: dc: Current DC state
 * @param [in/out]: context: New DC state to be programmed, restore MALL state into here
 * @param [in]: temp_config: struct that has the cached MALL state
 *
 * @return: void
 *
 * *******************************************************************************************
 */
void dcn32_restore_mall_state(struct dc *dc,
		struct dc_state *context,
		struct mall_temp_config *temp_config)
{
	uint32_t i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream)
			pipe->stream->mall_stream_config = temp_config->mall_stream_config[i];

		if (pipe->plane_state)
			pipe->plane_state->is_phantom = temp_config->is_phantom_plane[i];
	}
}
