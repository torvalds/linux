/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "dml_wrapper.h"
#include "resource.h"
#include "core_types.h"
#include "dsc.h"
#include "clk_mgr.h"

#ifndef DC_LOGGER_INIT
#define DC_LOGGER_INIT
#undef DC_LOG_WARNING
#define DC_LOG_WARNING
#endif

#define DML_WRAPPER_TRANSLATION_
#include "dml_wrapper_translation.c"
#undef DML_WRAPPER_TRANSLATION_

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

static void build_clamping_params(struct dc_stream_state *stream)
{
	stream->clamping.clamping_level = CLAMPING_FULL_RANGE;
	stream->clamping.c_depth = stream->timing.display_color_depth;
	stream->clamping.pixel_encoding = stream->timing.pixel_encoding;
}

static void get_pixel_clock_parameters(
	const struct pipe_ctx *pipe_ctx,
	struct pixel_clk_params *pixel_clk_params)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;

	/*TODO: is this halved for YCbCr 420? in that case we might want to move
	 * the pixel clock normalization for hdmi up to here instead of doing it
	 * in pll_adjust_pix_clk
	 */
	pixel_clk_params->requested_pix_clk_100hz = stream->timing.pix_clk_100hz;
	pixel_clk_params->encoder_object_id = stream->link->link_enc->id;
	pixel_clk_params->signal_type = pipe_ctx->stream->signal;
	pixel_clk_params->controller_id = pipe_ctx->stream_res.tg->inst + 1;
	/* TODO: un-hardcode*/
	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
						LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
	pixel_clk_params->flags.SUPPORT_YCBCR420 = (stream->timing.pixel_encoding ==
			PIXEL_ENCODING_YCBCR420);
	pixel_clk_params->pixel_encoding = stream->timing.pixel_encoding;
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		pixel_clk_params->color_depth = COLOR_DEPTH_888;
	}
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		pixel_clk_params->requested_pix_clk_100hz  = pixel_clk_params->requested_pix_clk_100hz / 2;
	}
	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		pixel_clk_params->requested_pix_clk_100hz *= 2;

}

static enum dc_status build_pipe_hw_param(struct pipe_ctx *pipe_ctx)
{
	get_pixel_clock_parameters(pipe_ctx, &pipe_ctx->stream_res.pix_clk_params);

	if (pipe_ctx->clock_source)
		pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			&pipe_ctx->pll_settings);

	pipe_ctx->stream->clamping.pixel_encoding = pipe_ctx->stream->timing.pixel_encoding;

	resource_build_bit_depth_reduction_params(pipe_ctx->stream,
					&pipe_ctx->stream->bit_depth_params);
	build_clamping_params(pipe_ctx->stream);

	return DC_OK;
}

static void resource_build_bit_depth_reduction_params(struct dc_stream_state *stream,
		struct bit_depth_reduction_params *fmt_bit_depth)
{
	enum dc_dither_option option = stream->dither_option;
	enum dc_pixel_encoding pixel_encoding =
			stream->timing.pixel_encoding;

	memset(fmt_bit_depth, 0, sizeof(*fmt_bit_depth));

	if (option == DITHER_OPTION_DEFAULT) {
		switch (stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			option = DITHER_OPTION_SPATIAL6;
			break;
		case COLOR_DEPTH_888:
			option = DITHER_OPTION_SPATIAL8;
			break;
		case COLOR_DEPTH_101010:
			option = DITHER_OPTION_SPATIAL10;
			break;
		default:
			option = DITHER_OPTION_DISABLE;
		}
	}

	if (option == DITHER_OPTION_DISABLE)
		return;

	if (option == DITHER_OPTION_TRUN6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 0;
	} else if (option == DITHER_OPTION_TRUN8 ||
			option == DITHER_OPTION_TRUN8_SPATIAL6 ||
			option == DITHER_OPTION_TRUN8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 1;
	} else if (option == DITHER_OPTION_TRUN10        ||
			option == DITHER_OPTION_TRUN10_SPATIAL6   ||
			option == DITHER_OPTION_TRUN10_SPATIAL8   ||
			option == DITHER_OPTION_TRUN10_FM8     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
	}

	/* special case - Formatter can only reduce by 4 bits at most.
	 * When reducing from 12 to 6 bits,
	 * HW recommends we use trunc with round mode
	 * (if we did nothing, trunc to 10 bits would be used)
	 * note that any 12->10 bit reduction is ignored prior to DCE8,
	 * as the input was 10 bits.
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
		fmt_bit_depth->flags.TRUNCATE_MODE = 1;
	}

	/* spatial dither
	 * note that spatial modes 1-3 are never used
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_TRUN10_SPATIAL6      ||
			option == DITHER_OPTION_TRUN8_SPATIAL6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 0;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL8_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL8_FM6        ||
			option == DITHER_OPTION_TRUN10_SPATIAL8      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 1;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL10_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL10 ||
			option == DITHER_OPTION_SPATIAL10_FM8 ||
			option == DITHER_OPTION_SPATIAL10_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 2;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	}

	if (option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL10) {
		fmt_bit_depth->flags.FRAME_RANDOM = 0;
	} else {
		fmt_bit_depth->flags.FRAME_RANDOM = 1;
	}

	//////////////////////
	//// temporal dither
	//////////////////////
	if (option == DITHER_OPTION_FM6           ||
			option == DITHER_OPTION_SPATIAL8_FM6     ||
			option == DITHER_OPTION_SPATIAL10_FM6     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN8_FM6      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 0;
	} else if (option == DITHER_OPTION_FM8        ||
			option == DITHER_OPTION_SPATIAL10_FM8  ||
			option == DITHER_OPTION_TRUN10_FM8) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 1;
	} else if (option == DITHER_OPTION_FM10) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 2;
	}

	fmt_bit_depth->pixel_encoding = pixel_encoding;
}

bool dml_validate_dsc(struct dc *dc, struct dc_state *new_ctx)
{
	int i;

	/* Validate DSC config, dsc count validation is already done */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &new_ctx->res_ctx.pipe_ctx[i];
		struct dc_stream_state *stream = pipe_ctx->stream;
		struct dsc_config dsc_cfg;
		struct pipe_ctx *odm_pipe;
		int opp_cnt = 1;

		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
			opp_cnt++;

		/* Only need to validate top pipe */
		if (pipe_ctx->top_pipe || pipe_ctx->prev_odm_pipe || !stream || !stream->timing.flags.DSC)
			continue;

		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left
				+ stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top
				+ stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		if (pipe_ctx->stream_res.dsc && !pipe_ctx->stream_res.dsc->funcs->dsc_validate_stream(pipe_ctx->stream_res.dsc, &dsc_cfg))
			return false;
	}
	return true;
}

enum dc_status dml_build_mapped_resource(const struct dc *dc, struct dc_state *context, struct dc_stream_state *stream)
{
	enum dc_status status = DC_OK;
	struct pipe_ctx *pipe_ctx = resource_get_head_pipe_for_stream(&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;


	status = build_pipe_hw_param(pipe_ctx);

	return status;
}

void dml_acquire_dsc(const struct dc *dc,
			struct resource_context *res_ctx,
			struct display_stream_compressor **dsc,
			int pipe_idx)
{
	int i;
	const struct resource_pool *pool = dc->res_pool;
	struct display_stream_compressor *dsc_old = dc->current_state->res_ctx.pipe_ctx[pipe_idx].stream_res.dsc;

	ASSERT(*dsc == NULL); /* If this ASSERT fails, dsc was not released properly */
	*dsc = NULL;

	/* Always do 1-to-1 mapping when number of DSCs is same as number of pipes */
	if (pool->res_cap->num_dsc == pool->res_cap->num_opp) {
		*dsc = pool->dscs[pipe_idx];
		res_ctx->is_dsc_acquired[pipe_idx] = true;
		return;
	}

	/* Return old DSC to avoid the need for redo it */
	if (dsc_old && !res_ctx->is_dsc_acquired[dsc_old->inst]) {
		*dsc = dsc_old;
		res_ctx->is_dsc_acquired[dsc_old->inst] = true;
		return ;
	}

	/* Find first free DSC */
	for (i = 0; i < pool->res_cap->num_dsc; i++)
		if (!res_ctx->is_dsc_acquired[i]) {
			*dsc = pool->dscs[i];
			res_ctx->is_dsc_acquired[i] = true;
			break;
		}
}

static bool dml_split_stream_for_mpc_or_odm(
		const struct dc *dc,
		struct resource_context *res_ctx,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm)
{
	int pipe_idx = sec_pipe->pipe_idx;
	const struct resource_pool *pool = dc->res_pool;

