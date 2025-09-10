/* SPDX-License-Identifier: MIT */
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

#include "dml2_dc_types.h"
#include "dml2_internal_types.h"
#include "dml2_utils.h"
#include "dml2_mall_phantom.h"

unsigned int dml2_helper_calculate_num_ways_for_subvp(struct dml2_context *ctx, struct dc_state *context)
{
	uint32_t num_ways = 0;
	uint32_t bytes_per_pixel = 0;
	uint32_t cache_lines_used = 0;
	uint32_t lines_per_way = 0;
	uint32_t total_cache_lines = 0;
	uint32_t bytes_in_mall = 0;
	uint32_t num_mblks = 0;
	uint32_t cache_lines_per_plane = 0;
	uint32_t i = 0;
	uint32_t mblk_width = 0;
	uint32_t mblk_height = 0;
	uint32_t full_vp_width_blk_aligned = 0;
	uint32_t mall_alloc_width_blk_aligned = 0;
	uint32_t mall_alloc_height_blk_aligned = 0;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Find the phantom pipes
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe && !pipe->prev_odm_pipe &&
				ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM) {
			bytes_per_pixel = pipe->plane_state->format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 ? 8 : 4;
			mblk_width = ctx->config.mall_cfg.mblk_width_pixels;
			mblk_height = bytes_per_pixel == 4 ? mblk_width = ctx->config.mall_cfg.mblk_height_4bpe_pixels : ctx->config.mall_cfg.mblk_height_8bpe_pixels;

			/* full_vp_width_blk_aligned = FLOOR(vp_x_start + full_vp_width + blk_width - 1, blk_width) -
			 * FLOOR(vp_x_start, blk_width)
			 */
			full_vp_width_blk_aligned = ((pipe->plane_res.scl_data.viewport.x +
					pipe->plane_res.scl_data.viewport.width + mblk_width - 1) / mblk_width * mblk_width) +
					(pipe->plane_res.scl_data.viewport.x / mblk_width * mblk_width);

			/* mall_alloc_width_blk_aligned_l/c = full_vp_width_blk_aligned_l/c */
			mall_alloc_width_blk_aligned = full_vp_width_blk_aligned;

			/* mall_alloc_height_blk_aligned_l/c = CEILING(sub_vp_height_l/c - 1, blk_height_l/c) + blk_height_l/c */
			mall_alloc_height_blk_aligned = (pipe->stream->timing.v_addressable - 1 + mblk_height - 1) /
					mblk_height * mblk_height + mblk_height;

			/* full_mblk_width_ub_l/c = malldml2_mall_phantom.c_alloc_width_blk_aligned_l/c;
			 * full_mblk_height_ub_l/c = mall_alloc_height_blk_aligned_l/c;
			 * num_mblk_l/c = (full_mblk_width_ub_l/c / mblk_width_l/c) * (full_mblk_height_ub_l/c / mblk_height_l/c);
			 * (Should be divisible, but round up if not)
			 */
			num_mblks = ((mall_alloc_width_blk_aligned + mblk_width - 1) / mblk_width) *
					((mall_alloc_height_blk_aligned + mblk_height - 1) / mblk_height);
			bytes_in_mall = num_mblks * ctx->config.mall_cfg.mblk_size_bytes;
			// cache lines used is total bytes / cache_line size. Add +2 for worst case alignment
			// (MALL is 64-byte aligned)
			cache_lines_per_plane = bytes_in_mall / ctx->config.mall_cfg.cache_line_size_bytes + 2;

			// For DCC we must cache the meat surface, so double cache lines required
			if (pipe->plane_state->dcc.enable)
				cache_lines_per_plane *= 2;
			cache_lines_used += cache_lines_per_plane;
		}
	}

	total_cache_lines = ctx->config.mall_cfg.max_cab_allocation_bytes / ctx->config.mall_cfg.cache_line_size_bytes;
	lines_per_way = total_cache_lines / ctx->config.mall_cfg.cache_num_ways;
	num_ways = cache_lines_used / lines_per_way;
	if (cache_lines_used % lines_per_way > 0)
		num_ways++;

	return num_ways;
}

