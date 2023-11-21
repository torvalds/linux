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
#include "dcn32/dcn32_resource.h"
#include "dcn20/dcn20_resource.h"
#include "dml/dcn32/display_mode_vba_util_32.h"
#include "dml/dcn32/dcn32_fpu.h"
#include "dc_state_priv.h"

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}


uint32_t dcn32_helper_mall_bytes_to_ways(
		struct dc *dc,
		uint32_t total_size_in_mall_bytes)
{
	uint32_t cache_lines_used, lines_per_way, total_cache_lines, num_ways;

	/* add 2 lines for worst case alignment */
	cache_lines_used = total_size_in_mall_bytes / dc->caps.cache_line_size + 2;

	total_cache_lines = dc->caps.max_cab_allocation_bytes / dc->caps.cache_line_size;
	lines_per_way = total_cache_lines / dc->caps.cache_num_ways;
	num_ways = cache_lines_used / lines_per_way;
	if (cache_lines_used % lines_per_way > 0)
		num_ways++;

	return num_ways;
}

uint32_t dcn32_helper_calculate_mall_bytes_for_cursor(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool ignore_cursor_buf)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	uint32_t cursor_size = hubp->curs_attr.pitch * hubp->curs_attr.height;
	uint32_t cursor_mall_size_bytes = 0;

	switch (pipe_ctx->stream->cursor_attributes.color_format) {
	case CURSOR_MODE_MONO:
		cursor_size /= 2;
		break;
	case CURSOR_MODE_COLOR_1BIT_AND:
	case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
	case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
		cursor_size *= 4;
		break;

	case CURSOR_MODE_COLOR_64BIT_FP_PRE_MULTIPLIED:
	case CURSOR_MODE_COLOR_64BIT_FP_UN_PRE_MULTIPLIED:
		cursor_size *= 8;
		break;
	}

	/* only count if cursor is enabled, and if additional allocation needed outside of the
	 * DCN cursor buffer
	 */
	if (pipe_ctx->stream->cursor_position.enable && (ignore_cursor_buf ||
			cursor_size > 16384)) {
		/* cursor_num_mblk = CEILING(num_cursors*cursor_width*cursor_width*cursor_Bpe/mblk_bytes, 1)
		 * Note: add 1 mblk in case of cursor misalignment
		 */
		cursor_mall_size_bytes = ((cursor_size + DCN3_2_MALL_MBLK_SIZE_BYTES - 1) /
				DCN3_2_MALL_MBLK_SIZE_BYTES + 1) * DCN3_2_MALL_MBLK_SIZE_BYTES;
	}

	return cursor_mall_size_bytes;
}

/**
 * dcn32_helper_calculate_num_ways_for_subvp(): Calculate number of ways needed for SubVP
 *
 * Gets total allocation required for the phantom viewport calculated by DML in bytes and
 * converts to number of cache ways.
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return: number of ways required for SubVP
 */
uint32_t dcn32_helper_calculate_num_ways_for_subvp(
		struct dc *dc,
		struct dc_state *context)
{
	if (context->bw_ctx.bw.dcn.mall_subvp_size_bytes > 0) {
		if (dc->debug.force_subvp_num_ways) {
			return dc->debug.force_subvp_num_ways;
		} else {
			return dcn32_helper_mall_bytes_to_ways(dc, context->bw_ctx.bw.dcn.mall_subvp_size_bytes);
		}
	} else {
		return 0;
	}
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