	*sec_pipe = *pri_pipe;

	sec_pipe->pipe_idx = pipe_idx;
	sec_pipe->plane_res.mi = pool->mis[pipe_idx];
	sec_pipe->plane_res.hubp = pool->hubps[pipe_idx];
	sec_pipe->plane_res.ipp = pool->ipps[pipe_idx];
	sec_pipe->plane_res.xfm = pool->transforms[pipe_idx];
	sec_pipe->plane_res.dpp = pool->dpps[pipe_idx];
	sec_pipe->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;
	sec_pipe->stream_res.dsc = NULL;
	if (odm) {
		if (pri_pipe->next_odm_pipe) {
			ASSERT(pri_pipe->next_odm_pipe != sec_pipe);
			sec_pipe->next_odm_pipe = pri_pipe->next_odm_pipe;
			sec_pipe->next_odm_pipe->prev_odm_pipe = sec_pipe;
		}
		if (pri_pipe->top_pipe && pri_pipe->top_pipe->next_odm_pipe) {
			pri_pipe->top_pipe->next_odm_pipe->bottom_pipe = sec_pipe;
			sec_pipe->top_pipe = pri_pipe->top_pipe->next_odm_pipe;
		}
		if (pri_pipe->bottom_pipe && pri_pipe->bottom_pipe->next_odm_pipe) {
			pri_pipe->bottom_pipe->next_odm_pipe->top_pipe = sec_pipe;
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe->next_odm_pipe;
		}
		pri_pipe->next_odm_pipe = sec_pipe;
		sec_pipe->prev_odm_pipe = pri_pipe;
		ASSERT(sec_pipe->top_pipe == NULL);

		if (!sec_pipe->top_pipe)
			sec_pipe->stream_res.opp = pool->opps[pipe_idx];
		else
			sec_pipe->stream_res.opp = sec_pipe->top_pipe->stream_res.opp;
		if (sec_pipe->stream->timing.flags.DSC == 1) {
			dml_acquire_dsc(dc, res_ctx, &sec_pipe->stream_res.dsc, pipe_idx);
			ASSERT(sec_pipe->stream_res.dsc);
			if (sec_pipe->stream_res.dsc == NULL)
				return false;
		}
	} else {
		if (pri_pipe->bottom_pipe) {
			ASSERT(pri_pipe->bottom_pipe != sec_pipe);
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe;
			sec_pipe->bottom_pipe->top_pipe = sec_pipe;
		}
		pri_pipe->bottom_pipe = sec_pipe;
		sec_pipe->top_pipe = pri_pipe;

		ASSERT(pri_pipe->plane_state);
	}

	return true;
}

static struct pipe_ctx *dml_find_split_pipe(
		struct dc *dc,
		struct dc_state *context,
		int old_index)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	if (old_index >= 0 && context->res_ctx.pipe_ctx[old_index].stream == NULL) {
		pipe = &context->res_ctx.pipe_ctx[old_index];
		pipe->pipe_idx = old_index;
	}

	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (dc->current_state->res_ctx.pipe_ctx[i].top_pipe == NULL
					&& dc->current_state->res_ctx.pipe_ctx[i].prev_odm_pipe == NULL) {
				if (context->res_ctx.pipe_ctx[i].stream == NULL) {
					pipe = &context->res_ctx.pipe_ctx[i];
					pipe->pipe_idx = i;
					break;
				}
			}
		}

	/*
	 * May need to fix pipes getting tossed from 1 opp to another on flip
	 * Add for debugging transient underflow during topology updates:
	 * ASSERT(pipe);
	 */
	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (context->res_ctx.pipe_ctx[i].stream == NULL) {
				pipe = &context->res_ctx.pipe_ctx[i];
				pipe->pipe_idx = i;
				break;
			}
		}

	return pipe;
}

static void dml_release_dsc(struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct display_stream_compressor **dsc)
{
	int i;

	for (i = 0; i < pool->res_cap->num_dsc; i++)
		if (pool->dscs[i] == *dsc) {
			res_ctx->is_dsc_acquired[i] = false;
			*dsc = NULL;
			break;
		}
}

static int dml_get_num_mpc_splits(struct pipe_ctx *pipe)
{
	int mpc_split_count = 0;
	struct pipe_ctx *other_pipe = pipe->bottom_pipe;

	while (other_pipe && other_pipe->plane_state == pipe->plane_state) {
		mpc_split_count++;
		other_pipe = other_pipe->bottom_pipe;
	}
	other_pipe = pipe->top_pipe;
	while (other_pipe && other_pipe->plane_state == pipe->plane_state) {
		mpc_split_count++;
		other_pipe = other_pipe->top_pipe;
	}

	return mpc_split_count;
}

static bool dml_enough_pipes_for_subvp(struct dc *dc,
		struct dc_state *context)
{
	int i = 0;
	int num_pipes = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->plane_state)
			num_pipes++;
	}

	// Sub-VP only possible if the number of "real" pipes is
	// less than or equal to half the number of available pipes
	if (num_pipes * 2 > dc->res_pool->pipe_count)
		return false;

	return true;
}

