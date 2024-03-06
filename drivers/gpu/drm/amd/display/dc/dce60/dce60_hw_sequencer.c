/*
 * Copyright 2020 Mauro Rossi <issor.oruam@gmail.com>
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

#include "dm_services.h"
#include "dc.h"
#include "core_types.h"
#include "dce60_hw_sequencer.h"

#include "dce/dce_hwseq.h"
#include "dce110/dce110_hwseq.h"
#include "dce100/dce100_hwseq.h"

/* include DCE6 register header files */
#include "dce/dce_6_0_d.h"
#include "dce/dce_6_0_sh_mask.h"

#define DC_LOGGER_INIT()

/*******************************************************************************
 * Private definitions
 ******************************************************************************/

/***************************PIPE_CONTROL***********************************/

/*
 *  Check if FBC can be enabled
 */
static bool dce60_should_enable_fbc(struct dc *dc,
		struct dc_state *context,
		uint32_t *pipe_idx)
{
	uint32_t i;
	struct pipe_ctx *pipe_ctx = NULL;
	struct resource_context *res_ctx = &context->res_ctx;
	unsigned int underlay_idx = dc->res_pool->underlay_pipe_index;


	ASSERT(dc->fbc_compressor);

	/* FBC memory should be allocated */
	if (!dc->ctx->fbc_gpu_addr)
		return false;

	/* Only supports single display */
	if (context->stream_count != 1)
		return false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (res_ctx->pipe_ctx[i].stream) {

			pipe_ctx = &res_ctx->pipe_ctx[i];

			if (!pipe_ctx)
				continue;

			/* fbc not applicable on underlay pipe */
			if (pipe_ctx->pipe_idx != underlay_idx) {
				*pipe_idx = i;
				break;
			}
		}
	}

	if (i == dc->res_pool->pipe_count)
		return false;

	if (!pipe_ctx->stream->link)
		return false;

	/* Only supports eDP */
	if (pipe_ctx->stream->link->connector_signal != SIGNAL_TYPE_EDP)
		return false;

	/* PSR should not be enabled */
	if (pipe_ctx->stream->link->psr_settings.psr_feature_enabled)
		return false;

	/* Nothing to compress */
	if (!pipe_ctx->plane_state)
		return false;

	/* Only for non-linear tiling */
	if (pipe_ctx->plane_state->tiling_info.gfx8.array_mode == DC_ARRAY_LINEAR_GENERAL)
		return false;

	return true;
}

/*
 *  Enable FBC
 */
static void dce60_enable_fbc(
		struct dc *dc,
		struct dc_state *context)
{
	uint32_t pipe_idx = 0;

	if (dce60_should_enable_fbc(dc, context, &pipe_idx)) {
		/* Program GRPH COMPRESSED ADDRESS and PITCH */
		struct compr_addr_and_pitch_params params = {0, 0, 0};
		struct compressor *compr = dc->fbc_compressor;
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];

		params.source_view_width = pipe_ctx->stream->timing.h_addressable;
		params.source_view_height = pipe_ctx->stream->timing.v_addressable;
		params.inst = pipe_ctx->stream_res.tg->inst;
		compr->compr_surface_address.quad_part = dc->ctx->fbc_gpu_addr;

		compr->funcs->surface_address_and_pitch(compr, &params);
		compr->funcs->set_fbc_invalidation_triggers(compr, 1);

		compr->funcs->enable_fbc(compr, &params);
	}
}


/*******************************************************************************
 * Front End programming
 ******************************************************************************/

static void dce60_set_default_colors(struct pipe_ctx *pipe_ctx)
{
	struct default_adjustment default_adjust = { 0 };

	default_adjust.force_hw_default = false;
	default_adjust.in_color_space = pipe_ctx->plane_state->color_space;
	default_adjust.out_color_space = pipe_ctx->stream->output_color_space;
	default_adjust.csc_adjust_type = GRAPHICS_CSC_ADJUST_TYPE_SW;
	default_adjust.surface_pixel_format = pipe_ctx->plane_res.scl_data.format;

	/* display color depth */
	default_adjust.color_depth =
		pipe_ctx->stream->timing.display_color_depth;

	/* Lb color depth */
	default_adjust.lb_color_depth = pipe_ctx->plane_res.scl_data.lb_params.depth;

	pipe_ctx->plane_res.xfm->funcs->opp_set_csc_default(
					pipe_ctx->plane_res.xfm, &default_adjust);
}