static void merge_pipes_for_subvp(struct dml2_context *ctx, struct dc_state *context)
{
	int i;

	/* merge pipes if necessary */
	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
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
				ctx->config.svp_pstate.callbacks.release_dsc(&context->res_ctx, ctx->config.svp_pstate.callbacks.dc->res_pool, &pipe->stream_res.dsc);
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

static bool all_pipes_have_stream_and_plane(struct dml2_context *ctx, const struct dc_state *context)
{
	int i;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		const struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (!pipe->plane_state)
			return false;
	}
	return true;
}

static bool mpo_in_use(const struct dc_state *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count > 1)
			return true;
	}
	return false;
}

/*
 * dcn32_get_num_free_pipes: Calculate number of free pipes
 *
 * This function assumes that a "used" pipe is a pipe that has
 * both a stream and a plane assigned to it.
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return:
 * Number of free pipes available in the context
 */
static unsigned int get_num_free_pipes(struct dml2_context *ctx, struct dc_state *state)
{
	unsigned int i;
	unsigned int free_pipes = 0;
	unsigned int num_pipes = 0;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (pipe->stream && !pipe->top_pipe) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}
		}
	}

	free_pipes = ctx->config.dcn_pipe_count - num_pipes;
	return free_pipes;
}

/*
 * dcn32_assign_subvp_pipe: Function to decide which pipe will use Sub-VP.
 *
 * We enter this function if we are Sub-VP capable (i.e. enough pipes available)
 * and regular P-State switching (i.e. VACTIVE/VBLANK) is not supported, or if
 * we are forcing SubVP P-State switching on the current config.
 *
 * The number of pipes used for the chosen surface must be less than or equal to the
 * number of free pipes available.
 *
 * In general we choose surfaces with the longest frame time first (better for SubVP + VBLANK).
 * For multi-display cases the ActiveDRAMClockChangeMargin doesn't provide enough info on its own
 * for determining which should be the SubVP pipe (need a way to determine if a pipe / plane doesn't
 * support MCLK switching naturally [i.e. ACTIVE or VBLANK]).
 *
 * @param dc: current dc state
 * @param context: new dc state
 * @param index: [out] dc pipe index for the pipe chosen to have phantom pipes assigned
 *
 * Return:
 * True if a valid pipe assignment was found for Sub-VP. Otherwise false.
 */
static bool assign_subvp_pipe(struct dml2_context *ctx, struct dc_state *context, unsigned int *index)
{
	unsigned int i, pipe_idx;
	unsigned int max_frame_time = 0;
	bool valid_assignment_found = false;
	unsigned int free_pipes = 2; //dcn32_get_num_free_pipes(dc, context);
	bool current_assignment_freesync = false;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	for (i = 0, pipe_idx = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		unsigned int num_pipes = 0;
		unsigned int refresh_rate = 0;

		if (!pipe->stream)
			continue;

		// Round up
		refresh_rate = (pipe->stream->timing.pix_clk_100hz * 100 +
				pipe->stream->timing.v_total * pipe->stream->timing.h_total - 1)
				/ (double)(pipe->stream->timing.v_total * pipe->stream->timing.h_total);
		/* SubVP pipe candidate requirements:
		 * - Refresh rate < 120hz
		 * - Not able to switch in vactive naturally (switching in active means the
		 *   DET provides enough buffer to hide the P-State switch latency -- trying
		 *   to combine this with SubVP can cause issues with the scheduling).
		 */
		if (pipe->plane_state && !pipe->top_pipe &&
				ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe) == SUBVP_NONE && refresh_rate < 120 &&
				vba->ActiveDRAMClockChangeLatencyMarginPerState[vba->VoltageLevel][vba->maxMpcComb][vba->pipe_plane[pipe_idx]] <= 0) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}

			pipe = &context->res_ctx.pipe_ctx[i];
			if (num_pipes <= free_pipes) {
				struct dc_stream_state *stream = pipe->stream;
				unsigned int frame_us = (stream->timing.v_total * stream->timing.h_total /
						(double)(stream->timing.pix_clk_100hz * 100)) * 1000000;
				if (frame_us > max_frame_time && !stream->ignore_msa_timing_param) {
					*index = i;
					max_frame_time = frame_us;
					valid_assignment_found = true;
					current_assignment_freesync = false;
				/* For the 2-Freesync display case, still choose the one with the
			     * longest frame time
			     */
				} else if (stream->ignore_msa_timing_param && (!valid_assignment_found ||
						(current_assignment_freesync && frame_us > max_frame_time))) {
					*index = i;
					valid_assignment_found = true;
					current_assignment_freesync = true;
				}
			}
		}
		pipe_idx++;
	}
	return valid_assignment_found;
}