static int dml_validate_apply_pipe_split_flags(
		struct dc *dc,
		struct dc_state *context,
		int vlevel,
		int *split,
		bool *merge)
{
	int i, pipe_idx, vlevel_split;
	int plane_count = 0;
	bool force_split = false;
	bool avoid_split = dc->debug.pipe_split_policy == MPC_SPLIT_AVOID;
	struct vba_vars_st *v = &context->bw_ctx.dml.vba;
	int max_mpc_comb = v->maxMpcComb;

	if (context->stream_count > 1) {
		if (dc->debug.pipe_split_policy == MPC_SPLIT_AVOID_MULT_DISP)
			avoid_split = true;
	} else if (dc->debug.force_single_disp_pipe_split)
			force_split = true;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/**
		 * Workaround for avoiding pipe-split in cases where we'd split
		 * planes that are too small, resulting in splits that aren't
		 * valid for the scaler.
		 */
		if (pipe->plane_state &&
		    (pipe->plane_state->dst_rect.width <= 16 ||
		     pipe->plane_state->dst_rect.height <= 16 ||
		     pipe->plane_state->src_rect.width <= 16 ||
		     pipe->plane_state->src_rect.height <= 16))
			avoid_split = true;

		/* TODO: fix dc bugs and remove this split threshold thing */
		if (pipe->stream && !pipe->prev_odm_pipe &&
				(!pipe->top_pipe || pipe->top_pipe->plane_state != pipe->plane_state))
			++plane_count;
	}
	if (plane_count > dc->res_pool->pipe_count / 2)
		avoid_split = true;

	/* W/A: Mode timing with borders may not work well with pipe split, avoid for this corner case */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct dc_crtc_timing timing;

		if (!pipe->stream)
			continue;
		else {
			timing = pipe->stream->timing;
			if (timing.h_border_left + timing.h_border_right
					+ timing.v_border_top + timing.v_border_bottom > 0) {
				avoid_split = true;
				break;
			}
		}
	}

	/* Avoid split loop looks for lowest voltage level that allows most unsplit pipes possible */
	if (avoid_split) {
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			if (!context->res_ctx.pipe_ctx[i].stream)
				continue;

			for (vlevel_split = vlevel; vlevel <= context->bw_ctx.dml.soc.num_states; vlevel++)
				if (v->NoOfDPP[vlevel][0][pipe_idx] == 1 &&
						v->ModeSupport[vlevel][0])
					break;
			/* Impossible to not split this pipe */
			if (vlevel > context->bw_ctx.dml.soc.num_states)
				vlevel = vlevel_split;
			else
				max_mpc_comb = 0;
			pipe_idx++;
		}
		v->maxMpcComb = max_mpc_comb;
	}

	/* Split loop sets which pipe should be split based on dml outputs and dc flags */
	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		int pipe_plane = v->pipe_plane[pipe_idx];
		bool split4mpc = context->stream_count == 1 && plane_count == 1
				&& dc->config.enable_4to1MPC && dc->res_pool->pipe_count >= 4;

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		if (split4mpc || v->NoOfDPP[vlevel][max_mpc_comb][pipe_plane] == 4)
			split[i] = 4;
		else if (force_split || v->NoOfDPP[vlevel][max_mpc_comb][pipe_plane] == 2)
				split[i] = 2;

		if ((pipe->stream->view_format ==
				VIEW_3D_FORMAT_SIDE_BY_SIDE ||
				pipe->stream->view_format ==
				VIEW_3D_FORMAT_TOP_AND_BOTTOM) &&
				(pipe->stream->timing.timing_3d_format ==
				TIMING_3D_FORMAT_TOP_AND_BOTTOM ||
				 pipe->stream->timing.timing_3d_format ==
				TIMING_3D_FORMAT_SIDE_BY_SIDE))
			split[i] = 2;
		if (dc->debug.force_odm_combine & (1 << pipe->stream_res.tg->inst)) {
			split[i] = 2;
			v->ODMCombineEnablePerState[vlevel][pipe_plane] = dm_odm_combine_mode_2to1;
		}
		if (dc->debug.force_odm_combine_4to1 & (1 << pipe->stream_res.tg->inst)) {
			split[i] = 4;
			v->ODMCombineEnablePerState[vlevel][pipe_plane] = dm_odm_combine_mode_4to1;
		}
		/*420 format workaround*/
		if (pipe->stream->timing.h_addressable > 7680 &&
				pipe->stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
			split[i] = 4;
		}

		v->ODMCombineEnabled[pipe_plane] =
			v->ODMCombineEnablePerState[vlevel][pipe_plane];

		if (v->ODMCombineEnabled[pipe_plane] == dm_odm_combine_mode_disabled) {
			if (dml_get_num_mpc_splits(pipe) == 1) {
				/*If need split for mpc but 2 way split already*/
				if (split[i] == 4)
					split[i] = 2; /* 2 -> 4 MPC */
				else if (split[i] == 2)
					split[i] = 0; /* 2 -> 2 MPC */
				else if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state)
					merge[i] = true; /* 2 -> 1 MPC */
			} else if (dml_get_num_mpc_splits(pipe) == 3) {
				/*If need split for mpc but 4 way split already*/
				if (split[i] == 2 && ((pipe->top_pipe && !pipe->top_pipe->top_pipe)
						|| !pipe->bottom_pipe)) {
					merge[i] = true; /* 4 -> 2 MPC */
				} else if (split[i] == 0 && pipe->top_pipe &&
						pipe->top_pipe->plane_state == pipe->plane_state)
					merge[i] = true; /* 4 -> 1 MPC */
				split[i] = 0;
			} else if (dml_get_num_mpc_splits(pipe)) {
				/* ODM -> MPC transition */
				if (pipe->prev_odm_pipe) {
					split[i] = 0;
					merge[i] = true;
				}
			}
		} else {
			if (dml_get_num_mpc_splits(pipe) == 1) {
				/*If need split for odm but 2 way split already*/
				if (split[i] == 4)
					split[i] = 2; /* 2 -> 4 ODM */
				else if (split[i] == 2)
					split[i] = 0; /* 2 -> 2 ODM */
				else if (pipe->prev_odm_pipe) {
					ASSERT(0); /* NOT expected yet */
					merge[i] = true; /* exit ODM */
				}
			} else if (dml_get_num_mpc_splits(pipe) == 3) {
				/*If need split for odm but 4 way split already*/
				if (split[i] == 2 && ((pipe->prev_odm_pipe && !pipe->prev_odm_pipe->prev_odm_pipe)
						|| !pipe->next_odm_pipe)) {
					ASSERT(0); /* NOT expected yet */
					merge[i] = true; /* 4 -> 2 ODM */
				} else if (split[i] == 0 && pipe->prev_odm_pipe) {
					ASSERT(0); /* NOT expected yet */
					merge[i] = true; /* exit ODM */
				}
				split[i] = 0;
			} else if (dml_get_num_mpc_splits(pipe)) {
				/* MPC -> ODM transition */
				ASSERT(0); /* NOT expected yet */
				if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state) {
					split[i] = 0;
					merge[i] = true;
				}
			}
		}

		/* Adjust dppclk when split is forced, do not bother with dispclk */
		if (split[i] != 0 && v->NoOfDPP[vlevel][max_mpc_comb][pipe_idx] == 1)
			v->RequiredDPPCLK[vlevel][max_mpc_comb][pipe_idx] /= 2;
		pipe_idx++;
	}

	return vlevel;
}

static void dml_set_phantom_stream_timing(struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *ref_pipe,
		struct dc_stream_state *phantom_stream)
{
	// phantom_vactive = blackout (latency + margin) + fw_processing_delays + pstate allow width
	uint32_t phantom_vactive_us = context->bw_ctx.dml.soc.dram_clock_change_latency_us + 60 +
					dc->caps.subvp_fw_processing_delay_us +
					dc->caps.subvp_pstate_allow_width_us;
	uint32_t phantom_vactive = ((double)phantom_vactive_us/1000000) *
					(ref_pipe->stream->timing.pix_clk_100hz * 100) /
					(double)ref_pipe->stream->timing.h_total;
	uint32_t phantom_bp = ref_pipe->pipe_dlg_param.vstartup_start;

	phantom_stream->dst.y = 0;
	phantom_stream->dst.height = phantom_vactive;
	phantom_stream->src.y = 0;
	phantom_stream->src.height = phantom_vactive;

	phantom_stream->timing.v_addressable = phantom_vactive;
	phantom_stream->timing.v_front_porch = 1;
	phantom_stream->timing.v_total = phantom_stream->timing.v_addressable +
						phantom_stream->timing.v_front_porch +
						phantom_stream->timing.v_sync_width +
						phantom_bp;
}

static struct dc_stream_state *dml_enable_phantom_stream(struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *ref_pipe)
{
	struct dc_stream_state *phantom_stream = NULL;

	phantom_stream = dc_create_stream_for_sink(ref_pipe->stream->sink);
	phantom_stream->signal = SIGNAL_TYPE_VIRTUAL;
	phantom_stream->dpms_off = true;
	phantom_stream->mall_stream_config.type = SUBVP_PHANTOM;
	phantom_stream->mall_stream_config.paired_stream = ref_pipe->stream;
	ref_pipe->stream->mall_stream_config.type = SUBVP_MAIN;
	ref_pipe->stream->mall_stream_config.paired_stream = phantom_stream;

	/* stream has limited viewport and small timing */
	memcpy(&phantom_stream->timing, &ref_pipe->stream->timing, sizeof(phantom_stream->timing));
	memcpy(&phantom_stream->src, &ref_pipe->stream->src, sizeof(phantom_stream->src));
	memcpy(&phantom_stream->dst, &ref_pipe->stream->dst, sizeof(phantom_stream->dst));
	dml_set_phantom_stream_timing(dc, context, ref_pipe, phantom_stream);

	dc_add_stream_to_ctx(dc, context, phantom_stream);
	dc->hwss.apply_ctx_to_hw(dc, context);
	return phantom_stream;
}