/*******************************************************************************
 * In order to turn on surface we will program
 * CRTC
 *
 * DCE6 has no bottom_pipe and no Blender HW
 * We need to set 'blank_target' to false in order to turn on the display
 *
 * |-----------|------------|---------|
 * |curr pipe  | set_blank  |         |
 * |Surface    |blank_target|  CRCT   |
 * |visibility |  argument  |         |
 * |-----------|------------|---------|
 * |    off    |   true     | blank   |
 * |    on     |   false    | unblank |
 * |-----------|------------|---------|
 *
 ******************************************************************************/
static void dce60_program_surface_visibility(const struct dc *dc,
		struct pipe_ctx *pipe_ctx)
{
	bool blank_target = false;

	/* DCE6 has no bottom_pipe and no Blender HW */

	if (!pipe_ctx->plane_state->visible)
		blank_target = true;

	/* DCE6 skip dce_set_blender_mode() but then proceed to 'unblank' CRTC */
	pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, blank_target);

}


static void dce60_get_surface_visual_confirm_color(const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE * (4 - pipe_ctx->stream_res.tg->inst) / 4;

	switch (pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set boarder color to red */
		color->color_r_cr = color_value;
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set boarder color to blue */
		color->color_b_cb = color_value;
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set boarder color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set boarder color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set boarder color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		break;
	default:
		break;
	}
}

static void dce60_program_scaler(const struct dc *dc,
		const struct pipe_ctx *pipe_ctx)
{
	struct tg_color color = {0};

	/* DCE6 skips DCN TOFPGA check for transform_set_pixel_storage_depth == NULL */

	if (dc->debug.visual_confirm == VISUAL_CONFIRM_SURFACE)
		dce60_get_surface_visual_confirm_color(pipe_ctx, &color);
	else
		color_space_to_black_color(dc,
				pipe_ctx->stream->output_color_space,
				&color);

	pipe_ctx->plane_res.xfm->funcs->transform_set_pixel_storage_depth(
		pipe_ctx->plane_res.xfm,
		pipe_ctx->plane_res.scl_data.lb_params.depth,
		&pipe_ctx->stream->bit_depth_params);

	if (pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color) {
		/*
		 * The way 420 is packed, 2 channels carry Y component, 1 channel
		 * alternate between Cb and Cr, so both channels need the pixel
		 * value for Y
		 */
		if (pipe_ctx->stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			color.color_r_cr = color.color_g_y;

		pipe_ctx->stream_res.tg->funcs->set_overscan_blank_color(
				pipe_ctx->stream_res.tg,
				&color);
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_scaler(pipe_ctx->plane_res.xfm,
		&pipe_ctx->plane_res.scl_data);
}

static void
dce60_program_front_end_for_pipe(
		struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct mem_input *mi = pipe_ctx->plane_res.mi;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct xfm_grph_csc_adjustment adjust;
	struct out_csc_color_matrix tbl_entry;
	unsigned int i;
	struct dce_hwseq *hws = dc->hwseq;

	DC_LOGGER_INIT();
	memset(&tbl_entry, 0, sizeof(tbl_entry));

	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	dce_enable_fe_clock(dc->hwseq, mi->inst, true);

	dce60_set_default_colors(pipe_ctx);
	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment
			== true) {
		tbl_entry.color_space =
			pipe_ctx->stream->output_color_space;

		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] =
			pipe_ctx->stream->csc_color_matrix.matrix[i];

		pipe_ctx->plane_res.xfm->funcs->opp_set_csc_adjustment
				(pipe_ctx->plane_res.xfm, &tbl_entry);
	}

	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;

		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			adjust.temperature_matrix[i] =
				pipe_ctx->stream->gamut_remap_matrix.matrix[i];
	}

	pipe_ctx->plane_res.xfm->funcs->transform_set_gamut_remap(pipe_ctx->plane_res.xfm, &adjust);

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != 0;

	dce60_program_scaler(dc, pipe_ctx);

	mi->funcs->mem_input_program_surface_config(
			mi,
			plane_state->format,
			&plane_state->tiling_info,
			&plane_state->plane_size,
			plane_state->rotation,
			NULL,
			false);
	if (mi->funcs->set_blank)
		mi->funcs->set_blank(mi, pipe_ctx->plane_state->visible);

	if (dc->config.gpu_vm_support)
		mi->funcs->mem_input_program_pte_vm(
				pipe_ctx->plane_res.mi,
				plane_state->format,
				&plane_state->tiling_info,
				plane_state->rotation);

	/* Moved programming gamma from dc to hwss */
	if (pipe_ctx->plane_state->update_flags.bits.full_update ||
			pipe_ctx->plane_state->update_flags.bits.in_transfer_func_change ||
			pipe_ctx->plane_state->update_flags.bits.gamma_change)
		hws->funcs.set_input_transfer_func(dc, pipe_ctx, pipe_ctx->plane_state);

	if (pipe_ctx->plane_state->update_flags.bits.full_update)
		hws->funcs.set_output_transfer_func(dc, pipe_ctx, pipe_ctx->stream);

	DC_LOG_SURFACE(
			"Pipe:%d %p: addr hi:0x%x, "
			"addr low:0x%x, "
			"src: %d, %d, %d,"
			" %d; dst: %d, %d, %d, %d;"
			"clip: %d, %d, %d, %d\n",
			pipe_ctx->pipe_idx,
			(void *) pipe_ctx->plane_state,
			pipe_ctx->plane_state->address.grph.addr.high_part,
			pipe_ctx->plane_state->address.grph.addr.low_part,
			pipe_ctx->plane_state->src_rect.x,
			pipe_ctx->plane_state->src_rect.y,
			pipe_ctx->plane_state->src_rect.width,
			pipe_ctx->plane_state->src_rect.height,
			pipe_ctx->plane_state->dst_rect.x,
			pipe_ctx->plane_state->dst_rect.y,
			pipe_ctx->plane_state->dst_rect.width,
			pipe_ctx->plane_state->dst_rect.height,
			pipe_ctx->plane_state->clip_rect.x,
			pipe_ctx->plane_state->clip_rect.y,
			pipe_ctx->plane_state->clip_rect.width,
			pipe_ctx->plane_state->clip_rect.height);

	DC_LOG_SURFACE(
			"Pipe %d: width, height, x, y\n"
			"viewport:%d, %d, %d, %d\n"
			"recout:  %d, %d, %d, %d\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->plane_res.scl_data.viewport.width,
			pipe_ctx->plane_res.scl_data.viewport.height,
			pipe_ctx->plane_res.scl_data.viewport.x,
			pipe_ctx->plane_res.scl_data.viewport.y,
			pipe_ctx->plane_res.scl_data.recout.width,
			pipe_ctx->plane_res.scl_data.recout.height,
			pipe_ctx->plane_res.scl_data.recout.x,
			pipe_ctx->plane_res.scl_data.recout.y);
}