/*
 * enough_pipes_for_subvp: Function to check if there are "enough" pipes for SubVP.
 *
 * This function returns true if there are enough free pipes
 * to create the required phantom pipes for any given stream
 * (that does not already have phantom pipe assigned).
 *
 * e.g. For a 2 stream config where the first stream uses one
 * pipe and the second stream uses 2 pipes (i.e. pipe split),
 * this function will return true because there is 1 remaining
 * pipe which can be used as the phantom pipe for the non pipe
 * split pipe.
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return:
 * True if there are enough free pipes to assign phantom pipes to at least one
 * stream that does not already have phantom pipes assigned. Otherwise false.
 */
static bool enough_pipes_for_subvp(struct dml2_context *ctx, struct dc_state *state)
{
	unsigned int i, split_cnt, free_pipes;
	unsigned int min_pipe_split = ctx->config.dcn_pipe_count + 1; // init as max number of pipes + 1
	bool subvp_possible = false;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		// Find the minimum pipe split count for non SubVP pipes
		if (pipe->stream && !pipe->top_pipe &&
				ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(state, pipe) == SUBVP_NONE) {
			split_cnt = 0;
			while (pipe) {
				split_cnt++;
				pipe = pipe->bottom_pipe;
			}

			if (split_cnt < min_pipe_split)
				min_pipe_split = split_cnt;
		}
	}

	free_pipes = get_num_free_pipes(ctx, state);

	// SubVP only possible if at least one pipe is being used (i.e. free_pipes
	// should not equal to the pipe_count)
	if (free_pipes >= min_pipe_split && free_pipes < ctx->config.dcn_pipe_count)
		subvp_possible = true;

	return subvp_possible;
}

/*
 * subvp_subvp_schedulable: Determine if SubVP + SubVP config is schedulable
 *
 * High level algorithm:
 * 1. Find longest microschedule length (in us) between the two SubVP pipes
 * 2. Check if the worst case overlap (VBLANK in middle of ACTIVE) for both
 * pipes still allows for the maximum microschedule to fit in the active
 * region for both pipes.
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return:
 * bool - True if the SubVP + SubVP config is schedulable, false otherwise
 */