static void dml_enable_phantom_plane(struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *phantom_stream,
		struct pipe_ctx *main_pipe)
{
	struct dc_plane_state *phantom_plane = NULL;
	struct dc_plane_state *prev_phantom_plane = NULL;
	struct pipe_ctx *curr_pipe = main_pipe;

	while (curr_pipe) {
		if (curr_pipe->top_pipe && curr_pipe->top_pipe->plane_state == curr_pipe->plane_state)
			phantom_plane = prev_phantom_plane;
		else
			phantom_plane = dc_create_plane_state(dc);

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
		/* Currently compat_level is undefined in dc_state
		* phantom_plane->compat_level = curr_pipe->plane_state->compat_level;
		*/
		phantom_plane->format = curr_pipe->plane_state->format;
		phantom_plane->rotation = curr_pipe->plane_state->rotation;
		phantom_plane->visible = curr_pipe->plane_state->visible;

		/* Shadow pipe has small viewport. */
		phantom_plane->clip_rect.y = 0;
		phantom_plane->clip_rect.height = phantom_stream->timing.v_addressable;

		dc_add_plane_to_context(dc, phantom_stream, phantom_plane, context);

		curr_pipe = curr_pipe->bottom_pipe;
		prev_phantom_plane = phantom_plane;
	}
}

static void dml_add_phantom_pipes(struct dc *dc, struct dc_state *context)
{
	int i = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct dc_stream_state *ref_stream = pipe->stream;
		// Only construct phantom stream for top pipes that have plane enabled
		if (!pipe->top_pipe && pipe->plane_state && pipe->stream &&
				pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			struct dc_stream_state *phantom_stream = NULL;

			phantom_stream = dml_enable_phantom_stream(dc, context, pipe);
			dml_enable_phantom_plane(dc, context, phantom_stream, pipe);
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state && pipe->stream &&
				pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			pipe->stream->use_dynamic_meta = false;
			pipe->plane_state->flip_immediate = false;
			if (!resource_build_scaling_params(pipe)) {
				// Log / remove phantom pipes since failed to build scaling params
			}
		}
	}
}

static void dml_remove_phantom_pipes(struct dc *dc, struct dc_state *context)
{
	int i;
	bool removed_pipe = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		// build scaling params for phantom pipes
		if (pipe->plane_state && pipe->stream && pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			dc_rem_all_planes_for_stream(dc, pipe->stream, context);
			dc_remove_stream_from_ctx(dc, context, pipe->stream);
			removed_pipe = true;
		}

		// Clear all phantom stream info
		if (pipe->stream) {
			pipe->stream->mall_stream_config.type = SUBVP_NONE;
			pipe->stream->mall_stream_config.paired_stream = NULL;
		}
	}
	if (removed_pipe)
		dc->hwss.apply_ctx_to_hw(dc, context);
}

/*
 * If the input state contains no upstream planes for a particular pipe (i.e. only timing)
 * we need to populate some "conservative" plane information as DML cannot handle "no planes"
 */
static void populate_default_plane_from_timing(const struct dc_crtc_timing *timing, struct _vcs_dpi_display_pipe_params_st *pipe)
{
	pipe->src.is_hsplit = pipe->dest.odm_combine != dm_odm_combine_mode_disabled;
	pipe->src.source_scan = dm_horz;
	pipe->src.sw_mode = dm_sw_4kb_s;
	pipe->src.macro_tile_size = dm_64k_tile;
	pipe->src.viewport_width = timing->h_addressable;
	if (pipe->src.viewport_width > 1920)
		pipe->src.viewport_width = 1920;
	pipe->src.viewport_height = timing->v_addressable;
	if (pipe->src.viewport_height > 1080)
		pipe->src.viewport_height = 1080;
	pipe->src.surface_height_y = pipe->src.viewport_height;
	pipe->src.surface_width_y = pipe->src.viewport_width;
	pipe->src.surface_height_c = pipe->src.viewport_height;
	pipe->src.surface_width_c = pipe->src.viewport_width;
	pipe->src.data_pitch = ((pipe->src.viewport_width + 255) / 256) * 256;
	pipe->src.source_format = dm_444_32;
	pipe->dest.recout_width = pipe->src.viewport_width;
	pipe->dest.recout_height = pipe->src.viewport_height;
	pipe->dest.full_recout_width = pipe->dest.recout_width;
	pipe->dest.full_recout_height = pipe->dest.recout_height;
	pipe->scale_ratio_depth.lb_depth = dm_lb_16;
	pipe->scale_ratio_depth.hscl_ratio = 1.0;
	pipe->scale_ratio_depth.vscl_ratio = 1.0;
	pipe->scale_ratio_depth.scl_enable = 0;
	pipe->scale_taps.htaps = 1;
	pipe->scale_taps.vtaps = 1;
	pipe->dest.vtotal_min = timing->v_total;
	pipe->dest.vtotal_max = timing->v_total;

	if (pipe->dest.odm_combine == dm_odm_combine_mode_2to1) {
		pipe->src.viewport_width /= 2;
		pipe->dest.recout_width /= 2;
	} else if (pipe->dest.odm_combine == dm_odm_combine_mode_4to1) {
		pipe->src.viewport_width /= 4;
		pipe->dest.recout_width /= 4;
	}

	pipe->src.dcc = false;
	pipe->src.dcc_rate = 1;
}

/*
 * If the pipe is not blending (i.e. pipe_ctx->top pipe == null) then its
 * hsplit group is equal to its own pipe ID
 * Otherwise, all pipes part of the same blending tree have the same hsplit group
 * ID as the top most pipe
 *
 * If the pipe ctx is ODM combined, then similar logic follows
 */
static void populate_hsplit_group_from_dc_pipe_ctx (const struct pipe_ctx *dc_pipe_ctx, struct _vcs_dpi_display_e2e_pipe_params_st *e2e_pipe)
{
	e2e_pipe->pipe.src.hsplit_grp = dc_pipe_ctx->pipe_idx;

	if (dc_pipe_ctx->top_pipe && dc_pipe_ctx->top_pipe->plane_state
			== dc_pipe_ctx->plane_state) {
		struct pipe_ctx *first_pipe = dc_pipe_ctx->top_pipe;
		int split_idx = 0;

		while (first_pipe->top_pipe && first_pipe->top_pipe->plane_state
				== dc_pipe_ctx->plane_state) {
			first_pipe = first_pipe->top_pipe;
			split_idx++;
		}

		/* Treat 4to1 mpc combine as an mpo of 2 2-to-1 combines */
		if (split_idx == 0)
			e2e_pipe->pipe.src.hsplit_grp = first_pipe->pipe_idx;
		else if (split_idx == 1)
			e2e_pipe->pipe.src.hsplit_grp = dc_pipe_ctx->pipe_idx;
		else if (split_idx == 2)
			e2e_pipe->pipe.src.hsplit_grp = dc_pipe_ctx->top_pipe->pipe_idx;

	} else if (dc_pipe_ctx->prev_odm_pipe) {
		struct pipe_ctx *first_pipe = dc_pipe_ctx->prev_odm_pipe;

		while (first_pipe->prev_odm_pipe)
			first_pipe = first_pipe->prev_odm_pipe;
		e2e_pipe->pipe.src.hsplit_grp = first_pipe->pipe_idx;
	}
}