		if (dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_NONE)
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

bool dcn32_is_center_timing(struct pipe_ctx *pipe)
{
	bool is_center_timing = false;

	if (pipe->stream) {
		if (pipe->stream->timing.v_addressable != pipe->stream->dst.height ||
				pipe->stream->timing.v_addressable != pipe->stream->src.height) {
			is_center_timing = true;
		}
	}

	if (pipe->plane_state) {
		if (pipe->stream->timing.v_addressable != pipe->plane_state->dst_rect.height &&
				pipe->stream->timing.v_addressable != pipe->plane_state->src_rect.height) {
			is_center_timing = true;
		}
	}

	return is_center_timing;
}

bool dcn32_is_psr_capable(struct pipe_ctx *pipe)
{
	bool psr_capable = false;

	if (pipe->stream && pipe->stream->link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED) {
		psr_capable = true;
	}
	return psr_capable;
}

static void override_det_for_subvp(struct dc *dc, struct dc_state *context, uint8_t pipe_segments[])
{
	uint32_t i;
	uint8_t fhd_count = 0;
	uint8_t subvp_high_refresh_count = 0;
	uint8_t stream_count = 0;

	// Do not override if a stream has multiple planes
	for (i = 0; i < context->stream_count; i++) {
		if (context->stream_status[i].plane_count > 1)
			return;

		if (dc_state_get_stream_subvp_type(context, context->streams[i]) != SUBVP_PHANTOM)
			stream_count++;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream && pipe_ctx->plane_state && dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM) {
			if (dcn32_allow_subvp_high_refresh_rate(dc, context, pipe_ctx)) {

				if (pipe_ctx->stream->timing.v_addressable == 1080 && pipe_ctx->stream->timing.h_addressable == 1920) {
					fhd_count++;
				}
				subvp_high_refresh_count++;
			}
		}
	}

	if (stream_count == 2 && subvp_high_refresh_count == 2 && fhd_count == 1) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream && pipe_ctx->plane_state && dc_state_get_pipe_subvp_type(context, pipe_ctx)) {
				if (pipe_ctx->stream->timing.v_addressable == 1080 && pipe_ctx->stream->timing.h_addressable == 1920) {
					if (pipe_segments[i] > 4)
						pipe_segments[i] = 4;
				}
			}
		}
	}
}

/**
 * dcn32_determine_det_override(): Determine DET allocation for each pipe
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
 * @dc: Current DC state
 * @context: New DC state to be programmed
 * @pipes: Array of DML pipes
 *
 * Return: void
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
		if (dc_state_get_stream_subvp_type(context, context->streams[i]) != SUBVP_PHANTOM)
			stream_count++;
	}

	if (stream_count > 0) {
		stream_segments = 18 / stream_count;
		for (i = 0; i < context->stream_count; i++) {
			if (dc_state_get_stream_subvp_type(context, context->streams[i]) == SUBVP_PHANTOM)
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

		override_det_for_subvp(dc, context, pipe_segments);
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
	bool disable_unbounded_requesting = dc->debug.disable_z9_mpc || dc->debug.disable_unbounded_requesting;

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
		if (pipe->plane_state && !disable_unbounded_requesting && pipe->plane_state->tiling_info.gfx9.swizzle != DC_SW_LINEAR) {
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

#define MAX_STRETCHED_V_BLANK 1000 // in micro-seconds (must ensure to match value in FW)
/*
 * Scaling factor for v_blank stretch calculations considering timing in
 * micro-seconds and pixel clock in 100hz.
 * Note: the parenthesis are necessary to ensure the correct order of
 * operation where V_SCALE is used.
 */
#define V_SCALE (10000 / MAX_STRETCHED_V_BLANK)