static bool subvp_subvp_schedulable(struct dml2_context *ctx, struct dc_state *context)
{
	struct pipe_ctx *subvp_pipes[2];
	struct dc_stream_state *phantom = NULL;
	uint32_t microschedule_lines = 0;
	uint32_t index = 0;
	uint32_t i;
	uint32_t max_microschedule_us = 0;
	int32_t vactive1_us, vactive2_us, vblank1_us, vblank2_us;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		uint32_t time_us = 0;

		/* Loop to calculate the maximum microschedule time between the two SubVP pipes,
		 * and also to store the two main SubVP pipe pointers in subvp_pipes[2].
		 */
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
				ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe) == SUBVP_MAIN) {
			phantom = ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(context, pipe->stream);
			microschedule_lines = (phantom->timing.v_total - phantom->timing.v_front_porch) +
					phantom->timing.v_addressable;

			// Round up when calculating microschedule time (+ 1 at the end)
			time_us = (microschedule_lines * phantom->timing.h_total) /
					(double)(phantom->timing.pix_clk_100hz * 100) * 1000000 +
					ctx->config.svp_pstate.subvp_prefetch_end_to_mall_start_us +
					ctx->config.svp_pstate.subvp_fw_processing_delay_us + 1;
			if (time_us > max_microschedule_us)
				max_microschedule_us = time_us;

			subvp_pipes[index] = pipe;
			index++;

			// Maximum 2 SubVP pipes
			if (index == 2)
				break;
		}
	}
	vactive1_us = ((subvp_pipes[0]->stream->timing.v_addressable * subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vactive2_us = ((subvp_pipes[1]->stream->timing.v_addressable * subvp_pipes[1]->stream->timing.h_total) /
				(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank1_us = (((subvp_pipes[0]->stream->timing.v_total - subvp_pipes[0]->stream->timing.v_addressable) *
			subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank2_us = (((subvp_pipes[1]->stream->timing.v_total - subvp_pipes[1]->stream->timing.v_addressable) *
			subvp_pipes[1]->stream->timing.h_total) /
			(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;

	if ((vactive1_us - vblank2_us) / 2 > max_microschedule_us &&
	    (vactive2_us - vblank1_us) / 2 > max_microschedule_us)
		return true;

	return false;
}

/*
 * dml2_svp_drr_schedulable: Determine if SubVP + DRR config is schedulable
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and DRR pipe
 * 2. Determine the frame time for the DRR display when adding required margin for MCLK switching
 * (the margin is equal to the MALL region + DRR margin (500us))
 * 3.If (SubVP Active - Prefetch > Stretched DRR frame + max(MALL region, Stretched DRR frame))
 * then report the configuration as supported
 *
 * @dc: current dc state
 * @context: new dc state
 * @drr_pipe: DRR pipe_ctx for the SubVP + DRR config
 *
 * Return:
 * bool - True if the SubVP + DRR config is schedulable, false otherwise
 */
bool dml2_svp_drr_schedulable(struct dml2_context *ctx, struct dc_state *context, struct dc_crtc_timing *drr_timing)
{
	bool schedulable = false;
	uint32_t i;
	struct pipe_ctx *pipe = NULL;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_stream_state *phantom_stream;
	int16_t prefetch_us = 0;
	int16_t mall_region_us = 0;
	int16_t drr_frame_us = 0;	// nominal frame time
	int16_t subvp_active_us = 0;
	int16_t stretched_drr_us = 0;
	int16_t drr_stretched_vblank_us = 0;
	int16_t max_vblank_mallregion = 0;

	// Find SubVP pipe
	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		// Find the SubVP pipe
		if (ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe) == SUBVP_MAIN)
			break;
	}

	phantom_stream = ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(context, pipe->stream);
	main_timing = &pipe->stream->timing;
	phantom_timing = &phantom_stream->timing;
	prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
			ctx->config.svp_pstate.subvp_prefetch_end_to_mall_start_us;
	subvp_active_us = main_timing->v_addressable * main_timing->h_total /
			(double)(main_timing->pix_clk_100hz * 100) * 1000000;
	drr_frame_us = drr_timing->v_total * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000;
	// P-State allow width and FW delays already included phantom_timing->v_addressable
	mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
	stretched_drr_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;
	drr_stretched_vblank_us = (drr_timing->v_total - drr_timing->v_addressable) * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000 + (stretched_drr_us - drr_frame_us);
	max_vblank_mallregion = drr_stretched_vblank_us > mall_region_us ? drr_stretched_vblank_us : mall_region_us;

	/* We consider SubVP + DRR schedulable if the stretched frame duration of the DRR display (i.e. the
	 * highest refresh rate + margin that can support UCLK P-State switch) passes the static analysis
	 * for VBLANK: (VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
	 * and the max of (VBLANK blanking time, MALL region)).
	 */
	if (stretched_drr_us < (1 / (double)drr_timing->min_refresh_in_uhz) * 1000000 * 1000000 &&
			subvp_active_us - prefetch_us - stretched_drr_us - max_vblank_mallregion > 0)
		schedulable = true;

	return schedulable;
}


/*
 * subvp_vblank_schedulable: Determine if SubVP + VBLANK config is schedulable
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and VBLANK pipe
 * 2. If (SubVP Active - Prefetch > Vblank Frame Time + max(MALL region, Vblank blanking time))
 * then report the configuration as supported
 * 3. If the VBLANK display is DRR, then take the DRR static schedulability path
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return:
 * bool - True if the SubVP + VBLANK/DRR config is schedulable, false otherwise
 */
static bool subvp_vblank_schedulable(struct dml2_context *ctx, struct dc_state *context)
{
	struct pipe_ctx *pipe = NULL;
	struct pipe_ctx *subvp_pipe = NULL;
	bool found = false;
	bool schedulable = false;
	uint32_t i = 0;
	uint8_t vblank_index = 0;
	uint16_t prefetch_us = 0;
	uint16_t mall_region_us = 0;
	uint16_t vblank_frame_us = 0;
	uint16_t subvp_active_us = 0;
	uint16_t vblank_blank_us = 0;
	uint16_t max_vblank_mallregion = 0;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_crtc_timing *vblank_timing = NULL;
	struct dc_stream_state *phantom_stream;
	enum mall_stream_type pipe_mall_type;

	/* For SubVP + VBLANK/DRR cases, we assume there can only be
	 * a single VBLANK/DRR display. If DML outputs SubVP + VBLANK
	 * is supported, it is either a single VBLANK case or two VBLANK
	 * displays which are synchronized (in which case they have identical
	 * timings).
	 */
	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		pipe_mall_type = ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe);

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		if (!found && pipe_mall_type == SUBVP_NONE) {
			// Found pipe which is not SubVP or Phantom (i.e. the VBLANK pipe).
			vblank_index = i;
			found = true;
		}

		if (!subvp_pipe && pipe_mall_type == SUBVP_MAIN)
			subvp_pipe = pipe;
	}
	// Use ignore_msa_timing_param flag to identify as DRR
	if (found && context->res_ctx.pipe_ctx[vblank_index].stream->ignore_msa_timing_param) {
		// SUBVP + DRR case
		schedulable = dml2_svp_drr_schedulable(ctx, context, &context->res_ctx.pipe_ctx[vblank_index].stream->timing);
	} else if (found) {
		phantom_stream = ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(context, subvp_pipe->stream);
		main_timing = &subvp_pipe->stream->timing;
		phantom_timing = &phantom_stream->timing;
		vblank_timing = &context->res_ctx.pipe_ctx[vblank_index].stream->timing;
		// Prefetch time is equal to VACTIVE + BP + VSYNC of the phantom pipe
		// Also include the prefetch end to mallstart delay time
		prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
				ctx->config.svp_pstate.subvp_prefetch_end_to_mall_start_us;
		// P-State allow width and FW delays already included phantom_timing->v_addressable
		mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
		vblank_frame_us = vblank_timing->v_total * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		vblank_blank_us =  (vblank_timing->v_total - vblank_timing->v_addressable) * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		subvp_active_us = main_timing->v_addressable * main_timing->h_total /
				(double)(main_timing->pix_clk_100hz * 100) * 1000000;
		max_vblank_mallregion = vblank_blank_us > mall_region_us ? vblank_blank_us : mall_region_us;

		// Schedulable if VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
		// and the max of (VBLANK blanking time, MALL region)
		// TODO: Possibly add some margin (i.e. the below conditions should be [...] > X instead of [...] > 0)
		if (subvp_active_us - prefetch_us - vblank_frame_us - max_vblank_mallregion > 0)
			schedulable = true;
	}
	return schedulable;
}

/*
 * subvp_validate_static_schedulability: Check which SubVP case is calculated and handle
 * static analysis based on the case.
 *
 * Three cases:
 * 1. SubVP + SubVP
 * 2. SubVP + VBLANK (DRR checked internally)
 * 3. SubVP + VACTIVE (currently unsupported)
 *
 * @dc: current dc state
 * @context: new dc state
 * @vlevel: Voltage level calculated by DML
 *
 * Return:
 * bool - True if statically schedulable, false otherwise
 */
bool dml2_svp_validate_static_schedulability(struct dml2_context *ctx, struct dc_state *context, enum dml_dram_clock_change_support pstate_change_type)
{
	bool schedulable = true;	// true by default for single display case
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	uint32_t i, pipe_idx;
	uint8_t subvp_count = 0;
	uint8_t vactive_count = 0;

	for (i = 0, pipe_idx = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		enum mall_stream_type pipe_mall_type = ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe);

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && !pipe->top_pipe &&
				pipe_mall_type == SUBVP_MAIN)
			subvp_count++;

		// Count how many planes that aren't SubVP/phantom are capable of VACTIVE
		// switching (SubVP + VACTIVE unsupported). In situations where we force
		// SubVP for a VACTIVE plane, we don't want to increment the vactive_count.
		if (vba->ActiveDRAMClockChangeLatencyMargin[vba->pipe_plane[pipe_idx]] > 0 &&
		    pipe_mall_type == SUBVP_NONE) {
			vactive_count++;
		}
		pipe_idx++;
	}

	if (subvp_count == 2) {
		// Static schedulability check for SubVP + SubVP case
		schedulable = subvp_subvp_schedulable(ctx, context);
	} else if (pstate_change_type == dml_dram_clock_change_vblank_w_mall_sub_vp) {
		// Static schedulability check for SubVP + VBLANK case. Also handle the case where
		// DML outputs SubVP + VBLANK + VACTIVE (DML will report as SubVP + VBLANK)
		if (vactive_count > 0)
			schedulable = false;
		else
			schedulable = subvp_vblank_schedulable(ctx, context);
	} else if (pstate_change_type == dml_dram_clock_change_vactive_w_mall_sub_vp &&
			vactive_count > 0) {
		// For single display SubVP cases, DML will output dm_dram_clock_change_vactive_w_mall_sub_vp by default.
		// We tell the difference between SubVP vs. SubVP + VACTIVE by checking the vactive_count.
		// SubVP + VACTIVE currently unsupported
		schedulable = false;
	}
	return schedulable;
}

static void set_phantom_stream_timing(struct dml2_context *ctx, struct dc_state *state,
				     struct pipe_ctx *ref_pipe,
				     struct dc_stream_state *phantom_stream,
				     unsigned int dc_pipe_idx,
				     unsigned int svp_height,
				     unsigned int svp_vstartup)
{
	unsigned int i;
	double line_time, fp_and_sync_width_time;
	struct pipe_ctx *pipe;
	uint32_t phantom_vactive, phantom_bp, pstate_width_fw_delay_lines;
	static const double cvt_rb_vblank_max = ((double) 460 / (1000 * 1000));

	// Find DML pipe index (pipe_idx) using dc_pipe_idx
	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		pipe = &state->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (i == dc_pipe_idx)
			break;
	}

	// Calculate lines required for pstate allow width and FW processing delays
	pstate_width_fw_delay_lines = ((double)(ctx->config.svp_pstate.subvp_fw_processing_delay_us +
			ctx->config.svp_pstate.subvp_pstate_allow_width_us) / 1000000) *
			(ref_pipe->stream->timing.pix_clk_100hz * 100) /
			(double)ref_pipe->stream->timing.h_total;

	// DML calculation for MALL region doesn't take into account FW delay
	// and required pstate allow width for multi-display cases
	/* Add 16 lines margin to the MALL REGION because SUB_VP_START_LINE must be aligned
	 * to 2 swaths (i.e. 16 lines)
	 */
	phantom_vactive = svp_height + pstate_width_fw_delay_lines + ctx->config.svp_pstate.subvp_swath_height_margin_lines;

	phantom_stream->timing.v_front_porch = 1;

	line_time = phantom_stream->timing.h_total / ((double)phantom_stream->timing.pix_clk_100hz * 100);
	fp_and_sync_width_time = (phantom_stream->timing.v_front_porch + phantom_stream->timing.v_sync_width) * line_time;

	if ((svp_vstartup * line_time) + fp_and_sync_width_time > cvt_rb_vblank_max) {
		svp_vstartup = (cvt_rb_vblank_max - fp_and_sync_width_time) / line_time;
	}

	// For backporch of phantom pipe, use vstartup of the main pipe
	phantom_bp = svp_vstartup;

	phantom_stream->dst.y = 0;
	phantom_stream->dst.height = phantom_vactive;
	phantom_stream->src.y = 0;
	phantom_stream->src.height = phantom_vactive;

	phantom_stream->timing.v_addressable = phantom_vactive;

	phantom_stream->timing.v_total = phantom_stream->timing.v_addressable +
						phantom_stream->timing.v_front_porch +
						phantom_stream->timing.v_sync_width +
						phantom_bp;
	phantom_stream->timing.flags.DSC = 0; // Don't need DSC for phantom timing
}