static void populate_dml_from_dc_pipe_ctx (const struct pipe_ctx *dc_pipe_ctx, struct _vcs_dpi_display_e2e_pipe_params_st *e2e_pipe, int always_scale)
{
	const struct dc_plane_state *pln = dc_pipe_ctx->plane_state;
	const struct scaler_data *scl = &dc_pipe_ctx->plane_res.scl_data;

	e2e_pipe->pipe.src.immediate_flip = pln->flip_immediate;
	e2e_pipe->pipe.src.is_hsplit = (dc_pipe_ctx->bottom_pipe && dc_pipe_ctx->bottom_pipe->plane_state == pln)
			|| (dc_pipe_ctx->top_pipe && dc_pipe_ctx->top_pipe->plane_state == pln)
			|| e2e_pipe->pipe.dest.odm_combine != dm_odm_combine_mode_disabled;

	/* stereo is not split */
	if (pln->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE ||
		pln->stereo_format == PLANE_STEREO_FORMAT_TOP_AND_BOTTOM) {
		e2e_pipe->pipe.src.is_hsplit = false;
		e2e_pipe->pipe.src.hsplit_grp = dc_pipe_ctx->pipe_idx;
	}

	e2e_pipe->pipe.src.source_scan = pln->rotation == ROTATION_ANGLE_90
			|| pln->rotation == ROTATION_ANGLE_270 ? dm_vert : dm_horz;
	e2e_pipe->pipe.src.viewport_y_y = scl->viewport.y;
	e2e_pipe->pipe.src.viewport_y_c = scl->viewport_c.y;
	e2e_pipe->pipe.src.viewport_width = scl->viewport.width;
	e2e_pipe->pipe.src.viewport_width_c = scl->viewport_c.width;
	e2e_pipe->pipe.src.viewport_height = scl->viewport.height;
	e2e_pipe->pipe.src.viewport_height_c = scl->viewport_c.height;
	e2e_pipe->pipe.src.viewport_width_max = pln->src_rect.width;
	e2e_pipe->pipe.src.viewport_height_max = pln->src_rect.height;
	e2e_pipe->pipe.src.surface_width_y = pln->plane_size.surface_size.width;
	e2e_pipe->pipe.src.surface_height_y = pln->plane_size.surface_size.height;
	e2e_pipe->pipe.src.surface_width_c = pln->plane_size.chroma_size.width;
	e2e_pipe->pipe.src.surface_height_c = pln->plane_size.chroma_size.height;

	if (pln->format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA
			|| pln->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		e2e_pipe->pipe.src.data_pitch = pln->plane_size.surface_pitch;
		e2e_pipe->pipe.src.data_pitch_c = pln->plane_size.chroma_pitch;
		e2e_pipe->pipe.src.meta_pitch = pln->dcc.meta_pitch;
		e2e_pipe->pipe.src.meta_pitch_c = pln->dcc.meta_pitch_c;
	} else {
		e2e_pipe->pipe.src.data_pitch = pln->plane_size.surface_pitch;
		e2e_pipe->pipe.src.meta_pitch = pln->dcc.meta_pitch;
	}
	e2e_pipe->pipe.src.dcc = pln->dcc.enable;
	e2e_pipe->pipe.src.dcc_rate = 1;
	e2e_pipe->pipe.dest.recout_width = scl->recout.width;
	e2e_pipe->pipe.dest.recout_height = scl->recout.height;
	e2e_pipe->pipe.dest.full_recout_height = scl->recout.height;
	e2e_pipe->pipe.dest.full_recout_width = scl->recout.width;
	if (e2e_pipe->pipe.dest.odm_combine == dm_odm_combine_mode_2to1)
		e2e_pipe->pipe.dest.full_recout_width *= 2;
	else if (e2e_pipe->pipe.dest.odm_combine == dm_odm_combine_mode_4to1)
		e2e_pipe->pipe.dest.full_recout_width *= 4;
	else {
		struct pipe_ctx *split_pipe = dc_pipe_ctx->bottom_pipe;

		while (split_pipe && split_pipe->plane_state == pln) {
			e2e_pipe->pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
			split_pipe = split_pipe->bottom_pipe;
		}
		split_pipe = dc_pipe_ctx->top_pipe;
		while (split_pipe && split_pipe->plane_state == pln) {
			e2e_pipe->pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
			split_pipe = split_pipe->top_pipe;
		}
	}

	e2e_pipe->pipe.scale_ratio_depth.lb_depth = dm_lb_16;
	e2e_pipe->pipe.scale_ratio_depth.hscl_ratio = (double) scl->ratios.horz.value / (1ULL<<32);
	e2e_pipe->pipe.scale_ratio_depth.hscl_ratio_c = (double) scl->ratios.horz_c.value / (1ULL<<32);
	e2e_pipe->pipe.scale_ratio_depth.vscl_ratio = (double) scl->ratios.vert.value / (1ULL<<32);
	e2e_pipe->pipe.scale_ratio_depth.vscl_ratio_c = (double) scl->ratios.vert_c.value / (1ULL<<32);
	e2e_pipe->pipe.scale_ratio_depth.scl_enable =
			scl->ratios.vert.value != dc_fixpt_one.value
			|| scl->ratios.horz.value != dc_fixpt_one.value
			|| scl->ratios.vert_c.value != dc_fixpt_one.value
			|| scl->ratios.horz_c.value != dc_fixpt_one.value /*Lb only or Full scl*/
			|| always_scale; /*support always scale*/
	e2e_pipe->pipe.scale_taps.htaps = scl->taps.h_taps;
	e2e_pipe->pipe.scale_taps.htaps_c = scl->taps.h_taps_c;
	e2e_pipe->pipe.scale_taps.vtaps = scl->taps.v_taps;
	e2e_pipe->pipe.scale_taps.vtaps_c = scl->taps.v_taps_c;

	/* Currently compat_level is not defined. Commenting it until further resolution
	 * if (pln->compat_level == DC_LEGACY_TILING_ADDR_GEN_TWO) {
		swizzle_to_dml_params(pln->tiling_info.gfx9.swizzle,
				&e2e_pipe->pipe.src.sw_mode);
		e2e_pipe->pipe.src.macro_tile_size =
				swizzle_mode_to_macro_tile_size(pln->tiling_info.gfx9.swizzle);
	} else {
		gfx10array_mode_to_dml_params(pln->tiling_info.gfx10compatible.array_mode,
				pln->compat_level,
				&e2e_pipe->pipe.src.sw_mode);
		e2e_pipe->pipe.src.macro_tile_size = dm_4k_tile;
	}*/

	e2e_pipe->pipe.src.source_format = dc_source_format_to_dml_source_format(pln->format);
}

static void populate_dml_cursor_parameters_from_dc_pipe_ctx (const struct pipe_ctx *dc_pipe_ctx, struct _vcs_dpi_display_e2e_pipe_params_st *e2e_pipe)
{
	/*
	* For graphic plane, cursor number is 1, nv12 is 0
	* bw calculations due to cursor on/off
	*/
	if (dc_pipe_ctx->plane_state &&
			(dc_pipe_ctx->plane_state->address.type == PLN_ADDR_TYPE_VIDEO_PROGRESSIVE ||
			dc_pipe_ctx->stream->mall_stream_config.type == SUBVP_PHANTOM))
		e2e_pipe->pipe.src.num_cursors = 0;
	else
		e2e_pipe->pipe.src.num_cursors = 1;

	e2e_pipe->pipe.src.cur0_src_width = 256;
	e2e_pipe->pipe.src.cur0_bpp = dm_cur_32bit;
}