static int get_frame_rate_at_max_stretch_100hz(
		struct dc_stream_state *fpo_candidate_stream,
		uint32_t fpo_vactive_margin_us)
{
	struct dc_crtc_timing *timing = NULL;
	uint32_t sec_per_100_lines;
	uint32_t max_v_blank;
	uint32_t curr_v_blank;
	uint32_t v_stretch_max;
	uint32_t stretched_frame_pix_cnt;
	uint32_t scaled_stretched_frame_pix_cnt;
	uint32_t scaled_refresh_rate;
	uint32_t v_scale;

	if (fpo_candidate_stream == NULL)
		return 0;

	/* check if refresh rate at least 120hz */
	timing = &fpo_candidate_stream->timing;
	if (timing == NULL)
		return 0;

	v_scale = 10000 / (MAX_STRETCHED_V_BLANK + fpo_vactive_margin_us);

	sec_per_100_lines = timing->pix_clk_100hz / timing->h_total + 1;
	max_v_blank = sec_per_100_lines / v_scale + 1;
	curr_v_blank = timing->v_total - timing->v_addressable;
	v_stretch_max = (max_v_blank > curr_v_blank) ? (max_v_blank - curr_v_blank) : (0);
	stretched_frame_pix_cnt = (v_stretch_max + timing->v_total) * timing->h_total;
	scaled_stretched_frame_pix_cnt = stretched_frame_pix_cnt / 10000;
	scaled_refresh_rate = (timing->pix_clk_100hz) / scaled_stretched_frame_pix_cnt + 1;

	return scaled_refresh_rate;

}

static bool is_refresh_rate_support_mclk_switch_using_fw_based_vblank_stretch(
		struct dc_stream_state *fpo_candidate_stream, uint32_t fpo_vactive_margin_us)
{
	int refresh_rate_max_stretch_100hz;
	int min_refresh_100hz;

	if (fpo_candidate_stream == NULL)
		return false;

	refresh_rate_max_stretch_100hz = get_frame_rate_at_max_stretch_100hz(fpo_candidate_stream, fpo_vactive_margin_us);
	min_refresh_100hz = fpo_candidate_stream->timing.min_refresh_in_uhz / 10000;

	if (refresh_rate_max_stretch_100hz < min_refresh_100hz)
		return false;

	return true;
}

static int get_refresh_rate(struct dc_stream_state *fpo_candidate_stream)
{
	int refresh_rate = 0;
	int h_v_total = 0;
	struct dc_crtc_timing *timing = NULL;

	if (fpo_candidate_stream == NULL)
		return 0;

	/* check if refresh rate at least 120hz */
	timing = &fpo_candidate_stream->timing;
	if (timing == NULL)
		return 0;

	h_v_total = timing->h_total * timing->v_total;
	if (h_v_total == 0)
		return 0;

	refresh_rate = ((timing->pix_clk_100hz * 100) / (h_v_total)) + 1;
	return refresh_rate;
}

/**
 * dcn32_can_support_mclk_switch_using_fw_based_vblank_stretch() - Determines if config can
 *								    support FPO
 *
 * @dc: current dc state
 * @context: new dc state
 *
 * Return: Pointer to FPO stream candidate if config can support FPO, otherwise NULL
 */
struct dc_stream_state *dcn32_can_support_mclk_switch_using_fw_based_vblank_stretch(struct dc *dc, const struct dc_state *context)
{
	int refresh_rate = 0;
	const int minimum_refreshrate_supported = 120;
	struct dc_stream_state *fpo_candidate_stream = NULL;
	bool is_fpo_vactive = false;
	uint32_t fpo_vactive_margin_us = 0;

	if (context == NULL)
		return NULL;

	if (dc->debug.disable_fams)
		return NULL;

	if (!dc->caps.dmub_caps.mclk_sw)
		return NULL;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching_shut_down)
		return NULL;

	/* For FPO we can support up to 2 display configs if:
	 * - first display uses FPO
	 * - Second display switches in VACTIVE */
	if (context->stream_count > 2)
		return NULL;
	else if (context->stream_count == 2) {
		DC_FP_START();
		dcn32_assign_fpo_vactive_candidate(dc, context, &fpo_candidate_stream);
		DC_FP_END();

		DC_FP_START();
		is_fpo_vactive = dcn32_find_vactive_pipe(dc, context, dc->debug.fpo_vactive_min_active_margin_us);
		DC_FP_END();
		if (!is_fpo_vactive || dc->debug.disable_fpo_vactive)
			return NULL;
	} else
		fpo_candidate_stream = context->streams[0];

	if (!fpo_candidate_stream)
		return NULL;

	if (fpo_candidate_stream->sink->edid_caps.panel_patch.disable_fams)
		return NULL;

	refresh_rate = get_refresh_rate(fpo_candidate_stream);
	if (refresh_rate < minimum_refreshrate_supported)
		return NULL;

	fpo_vactive_margin_us = is_fpo_vactive ? dc->debug.fpo_vactive_margin_us : 0; // For now hardcode the FPO + Vactive stretch margin to be 2000us
	if (!is_refresh_rate_support_mclk_switch_using_fw_based_vblank_stretch(fpo_candidate_stream, fpo_vactive_margin_us))
		return NULL;

	if (!fpo_candidate_stream->allow_freesync)
		return NULL;

	if (fpo_candidate_stream->vrr_active_variable && dc->debug.disable_fams_gaming)
		return NULL;

	return fpo_candidate_stream;
}