static struct dc_stream_state *enable_phantom_stream(struct dml2_context *ctx, struct dc_state *state, unsigned int dc_pipe_idx, unsigned int svp_height, unsigned int vstartup)
{
	struct pipe_ctx *ref_pipe = &state->res_ctx.pipe_ctx[dc_pipe_idx];
	struct dc_stream_state *phantom_stream = ctx->config.svp_pstate.callbacks.create_phantom_stream(
			ctx->config.svp_pstate.callbacks.dc,
			state,
			ref_pipe->stream);

	/* stream has limited viewport and small timing */
	memcpy(&phantom_stream->timing, &ref_pipe->stream->timing, sizeof(phantom_stream->timing));
	memcpy(&phantom_stream->src, &ref_pipe->stream->src, sizeof(phantom_stream->src));
	memcpy(&phantom_stream->dst, &ref_pipe->stream->dst, sizeof(phantom_stream->dst));
	set_phantom_stream_timing(ctx, state, ref_pipe, phantom_stream, dc_pipe_idx, svp_height, vstartup);

	ctx->config.svp_pstate.callbacks.add_phantom_stream(ctx->config.svp_pstate.callbacks.dc,
			state,
			phantom_stream,
			ref_pipe->stream);
	return phantom_stream;
}

static void enable_phantom_plane(struct dml2_context *ctx,
		struct dc_state *state,
		struct dc_stream_state *phantom_stream,
		unsigned int dc_pipe_idx)
{
	struct dc_plane_state *phantom_plane = NULL;
	struct dc_plane_state *prev_phantom_plane = NULL;
	struct pipe_ctx *curr_pipe = &state->res_ctx.pipe_ctx[dc_pipe_idx];

	while (curr_pipe) {
		if (curr_pipe->top_pipe && curr_pipe->top_pipe->plane_state == curr_pipe->plane_state) {
			phantom_plane = prev_phantom_plane;
		} else {
			phantom_plane = ctx->config.svp_pstate.callbacks.create_phantom_plane(
					ctx->config.svp_pstate.callbacks.dc,
					state,
					curr_pipe->plane_state);
			if (!phantom_plane)
				return;
		}

		memcpy(&phantom_plane->address, &curr_pipe->plane_state->address, sizeof(phantom_plane->address));
		memcpy(&phantom_plane->scaling_quality, &curr_pipe->plane_state->scaling_quality,
				sizeof(phantom_plane->scaling_quality));
		memcpy(&phantom_plane->src_rect, &curr_pipe->plane_state->src_rect, sizeof(phantom_plane->src_rect));
		memcpy(&phantom_plane->dst_rect, &curr_pipe->plane_state->dst_rect, sizeof(phantom_plane->dst_rect));
		memcpy(&phantom_plane->clip_rect, &curr_pipe->plane_state->clip_rect, sizeof(phantom_plane->clip_rect));
		memcpy(&phantom_plane->plane_size, &curr_pipe->plane_state->plane_size,
				sizeof(phantom_plane->plane_size));
		memcpy(&phantom_plane->tiling_info, &curr_pipe->plane_state->tiling_info,
				sizeof(phantom_plane->tiling_info));
		memcpy(&phantom_plane->dcc, &curr_pipe->plane_state->dcc, sizeof(phantom_plane->dcc));
		//phantom_plane->tiling_info.gfx10compatible.compat_level = curr_pipe->plane_state->tiling_info.gfx10compatible.compat_level;
		phantom_plane->format = curr_pipe->plane_state->format;
		phantom_plane->rotation = curr_pipe->plane_state->rotation;
		phantom_plane->visible = curr_pipe->plane_state->visible;

		/* Shadow pipe has small viewport. */
		phantom_plane->clip_rect.y = 0;
		phantom_plane->clip_rect.height = phantom_stream->timing.v_addressable;

		ctx->config.svp_pstate.callbacks.add_phantom_plane(ctx->config.svp_pstate.callbacks.dc, phantom_stream, phantom_plane, state);

		curr_pipe = curr_pipe->bottom_pipe;
		prev_phantom_plane = phantom_plane;
	}
}