static int populate_dml_pipes_from_context_base(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		bool fast_validate)
{
	int pipe_cnt, i;
	bool synchronized_vblank = true;
	struct resource_context *res_ctx = &context->res_ctx;

	for (i = 0, pipe_cnt = -1; i < dc->res_pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		if (pipe_cnt < 0) {
			pipe_cnt = i;
			continue;
		}

		if (res_ctx->pipe_ctx[pipe_cnt].stream == res_ctx->pipe_ctx[i].stream)
			continue;

		if (dc->debug.disable_timing_sync ||
			(!resource_are_streams_timing_synchronizable(
				res_ctx->pipe_ctx[pipe_cnt].stream,
				res_ctx->pipe_ctx[i].stream) &&
			!resource_are_vblanks_synchronizable(
				res_ctx->pipe_ctx[pipe_cnt].stream,
				res_ctx->pipe_ctx[i].stream))) {
			synchronized_vblank = false;
			break;
		}
	}

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing = &res_ctx->pipe_ctx[i].stream->timing;

		struct audio_check aud_check = {0};
		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		/* todo:
		pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = 0;
		pipes[pipe_cnt].pipe.src.dcc = 0;
		pipes[pipe_cnt].pipe.src.vm = 0;*/

		pipes[pipe_cnt].clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;

		pipes[pipe_cnt].dout.dsc_enable = res_ctx->pipe_ctx[i].stream->timing.flags.DSC;
		/* todo: rotation?*/
		pipes[pipe_cnt].dout.dsc_slices = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.num_slices_h;
		if (res_ctx->pipe_ctx[i].stream->use_dynamic_meta) {
			pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = true;
			/* 1/2 vblank */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_lines_before_active =
				(timing->v_total - timing->v_addressable
					- timing->v_border_top - timing->v_border_bottom) / 2;
			/* 36 bytes dp, 32 hdmi */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_xmit_bytes =
				dc_is_dp_signal(res_ctx->pipe_ctx[i].stream->signal) ? 36 : 32;
		}
		pipes[pipe_cnt].pipe.dest.synchronized_vblank_all_planes = synchronized_vblank;

		dc_timing_to_dml_timing(timing, &pipes[pipe_cnt].pipe.dest);
		pipes[pipe_cnt].pipe.dest.vtotal_min = res_ctx->pipe_ctx[i].stream->adjust.v_total_min;
		pipes[pipe_cnt].pipe.dest.vtotal_max = res_ctx->pipe_ctx[i].stream->adjust.v_total_max;

		pipes[pipe_cnt].pipe.dest.otg_inst = res_ctx->pipe_ctx[i].stream_res.tg->inst;

		pipes[pipe_cnt].pipe.dest.odm_combine = get_dml_odm_combine(&res_ctx->pipe_ctx[i]);

		populate_hsplit_group_from_dc_pipe_ctx(&res_ctx->pipe_ctx[i], &pipes[pipe_cnt]);

		pipes[pipe_cnt].dout.dp_lanes = 4;
		pipes[pipe_cnt].dout.is_virtual = 0;
		pipes[pipe_cnt].dout.output_type = get_dml_output_type(res_ctx->pipe_ctx[i].stream->signal);
		if (pipes[pipe_cnt].dout.output_type < 0) {
			pipes[pipe_cnt].dout.output_type = dm_dp;
			pipes[pipe_cnt].dout.is_virtual = 1;
		}

		populate_color_depth_and_encoding_from_timing(&res_ctx->pipe_ctx[i].stream->timing, &pipes[pipe_cnt].dout);

		if (res_ctx->pipe_ctx[i].stream->timing.flags.DSC)
			pipes[pipe_cnt].dout.output_bpp = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.bits_per_pixel / 16.0;

		/* todo: default max for now, until there is logic reflecting this in dc*/
		pipes[pipe_cnt].dout.dsc_input_bpc = 12;
		/*fill up the audio sample rate (unit in kHz)*/
		get_audio_check(&res_ctx->pipe_ctx[i].stream->audio_info, &aud_check);
		pipes[pipe_cnt].dout.max_audio_sample_rate = aud_check.max_audiosample_rate / 1000;

		populate_dml_cursor_parameters_from_dc_pipe_ctx(&res_ctx->pipe_ctx[i], &pipes[pipe_cnt]);

		if (!res_ctx->pipe_ctx[i].plane_state) {
			populate_default_plane_from_timing(timing, &pipes[pipe_cnt].pipe);
		} else {
			populate_dml_from_dc_pipe_ctx(&res_ctx->pipe_ctx[i], &pipes[pipe_cnt], dc->debug.always_scale);
		}

		pipe_cnt++;
	}

	/* populate writeback information */
	if (dc->res_pool)
		dc->res_pool->funcs->populate_dml_writeback_from_context(dc, res_ctx, pipes);

	return pipe_cnt;
}

static int dml_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;

	populate_dml_pipes_from_context_base(dc, context, pipes, fast_validate);

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;
		pipe = &res_ctx->pipe_ctx[i];
		timing = &pipe->stream->timing;

		pipes[pipe_cnt].pipe.src.gpuvm = true;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_luma = 0;
		pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_chroma = 0;
		pipes[pipe_cnt].pipe.dest.vfront_porch = timing->v_front_porch;

		pipes[pipe_cnt].dout.dsc_input_bpc = 0;
		if (pipes[pipe_cnt].dout.dsc_enable) {
			switch (timing->display_color_depth) {
			case COLOR_DEPTH_888:
				pipes[pipe_cnt].dout.dsc_input_bpc = 8;
				break;
			case COLOR_DEPTH_101010:
				pipes[pipe_cnt].dout.dsc_input_bpc = 10;
				break;
			case COLOR_DEPTH_121212:
				pipes[pipe_cnt].dout.dsc_input_bpc = 12;
				break;
			default:
				ASSERT(0);
				break;
			}
		}
		pipe_cnt++;
	}
	dc->config.enable_4to1MPC = false;
	if (pipe_cnt == 1 && pipe->plane_state && !dc->debug.disable_z9_mpc) {
		if (is_dual_plane(pipe->plane_state->format)
				&& pipe->plane_state->src_rect.width <= 1920 && pipe->plane_state->src_rect.height <= 1080) {
			dc->config.enable_4to1MPC = true;
		} else if (!is_dual_plane(pipe->plane_state->format)) {
			context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
			pipes[0].pipe.src.unbounded_req_mode = true;
		}
	}

	return pipe_cnt;
}

static void dml_full_validate_bw_helper(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *vlevel,
		int *split,
		bool *merge,
		int *pipe_cnt)
{
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	/*
	 * DML favors voltage over p-state, but we're more interested in
	 * supporting p-state over voltage. We can't support p-state in
	 * prefetch mode > 0 so try capping the prefetch mode to start.
	 */
	context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
		dm_allow_self_refresh_and_mclk_switch;
	*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
	/* This may adjust vlevel and maxMpcComb */
	if (*vlevel < context->bw_ctx.dml.soc.num_states)
		*vlevel = dml_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);

	/* Conditions for setting up phantom pipes for SubVP:
	 * 1. Not force disable SubVP
	 * 2. Full update (i.e. !fast_validate)
	 * 3. Enough pipes are available to support SubVP (TODO: Which pipes will use VACTIVE / VBLANK / SUBVP?)
	 * 4. Display configuration passes validation
	 * 5. (Config doesn't support MCLK in VACTIVE/VBLANK || dc->debug.force_subvp_mclk_switch)
	 */
	if (!dc->debug.force_disable_subvp &&
			dml_enough_pipes_for_subvp(dc, context) &&
			*vlevel < context->bw_ctx.dml.soc.num_states &&
			(vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported ||
			dc->debug.force_subvp_mclk_switch)) {

		dml_add_phantom_pipes(dc, context);

		 /* Create input to DML based on new context which includes phantom pipes
		  * TODO: Input to DML should mark which pipes are phantom
		  */
		*pipe_cnt = dml_populate_dml_pipes_from_context(dc, context, pipes, false);
		*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
		if (*vlevel < context->bw_ctx.dml.soc.num_states) {
			memset(split, 0, MAX_PIPES * sizeof(*split));
			memset(merge, 0, MAX_PIPES * sizeof(*merge));
			*vlevel = dml_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);
		}

		// If SubVP pipe config is unsupported (or cannot be used for UCLK switching)
		// remove phantom pipes and repopulate dml pipes
		if (*vlevel == context->bw_ctx.dml.soc.num_states ||
				vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported) {
			dml_remove_phantom_pipes(dc, context);
			*pipe_cnt = dml_populate_dml_pipes_from_context(dc, context, pipes, false);
		}
	}
}