static void dce60_apply_ctx_for_surface(
		struct dc *dc,
		const struct dc_stream_state *stream,
		int num_planes,
		struct dc_state *context)
{
	int i;

	if (num_planes == 0)
		return;

	if (dc->fbc_compressor)
		dc->fbc_compressor->funcs->disable_fbc(dc->fbc_compressor);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		/* Need to allocate mem before program front end for Fiji */
		pipe_ctx->plane_res.mi->funcs->allocate_mem_input(
				pipe_ctx->plane_res.mi,
				pipe_ctx->stream->timing.h_total,
				pipe_ctx->stream->timing.v_total,
				pipe_ctx->stream->timing.pix_clk_100hz / 10,
				context->stream_count);

		dce60_program_front_end_for_pipe(dc, pipe_ctx);

		dc->hwss.update_plane_addr(dc, pipe_ctx);

		dce60_program_surface_visibility(dc, pipe_ctx);

	}

	if (dc->fbc_compressor)
		dce60_enable_fbc(dc, context);
}

void dce60_hw_sequencer_construct(struct dc *dc)
{
	dce110_hw_sequencer_construct(dc);

	dc->hwseq->funcs.enable_display_power_gating = dce100_enable_display_power_gating;
	dc->hwss.apply_ctx_for_surface = dce60_apply_ctx_for_surface;
	dc->hwss.cursor_lock = dce60_pipe_control_lock;
	dc->hwss.pipe_control_lock = dce60_pipe_control_lock;
	dc->hwss.prepare_bandwidth = dce100_prepare_bandwidth;
	dc->hwss.optimize_bandwidth = dce100_optimize_bandwidth;
}