static void add_phantom_pipes_for_main_pipe(struct dml2_context *ctx, struct dc_state *state, unsigned int main_pipe_idx, unsigned int svp_height, unsigned int vstartup)
{
	struct dc_stream_state *phantom_stream = NULL;
	unsigned int i;

	// The index of the DC pipe passed into this function is guarenteed to
	// be a valid candidate for SubVP (i.e. has a plane, stream, doesn't
	// already have phantom pipe assigned, etc.) by previous checks.
	phantom_stream = enable_phantom_stream(ctx, state, main_pipe_idx, svp_height, vstartup);
	enable_phantom_plane(ctx, state, phantom_stream, main_pipe_idx);

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		// Build scaling params for phantom pipes which were newly added.
		// We determine which phantom pipes were added by comparing with
		// the phantom stream.
		if (pipe->plane_state && pipe->stream && pipe->stream == phantom_stream &&
				ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(state, pipe) == SUBVP_PHANTOM) {
			pipe->stream->use_dynamic_meta = false;
			pipe->plane_state->flip_immediate = false;
			if (!ctx->config.svp_pstate.callbacks.build_scaling_params(pipe)) {
				// Log / remove phantom pipes since failed to build scaling params
			}
		}
	}
}

static bool remove_all_phantom_planes_for_stream(struct dml2_context *ctx, struct dc_stream_state *stream, struct dc_state *context)
{
	int i, old_plane_count;
	struct dc_stream_status *stream_status = NULL;
	struct dc_plane_state *del_planes[MAX_SURFACES] = { 0 };

	for (i = 0; i < context->stream_count; i++)
			if (context->streams[i] == stream) {
				stream_status = &context->stream_status[i];
				break;
			}

	if (stream_status == NULL) {
		return false;
	}

	old_plane_count = stream_status->plane_count;

	for (i = 0; i < old_plane_count; i++)
		del_planes[i] = stream_status->plane_states[i];

	for (i = 0; i < old_plane_count; i++) {
		if (!ctx->config.svp_pstate.callbacks.remove_phantom_plane(ctx->config.svp_pstate.callbacks.dc, stream, del_planes[i], context))
			return false;
		ctx->config.svp_pstate.callbacks.release_phantom_plane(ctx->config.svp_pstate.callbacks.dc, context, del_planes[i]);
	}

	return true;
}