static void dcn20_adjust_adaptive_sync_v_startup(
		const struct dc_crtc_timing *dc_crtc_timing, int *vstartup_start)
{
	struct dc_crtc_timing patched_crtc_timing;
	uint32_t asic_blank_end   = 0;
	uint32_t asic_blank_start = 0;
	uint32_t newVstartup	  = 0;

	patched_crtc_timing = *dc_crtc_timing;

	if (patched_crtc_timing.flags.INTERLACE == 1) {
		if (patched_crtc_timing.v_front_porch < 2)
			patched_crtc_timing.v_front_porch = 2;
	} else {
		if (patched_crtc_timing.v_front_porch < 1)
			patched_crtc_timing.v_front_porch = 1;
	}

	/* blank_start = frame end - front porch */
	asic_blank_start = patched_crtc_timing.v_total -
					patched_crtc_timing.v_front_porch;

	/* blank_end = blank_start - active */
	asic_blank_end = asic_blank_start -
					patched_crtc_timing.v_border_bottom -
					patched_crtc_timing.v_addressable -
					patched_crtc_timing.v_border_top;

	newVstartup = asic_blank_end + (patched_crtc_timing.v_total - asic_blank_start);

	*vstartup_start = ((newVstartup > *vstartup_start) ? newVstartup : *vstartup_start);
}

static bool is_dp_128b_132b_signal(struct pipe_ctx *pipe_ctx)
{
	return (pipe_ctx->stream_res.hpo_dp_stream_enc &&
			pipe_ctx->link_res.hpo_dp_link_enc &&
			dc_is_dp_signal(pipe_ctx->stream->signal));
}

static bool is_dtbclk_required(struct dc *dc, struct dc_state *context)
{
	int i;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
#if defined (CONFIG_DRM_AMD_DC_DP2_0)
		if (is_dp_128b_132b_signal(&context->res_ctx.pipe_ctx[i]))
			return true;
#endif
	}
	return false;
}

static void dml_update_soc_for_wm_a(struct dc *dc, struct dc_state *context)
{
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us;
	}
}

static bool dml_internal_validate(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *vlevel_out,
		bool fast_validate)
{
	bool out = false;
	bool repopulate_pipes = false;
	int split[MAX_PIPES] = { 0 };
	bool merge[MAX_PIPES] = { false };
	bool newly_split[MAX_PIPES] = { false };
	int pipe_cnt, i, pipe_idx, vlevel;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	ASSERT(pipes);
	if (!pipes)
		return false;

	// For each full update, remove all existing phantom pipes first
	dml_remove_phantom_pipes(dc, context);

	dml_update_soc_for_wm_a(dc, context);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			// On initial pass through DML, we intend to use MALL for SS on all
			// (non-PSR) surfaces with none using MALL for P-State
			// 'mall_plane_config': is not a member of 'dc_plane_state' - commenting it out till mall_plane_config gets supported in dc_plant_state
			//if (pipe->stream && pipe->stream->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED)
			//	pipe->plane_state->mall_plane_config.use_mall_for_ss = true;
		}
	}
	pipe_cnt = dml_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}

	dml_log_pipe_params(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (!fast_validate) {
		dml_full_validate_bw_helper(dc, context, pipes, &vlevel, split, merge, &pipe_cnt);
	}

	if (fast_validate || vlevel == context->bw_ctx.dml.soc.num_states ||
			vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported) {
		/*
		 * If mode is unsupported or there's still no p-state support then
		 * fall back to favoring voltage.
		 *
		 * We don't actually support prefetch mode 2, so require that we
		 * at least support prefetch mode 1.
		 */
		context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
			dm_allow_self_refresh;

		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		if (vlevel < context->bw_ctx.dml.soc.num_states) {
			memset(split, 0, sizeof(split));
			memset(merge, 0, sizeof(merge));
			vlevel = dml_validate_apply_pipe_split_flags(dc, context, vlevel, split, merge);
		}
	}

	dml_log_mode_support_params(&context->bw_ctx.dml);

	if (vlevel == context->bw_ctx.dml.soc.num_states)
		goto validate_fail;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *mpo_pipe = pipe->bottom_pipe;

		if (!pipe->stream)
			continue;

		/* We only support full screen mpo with ODM */
		if (vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled
				&& pipe->plane_state && mpo_pipe
				&& memcmp(&mpo_pipe->plane_res.scl_data.recout,
						&pipe->plane_res.scl_data.recout,
						sizeof(struct rect)) != 0) {
			ASSERT(mpo_pipe->plane_state != pipe->plane_state);
			goto validate_fail;
		}
		pipe_idx++;
	}

	/* merge pipes if necessary */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/*skip pipes that don't need merging*/
		if (!merge[i])
			continue;

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
				dml_release_dsc(&context->res_ctx, dc->res_pool, &pipe->stream_res.dsc);
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
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
			repopulate_pipes = true;
		} else
			ASSERT(0); /* Should never try to merge master pipe */

	}

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = NULL;
		bool odm;
		int old_index = -1;

		if (!pipe->stream || newly_split[i])
			continue;

		pipe_idx++;
		odm = vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled;

		if (!pipe->plane_state && !odm)
			continue;

		if (split[i]) {
			if (odm) {
				if (split[i] == 4 && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
				else if (old_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->pipe_idx;
			} else {
				if (split[i] == 4 && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->bottom_pipe->pipe_idx;
				else if (old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->pipe_idx;
			}
			hsplit_pipe = dml_find_split_pipe(dc, context, old_index);
			ASSERT(hsplit_pipe);
			if (!hsplit_pipe)
				goto validate_fail;

			if (!dml_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, hsplit_pipe, odm))
				goto validate_fail;

			newly_split[hsplit_pipe->pipe_idx] = true;
			repopulate_pipes = true;
		}
		if (split[i] == 4) {
			struct pipe_ctx *pipe_4to1;

			if (odm && old_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dml_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dml_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;

			if (odm && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe
					&& old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dml_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dml_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					hsplit_pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;
		}
		if (odm)
			dml_build_mapped_resource(dc, context, pipe->stream);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!resource_build_scaling_params(pipe))
				goto validate_fail;
		}
	}

	/* Actual dsc count per stream dsc validation*/
	if (!dml_validate_dsc(dc, context)) {
		vba->ValidationStatus[vba->soc.num_states] = DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	if (repopulate_pipes)
		pipe_cnt = dml_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);
	*vlevel_out = vlevel;
	*pipe_cnt_out = pipe_cnt;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}

static void dml_calculate_dlg_params(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, pipe_idx;
	int plane_count;

	/* Writeback MCIF_WB arbitration parameters */
	if (dc->res_pool)
		dc->res_pool->funcs->set_mcif_arb_params(dc, context, pipes, pipe_cnt);

	context->bw_ctx.bw.dcn.clk.dispclk_khz = context->bw_ctx.dml.vba.DISPCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = context->bw_ctx.dml.vba.DCFCLK * 1000;
	context->bw_ctx.bw.dcn.clk.socclk_khz = context->bw_ctx.dml.vba.SOCCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = context->bw_ctx.dml.vba.DRAMSpeed * 1000 / 16;
	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = context->bw_ctx.dml.vba.DCFCLKDeepSleep * 1000;
	context->bw_ctx.bw.dcn.clk.fclk_khz = context->bw_ctx.dml.vba.FabricClock * 1000;
	context->bw_ctx.bw.dcn.clk.p_state_change_support =
		context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb]
							!= dm_dram_clock_change_unsupported;

	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;
	/* 'z9_support': is not a member of 'dc_clocks' - Commenting out till we have this support in dc_clocks
	 * context->bw_ctx.bw.dcn.clk.z9_support = (context->bw_ctx.dml.vba.StutterPeriod > 5000.0) ?
			DCN_Z9_SUPPORT_ALLOW : DCN_Z9_SUPPORT_DISALLOW;
	*/
	plane_count = 0;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].plane_state)
			plane_count++;
	}

	/* Commented out as per above error for now.
	if (plane_count == 0)
		context->bw_ctx.bw.dcn.clk.z9_support = DCN_Z9_SUPPORT_ALLOW;
	*/
	context->bw_ctx.bw.dcn.clk.dtbclk_en = is_dtbclk_required(dc, context);
	/* TODO : Uncomment the below line and make changes
	 * as per DML nomenclature once it is available.
	 * context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = context->bw_ctx.dml.vba.fclk_pstate_support;
	 */

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		pipes[pipe_idx].pipe.dest.vstartup_start = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_offset = get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_width = get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vready_offset = get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		if (context->res_ctx.pipe_ctx[i].stream->mall_stream_config.type == SUBVP_PHANTOM) {
			// Phantom pipe requires that DET_SIZE = 0 and no unbounded requests
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb = 0;
			context->res_ctx.pipe_ctx[i].unbounded_req = false;
		} else {
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb = context->bw_ctx.dml.ip.det_buffer_size_kbytes;
			context->res_ctx.pipe_ctx[i].unbounded_req = pipes[pipe_idx].pipe.src.unbounded_req_mode;
		}

		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz =
						pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		pipe_idx++;
	}
	/*save a original dppclock copy*/
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dppclk_mhz * 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dispclk_mhz * 1000;
	context->bw_ctx.bw.dcn.compbuf_size_kb = context->bw_ctx.dml.ip.config_return_buffer_size_in_kbytes
						- context->bw_ctx.dml.ip.det_buffer_size_kbytes * pipe_idx;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		bool cstate_en = context->bw_ctx.dml.vba.PrefetchMode[vlevel][context->bw_ctx.dml.vba.maxMpcComb] != 2;

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		context->bw_ctx.dml.funcs.rq_dlg_get_dlg_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].dlg_regs,
				&context->res_ctx.pipe_ctx[i].ttu_regs,
				pipes,
				pipe_cnt,
				pipe_idx,
				cstate_en,
				context->bw_ctx.bw.dcn.clk.p_state_change_support,
				false, false, true);

		context->bw_ctx.dml.funcs.rq_dlg_get_rq_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].rq_regs,
				&pipes[pipe_idx].pipe);
		pipe_idx++;
	}
}

