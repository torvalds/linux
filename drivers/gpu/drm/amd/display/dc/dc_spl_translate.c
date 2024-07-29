// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dc_spl_translate.h"
#include "spl/dc_spl_types.h"
#include "dcn20/dcn20_dpp.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn401/dcn401_dpp.h"

static struct spl_funcs dcn2_spl_funcs = {
	.spl_calc_lb_num_partitions = dscl2_spl_calc_lb_num_partitions,
};
static struct spl_funcs dcn32_spl_funcs = {
	.spl_calc_lb_num_partitions = dscl32_spl_calc_lb_num_partitions,
};
static struct spl_funcs dcn401_spl_funcs = {
	.spl_calc_lb_num_partitions = dscl401_spl_calc_lb_num_partitions,
};
static void populate_splrect_from_rect(struct spl_rect *spl_rect, const struct rect *rect)
{
	spl_rect->x = rect->x;
	spl_rect->y = rect->y;
	spl_rect->width = rect->width;
	spl_rect->height = rect->height;
}
static void populate_rect_from_splrect(struct rect *rect, const struct spl_rect *spl_rect)
{
	rect->x = spl_rect->x;
	rect->y = spl_rect->y;
	rect->width = spl_rect->width;
	rect->height = spl_rect->height;
}
static void populate_spltaps_from_taps(struct spl_taps *spl_scaling_quality,
		const struct scaling_taps *scaling_quality)
{
	spl_scaling_quality->h_taps_c = scaling_quality->h_taps_c;
	spl_scaling_quality->h_taps = scaling_quality->h_taps;
	spl_scaling_quality->v_taps_c = scaling_quality->v_taps_c;
	spl_scaling_quality->v_taps = scaling_quality->v_taps;
}
static void populate_taps_from_spltaps(struct scaling_taps *scaling_quality,
		const struct spl_taps *spl_scaling_quality)
{
	scaling_quality->h_taps_c = spl_scaling_quality->h_taps_c;
	scaling_quality->h_taps = spl_scaling_quality->h_taps;
	scaling_quality->v_taps_c = spl_scaling_quality->v_taps_c;
	scaling_quality->v_taps = spl_scaling_quality->v_taps;
}
static void populate_ratios_from_splratios(struct scaling_ratios *ratios,
		const struct spl_ratios *spl_ratios)
{
	ratios->horz = spl_ratios->horz;
	ratios->vert = spl_ratios->vert;
	ratios->horz_c = spl_ratios->horz_c;
	ratios->vert_c = spl_ratios->vert_c;
}
static void populate_inits_from_splinits(struct scl_inits *inits,
		const struct spl_inits *spl_inits)
{
	inits->h = spl_inits->h;
	inits->v = spl_inits->v;
	inits->h_c = spl_inits->h_c;
	inits->v_c = spl_inits->v_c;
}
/// @brief Translate SPL input parameters from pipe context
/// @param pipe_ctx
/// @param spl_in
void translate_SPL_in_params_from_pipe_ctx(struct pipe_ctx *pipe_ctx, struct spl_in *spl_in)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct rect odm_slice_src = resource_get_odm_slice_src_rect(pipe_ctx);

	// Assign the function to calculate the number of partitions in the line buffer
	// This is used to determine the vtap support
	switch (plane_state->ctx->dce_version)	{
	case DCN_VERSION_2_0:
		spl_in->funcs = &dcn2_spl_funcs;
		break;
	case DCN_VERSION_3_2:
		spl_in->funcs = &dcn32_spl_funcs;
		break;
	case DCN_VERSION_4_01:
		spl_in->funcs = &dcn401_spl_funcs;
		break;
	default:
		spl_in->funcs = &dcn2_spl_funcs;
	}
	// Make format field from spl_in point to plane_res scl_data format
	spl_in->basic_in.format = (enum spl_pixel_format)pipe_ctx->plane_res.scl_data.format;
	// Make view_format from basic_out point to view_format from stream
	spl_in->basic_out.view_format = (enum spl_view_3d)stream->view_format;
	// Populate spl input basic input clip rect from plane state clip rect
	populate_splrect_from_rect(&spl_in->basic_in.clip_rect, &plane_state->clip_rect);
	// Populate spl input basic out src rect from stream src rect
	populate_splrect_from_rect(&spl_in->basic_out.src_rect, &stream->src);
	// Populate spl input basic out dst rect from stream dst rect
	populate_splrect_from_rect(&spl_in->basic_out.dst_rect, &stream->dst);
	// Make spl input basic input info rotation field point to plane state rotation
	spl_in->basic_in.rotation = (enum spl_rotation_angle)plane_state->rotation;
	// Populate spl input basic input src rect from plane state src rect
	populate_splrect_from_rect(&spl_in->basic_in.src_rect, &plane_state->src_rect);
	// Populate spl input basic input dst rect from plane state dst rect
	populate_splrect_from_rect(&spl_in->basic_in.dst_rect, &plane_state->dst_rect);
	// Make spl input basic input info horiz mirror field point to plane state horz mirror
	spl_in->basic_in.horizontal_mirror = plane_state->horizontal_mirror;

	// Calculate horizontal splits and split index
	spl_in->basic_in.mpc_combine_h = resource_get_mpc_slice_count(pipe_ctx);

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
		spl_in->basic_in.mpc_combine_v = 0;
	else
		spl_in->basic_in.mpc_combine_v = resource_get_mpc_slice_index(pipe_ctx);

	populate_splrect_from_rect(&spl_in->basic_out.odm_slice_rect, &odm_slice_src);
	spl_in->basic_out.odm_combine_factor = 0;
	spl_in->odm_slice_index = resource_get_odm_slice_index(pipe_ctx);
	// Make spl input basic out info output_size width point to stream h active
	spl_in->basic_out.output_size.width =
		stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right;
	// Make spl input basic out info output_size height point to v active
	spl_in->basic_out.output_size.height =
		stream->timing.v_addressable + stream->timing.v_border_bottom + stream->timing.v_border_top;
	spl_in->basic_out.max_downscale_src_width =
			pipe_ctx->stream->ctx->dc->debug.max_downscale_src_width;
	spl_in->basic_out.always_scale = pipe_ctx->stream->ctx->dc->debug.always_scale;
	// Make spl input basic output info alpha_en field point to plane res scl_data lb_params alpha_en
	spl_in->basic_out.alpha_en = pipe_ctx->plane_res.scl_data.lb_params.alpha_en;
	// Make spl input basic input info scaling quality field point to plane state scaling_quality
	populate_spltaps_from_taps(&spl_in->scaling_quality, &plane_state->scaling_quality);
	// Translate edge adaptive scaler preference
	spl_in->prefer_easf = pipe_ctx->stream->ctx->dc->config.prefer_easf;
	spl_in->disable_easf = false;
	if (pipe_ctx->stream->ctx->dc->debug.force_easf == 1)
		spl_in->prefer_easf = false;
	else if (pipe_ctx->stream->ctx->dc->debug.force_easf == 2)
		spl_in->disable_easf = true;
	/* Translate adaptive sharpening preference */
	if (pipe_ctx->stream->ctx->dc->debug.force_sharpness > 0) {
		spl_in->adaptive_sharpness.enable = (pipe_ctx->stream->ctx->dc->debug.force_sharpness > 1) ? true : false;
		if (pipe_ctx->stream->ctx->dc->debug.force_sharpness == 2)
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_LOW;
		else if (pipe_ctx->stream->ctx->dc->debug.force_sharpness == 3)
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_MID;
		else if (pipe_ctx->stream->ctx->dc->debug.force_sharpness >= 4)
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_HIGH;
	} else {
		spl_in->adaptive_sharpness.enable = plane_state->adaptive_sharpness_en;
		if (plane_state->sharpnessX1000 == 0)
			spl_in->adaptive_sharpness.enable = false;
		else if (plane_state->sharpnessX1000 < 999)
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_LOW;
		else if (plane_state->sharpnessX1000 < 1999)
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_MID;
		else // Any other value is high sharpness
			spl_in->adaptive_sharpness.sharpness = SHARPNESS_HIGH;
	}
	// Translate linear light scaling preference
	if (pipe_ctx->stream->ctx->dc->debug.force_lls > 0)
		spl_in->lls_pref = pipe_ctx->stream->ctx->dc->debug.force_lls;
	else
		spl_in->lls_pref = plane_state->linear_light_scaling;
	/* Translate chroma subsampling offset ( cositing ) */
	if (pipe_ctx->stream->ctx->dc->debug.force_cositing)
		spl_in->basic_in.cositing = pipe_ctx->stream->ctx->dc->debug.force_cositing - 1;
	else
		spl_in->basic_in.cositing = plane_state->cositing;
	/* Translate transfer function */
	spl_in->basic_in.tf_type = (enum spl_transfer_func_type) plane_state->in_transfer_func.type;
	spl_in->basic_in.tf_predefined_type = (enum spl_transfer_func_predefined) plane_state->in_transfer_func.tf;

}

/// @brief Translate SPL output parameters to pipe context
/// @param pipe_ctx
/// @param spl_out
void translate_SPL_out_params_to_pipe_ctx(struct pipe_ctx *pipe_ctx, struct spl_out *spl_out)
{
	// Make scaler data recout point to spl output field recout
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.recout, &spl_out->scl_data.recout);
	// Make scaler data ratios point to spl output field ratios
	populate_ratios_from_splratios(&pipe_ctx->plane_res.scl_data.ratios, &spl_out->scl_data.ratios);
	// Make scaler data viewport point to spl output field viewport
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.viewport, &spl_out->scl_data.viewport);
	// Make scaler data viewport_c point to spl output field viewport_c
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.viewport_c, &spl_out->scl_data.viewport_c);
	// Make scaler data taps point to spl output field scaling taps
	populate_taps_from_spltaps(&pipe_ctx->plane_res.scl_data.taps, &spl_out->scl_data.taps);
	// Make scaler data init point to spl output field init
	populate_inits_from_splinits(&pipe_ctx->plane_res.scl_data.inits, &spl_out->scl_data.inits);
}