bool dml2_svp_remove_all_phantom_pipes(struct dml2_context *ctx, struct dc_state *state)
{
	int i;
	bool removed_pipe = false;
	struct dc_stream_state *phantom_stream = NULL;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];
		// build scaling params for phantom pipes
		if (pipe->plane_state && pipe->stream && ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(state, pipe) == SUBVP_PHANTOM) {
			phantom_stream = pipe->stream;

			remove_all_phantom_planes_for_stream(ctx, phantom_stream, state);
			ctx->config.svp_pstate.callbacks.remove_phantom_stream(ctx->config.svp_pstate.callbacks.dc, state, phantom_stream);
			ctx->config.svp_pstate.callbacks.release_phantom_stream(ctx->config.svp_pstate.callbacks.dc, state, phantom_stream);

			removed_pipe = true;
		}

		if (pipe->plane_state) {
			pipe->plane_state->is_phantom = false;
		}
	}
	return removed_pipe;
}


/* Conditions for setting up phantom pipes for SubVP:
 * 1. Not force disable SubVP
 * 2. Full update (i.e. DC_VALIDATE_MODE_AND_PROGRAMMING)
 * 3. Enough pipes are available to support SubVP (TODO: Which pipes will use VACTIVE / VBLANK / SUBVP?)
 * 4. Display configuration passes validation
 * 5. (Config doesn't support MCLK in VACTIVE/VBLANK || dc->debug.force_subvp_mclk_switch)
 */
