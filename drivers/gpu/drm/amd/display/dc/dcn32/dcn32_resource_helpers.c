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
 * dcn32_helper_populate_phantom_dlg_params: Get DLG params for phantom pipes and populate pipe_ctx
 * with those params.
 *
 * This function must be called AFTER the phantom pipes are added to context and run through DML
 * (so that the DLG params for the phantom pipes can be populated), and BEFORE we program the
 * timing for the phantom pipes.
 *
 * @param [in] dc: current dc state
 * @param [in] context: new dc state
 * @param [in] pipes: DML pipe params array
 * @param [in] pipe_cnt: DML pipe count
 *
 * @return: void
 *
 * ********************************************************************************************
 */
void dcn32_helper_populate_phantom_dlg_params(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt)
{
	uint32_t i, pipe_idx;
	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		if (!pipe->stream)
			continue;

		if (pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			pipes[pipe_idx].pipe.dest.vstartup_start = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt,
					pipe_idx);
			pipes[pipe_idx].pipe.dest.vupdate_offset = get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
					pipe_idx);
			pipes[pipe_idx].pipe.dest.vupdate_width = get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt,
					pipe_idx);
			pipes[pipe_idx].pipe.dest.vready_offset = get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
					pipe_idx);
			pipe->pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		}
		pipe_idx++;
	}
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
	uint32_t mall_region_pixels = 0;
	uint32_t bytes_per_pixel = 0;
	uint32_t cache_lines_used = 0;
	uint32_t lines_per_way = 0;
	uint32_t total_cache_lines = 0;
	uint32_t i = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Find the phantom pipes
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			bytes_per_pixel = pipe->plane_state->format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 ? 8 : 4;
			mall_region_pixels = pipe->stream->timing.h_addressable * pipe->stream->timing.v_addressable;
			// cache lines used is total bytes / cache_line size. Add +2 for worst case alignment
			// (MALL is 64-byte aligned)
			cache_lines_used += (bytes_per_pixel * mall_region_pixels) / dc->caps.cache_line_size + 2;
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

bool dcn32_predict_pipe_split(struct dc_state *context, display_pipe_params_st pipe, int index)
{
	double pscl_throughput, pscl_throughput_chroma, dpp_clk_single_dpp, clock,
		clk_frequency = 0.0, vco_speed = context->bw_ctx.dml.soc.dispclk_dppclk_vco_speed_mhz;

	dml32_CalculateSinglePipeDPPCLKAndSCLThroughput(pipe.scale_ratio_depth.hscl_ratio,
			pipe.scale_ratio_depth.hscl_ratio_c,
			pipe.scale_ratio_depth.vscl_ratio,
			pipe.scale_ratio_depth.vscl_ratio_c,
			context->bw_ctx.dml.ip.max_dchub_pscl_bw_pix_per_clk,
			context->bw_ctx.dml.ip.max_pscl_lb_bw_pix_per_clk,
			pipe.dest.pixel_rate_mhz,
			pipe.src.source_format,
			pipe.scale_taps.htaps,
			pipe.scale_taps.htaps_c,
			pipe.scale_taps.vtaps,
			pipe.scale_taps.vtaps_c,

			/* Output */
			&pscl_throughput, &pscl_throughput_chroma,
			&dpp_clk_single_dpp);

	clock = dpp_clk_single_dpp * (1 + context->bw_ctx.dml.soc.dcn_downspread_percent / 100);

	if (clock > 0)
		clk_frequency = vco_speed * 4.0 / ((int) (vco_speed * 4.0));

	if (clk_frequency > context->bw_ctx.dml.soc.clock_limits[index].dppclk_mhz)
		return true;
	else
		return false;
}

void dcn32_determine_det_override(struct dc_state *context, display_e2e_pipe_params_st *pipes,
		bool *is_pipe_split_expected, int pipe_cnt)
{
	int i, j, count, stream_segments, pipe_segments[MAX_PIPES];

	if (context->stream_count > 0) {
		stream_segments = 18 / context->stream_count;
		for (i = 0, count = 0; i < context->stream_count; i++) {
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