static void dml_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, pipe_idx, vlevel_temp = 0;

	double dcfclk = context->bw_ctx.dml.soc.clock_limits[0].dcfclk_mhz;
	double dcfclk_from_validation = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	unsigned int min_dram_speed_mts = context->bw_ctx.dml.vba.DRAMSpeed;
	bool pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] !=
			dm_dram_clock_change_unsupported;

	/* Set B:
	 * For Set B calculations use clocks from clock_limits[2] when available i.e. when SMU is present,
	 * otherwise use arbitrary low value from spreadsheet for DCFCLK as lower is safer for watermark
	 * calculations to cover bootup clocks.
	 * DCFCLK: soc.clock_limits[2] when available
	 * UCLK: soc.clock_limits[2] when available
	 */
	if (context->bw_ctx.dml.soc.num_states > 2) {
		vlevel_temp = 2;
		dcfclk = context->bw_ctx.dml.soc.clock_limits[2].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.fclk_pstate_change_ns = get_wm_fclk_pstate(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.b.usr_retraining_ns = get_wm_usr_retraining(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Temporary, to have some fclk_pstate_change_ns and usr_retraining_ns wm values until DML is implemented */
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.fclk_pstate_change_ns = context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns / 4;
	context->bw_ctx.bw.dcn.watermarks.b.usr_retraining_ns = context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns / 8;

	/* Set D:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW when available
	 * UCLK  : Min, as reported by PM FW when available
	 * sr_enter_exit/sr_exit should be lower than used for DRAM (TBD after bringup or later, use as decided in Clk Mgr)
	 */

	if (context->bw_ctx.dml.soc.num_states > 2) {
		vlevel_temp = 0;
		dcfclk = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.fclk_pstate_change_ns = get_wm_fclk_pstate(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.d.usr_retraining_ns = get_wm_usr_retraining(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Temporary, to have some fclk_pstate_change_ns and usr_retraining_ns wm values until DML is implemented */
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.fclk_pstate_change_ns = context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns / 4;
	context->bw_ctx.bw.dcn.watermarks.d.usr_retraining_ns = context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns / 8;

	/* Set C, for Dummy P-State:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW, when available
	 * UCLK  : Min,  as reported by PM FW, when available
	 * pstate latency as per UCLK state dummy pstate latency
	 */
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid) {
		unsigned int min_dram_speed_mts_margin = 160;

		if ((!pstate_en))
			min_dram_speed_mts = dc->clk_mgr->bw_params->clk_table.entries[dc->clk_mgr->bw_params->clk_table.num_entries - 1].memclk_mhz * 16;

		/* find largest table entry that is lower than dram speed, but lower than DPM0 still uses DPM0 */
		for (i = 3; i > 0; i--)
			if (min_dram_speed_mts + min_dram_speed_mts_margin > dc->clk_mgr->bw_params->dummy_pstate_table[i].dram_speed_mts)
				break;

		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->dummy_pstate_table[i].dummy_pstate_latency_us;
		context->bw_ctx.dml.soc.dummy_pstate_latency_us = dc->clk_mgr->bw_params->dummy_pstate_table[i].dummy_pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.fclk_pstate_change_ns = get_wm_fclk_pstate(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	//context->bw_ctx.bw.dcn.watermarks.c.usr_retraining_ns = get_wm_usr_retraining(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Temporary, to have some fclk_pstate_change_ns and usr_retraining_ns wm values until DML is implemented */
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.fclk_pstate_change_ns = context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns / 4;
	context->bw_ctx.bw.dcn.watermarks.c.usr_retraining_ns = context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns / 8;

	if ((!pstate_en) && (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid)) {
		/* The only difference between A and C is p-state latency, if p-state is not supported
		 * with full p-state latency we want to calculate DLG based on dummy p-state latency,
		 * Set A p-state watermark set to 0 previously, when p-state unsupported, for now keep as previous implementation.
		 */
		context->bw_ctx.bw.dcn.watermarks.a = context->bw_ctx.bw.dcn.watermarks.c;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 0;
	} else {
		/* Set A:
		 * All clocks min.
		 * DCFCLK: Min, as reported by PM FW, when available
		 * UCLK: Min, as reported by PM FW, when available
		 */
		dml_update_soc_for_wm_a(dc, context);
		context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	}

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk_from_validation;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_idx].clks_cfg.dispclk_mhz = get_dispclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt);
		pipes[pipe_idx].clks_cfg.dppclk_mhz = get_dppclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

		if (dc->config.forced_clocks) {
			pipes[pipe_idx].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_idx].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_idx].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_idx++;
	}

	context->perf_params.stutter_period_us = context->bw_ctx.dml.vba.StutterPeriod;

	dml_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	if (!pstate_en)
		/* Restore full p-state latency */
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
}

bool dml_validate(struct dc *dc,
		struct dc_state *context,
		bool fast_validate)
{
	bool out = false;

	BW_VAL_TRACE_SETUP();

	int vlevel = 0;
	int pipe_cnt = 0;
	display_e2e_pipe_params_st *pipes = context->bw_ctx.dml.dml_pipe_state;
	DC_LOGGER_INIT(dc->ctx->logger);

	BW_VAL_TRACE_COUNT();

	out = dml_internal_validate(dc, context, pipes, &pipe_cnt, &vlevel, fast_validate);

	if (pipe_cnt == 0)
		goto validate_out;

	if (!out)
		goto validate_fail;

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
		goto validate_out;
	}

	dml_calculate_wm_and_dlg(dc, context, pipes, pipe_cnt, vlevel);

	BW_VAL_TRACE_END_WATERMARKS();

	goto validate_out;

validate_fail:
	DC_LOG_WARNING("Mode Validation Warning: %s failed validation.\n",
		dml_get_status_message(context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states]));

	BW_VAL_TRACE_SKIP(fail);
	out = false;

validate_out:
	BW_VAL_TRACE_FINISH();

	return out;
}