bool dcn32_check_native_scaling_for_res(struct pipe_ctx *pipe, unsigned int width, unsigned int height)
{
	bool is_native_scaling = false;

	if (pipe->stream->timing.h_addressable == width &&
			pipe->stream->timing.v_addressable == height &&
			pipe->plane_state->src_rect.width == width &&
			pipe->plane_state->src_rect.height == height &&
			pipe->plane_state->dst_rect.width == width &&
			pipe->plane_state->dst_rect.height == height)
		is_native_scaling = true;

	return is_native_scaling;
}

/**
 * disallow_subvp_in_active_plus_blank() - Function to determine disallowed subvp + drr/vblank configs
 *
 * @pipe: subvp pipe to be used for the subvp + drr/vblank config
 *
 * Since subvp is being enabled on more configs (such as 1080p60), we want
 * to explicitly block any configs that we don't want to enable. We do not
 * want to enable any 1080p60 (SubVP) + drr / vblank configs since these
 * are already convered by FPO.
 *
 * Return: True if disallowed, false otherwise
 */
static bool disallow_subvp_in_active_plus_blank(struct pipe_ctx *pipe)
{
	bool disallow = false;

	if (resource_is_pipe_type(pipe, OPP_HEAD) &&
			resource_is_pipe_type(pipe, DPP_PIPE)) {
		if (pipe->stream->timing.v_addressable == 1080 && pipe->stream->timing.h_addressable == 1920)
			disallow = true;
	}
	return disallow;
}

/**
 * dcn32_subvp_drr_admissable() - Determine if SubVP + DRR config is admissible
 *
 * @dc: Current DC state
 * @context: New DC state to be programmed
 *
 * SubVP + DRR is admissible under the following conditions:
 * - Config must have 2 displays (i.e., 2 non-phantom master pipes)
 * - One display is SubVP
 * - Other display must have Freesync enabled
 * - The potential DRR display must not be PSR capable
 *
 * Return: True if admissible, false otherwise
 */
bool dcn32_subvp_drr_admissable(struct dc *dc, struct dc_state *context)
{
	bool result = false;
	uint32_t i;
	uint8_t subvp_count = 0;
	uint8_t non_subvp_pipes = 0;
	bool drr_pipe_found = false;
	bool drr_psr_capable = false;
	uint64_t refresh_rate = 0;
	bool subvp_disallow = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		enum mall_stream_type pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe);

		if (resource_is_pipe_type(pipe, OPP_HEAD) &&
				resource_is_pipe_type(pipe, DPP_PIPE)) {
			if (pipe_mall_type == SUBVP_MAIN) {
				subvp_count++;

				subvp_disallow |= disallow_subvp_in_active_plus_blank(pipe);
				refresh_rate = (pipe->stream->timing.pix_clk_100hz * (uint64_t)100 +
					pipe->stream->timing.v_total * pipe->stream->timing.h_total - (uint64_t)1);
				refresh_rate = div_u64(refresh_rate, pipe->stream->timing.v_total);
				refresh_rate = div_u64(refresh_rate, pipe->stream->timing.h_total);
			}
			if (pipe_mall_type == SUBVP_NONE) {
				non_subvp_pipes++;
				drr_psr_capable = (drr_psr_capable || dcn32_is_psr_capable(pipe));
				if (pipe->stream->ignore_msa_timing_param &&
						(pipe->stream->allow_freesync || pipe->stream->vrr_active_variable || pipe->stream->vrr_active_fixed)) {
					drr_pipe_found = true;
				}
			}
		}
	}

	if (subvp_count == 1 && !subvp_disallow && non_subvp_pipes == 1 && drr_pipe_found && !drr_psr_capable &&
		((uint32_t)refresh_rate < 120))
		result = true;

	return result;
}