bool dml2_svp_add_phantom_pipe_to_dc_state(struct dml2_context *ctx, struct dc_state *state, struct dml_mode_support_info_st *mode_support_info)
{
	unsigned int dc_pipe_idx, dml_pipe_idx;
	unsigned int svp_height, vstartup;

	if (ctx->config.svp_pstate.force_disable_subvp)
		return false;

	if (!all_pipes_have_stream_and_plane(ctx, state))
		return false;

	if (mpo_in_use(state))
		return false;

	merge_pipes_for_subvp(ctx, state);
	// to re-initialize viewport after the pipe merge
	for (int i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &state->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->plane_state || !pipe_ctx->stream)
			continue;

		ctx->config.svp_pstate.callbacks.build_scaling_params(pipe_ctx);
	}

	if (enough_pipes_for_subvp(ctx, state) && assign_subvp_pipe(ctx, state, &dc_pipe_idx)) {
		dml_pipe_idx = dml2_helper_find_dml_pipe_idx_by_stream_id(ctx, state->res_ctx.pipe_ctx[dc_pipe_idx].stream->stream_id);
		svp_height = mode_support_info->SubViewportLinesNeededInMALL[dml_pipe_idx];
		vstartup = dml_get_vstartup_calculated(&ctx->v20.dml_core_ctx, dml_pipe_idx);

		add_phantom_pipes_for_main_pipe(ctx, state, dc_pipe_idx, svp_height, vstartup);

		return true;
	}

	return false;
}