/**
 * dcn32_subvp_vblank_admissable() - Determine if SubVP + Vblank config is admissible
 *
 * @dc: Current DC state
 * @context: New DC state to be programmed
 * @vlevel: Voltage level calculated by DML
 *
 * SubVP + Vblank is admissible under the following conditions:
 * - Config must have 2 displays (i.e., 2 non-phantom master pipes)
 * - One display is SubVP
 * - Other display must not have Freesync capability
 * - DML must have output DRAM clock change support as SubVP + Vblank
 * - The potential vblank display must not be PSR capable
 *
 * Return: True if admissible, false otherwise
 */
bool dcn32_subvp_vblank_admissable(struct dc *dc, struct dc_state *context, int vlevel)
{
	bool result = false;
	uint32_t i;
	uint8_t subvp_count = 0;
	uint8_t non_subvp_pipes = 0;
	bool drr_pipe_found = false;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	bool vblank_psr_capable = false;
	uint64_t refresh_rate = 0;
	bool subvp_disallow = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		enum mall_stream_type pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe);

		if (resource_is_pipe_type(pipe, OPP_HEAD) &&
				resource_is_pipe_type(pipe, DPP_PIPE)) {
			if (pipe_mall_type == SUBVP_MAIN) {
				subvp_count++;

				subvp_disallow |= disallow_subvp_in_active_plus_blank(pipe);
				refresh_rate = (pipe->stream->timing.pix_clk_100hz * (uint64_t)100 +
					pipe->stream->timing.v_total * pipe->stream->timing.h_total - (uint64_t)1);
				refresh_rate = div_u64(refresh_rate, pipe->stream->timing.v_total);
				refresh_rate = div_u64(refresh_rate, pipe->stream->timing.h_total);
			}
			if (pipe_mall_type == SUBVP_NONE) {
				non_subvp_pipes++;
				vblank_psr_capable = (vblank_psr_capable || dcn32_is_psr_capable(pipe));
				if (pipe->stream->ignore_msa_timing_param &&
						(pipe->stream->allow_freesync || pipe->stream->vrr_active_variable || pipe->stream->vrr_active_fixed)) {
					drr_pipe_found = true;
				}
			}
		}
	}

	if (subvp_count == 1 && non_subvp_pipes == 1 && !drr_pipe_found && !vblank_psr_capable &&
		((uint32_t)refresh_rate < 120) && !subvp_disallow &&
		vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_vblank_w_mall_sub_vp)
		result = true;

	return result;
}

void dcn32_update_dml_pipes_odm_policy_based_on_context(struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe = NULL;

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		int odm_slice_count = 0;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;
		pipe = &res_ctx->pipe_ctx[i];
		odm_slice_count = resource_get_odm_slice_count(pipe);

		if (odm_slice_count == 1)
			pipes[pipe_cnt].pipe.dest.odm_combine_policy = dm_odm_combine_policy_dal;
		else if (odm_slice_count == 2)
			pipes[pipe_cnt].pipe.dest.odm_combine_policy = dm_odm_combine_policy_2to1;
		else if (odm_slice_count == 4)
			pipes[pipe_cnt].pipe.dest.odm_combine_policy = dm_odm_combine_policy_4to1;

		pipe_cnt++;
	}
}
