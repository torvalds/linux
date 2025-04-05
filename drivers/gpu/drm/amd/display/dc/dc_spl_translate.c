// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dc_spl_translate.h"
#include "spl/dc_spl_types.h"
#include "dcn20/dcn20_dpp.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn401/dcn401_dpp.h"

static struct spl_callbacks dcn2_spl_callbacks = {
	.spl_calc_lb_num_partitions = dscl2_spl_calc_lb_num_partitions,
};
static struct spl_callbacks dcn32_spl_callbacks = {
	.spl_calc_lb_num_partitions = dscl32_spl_calc_lb_num_partitions,
};
static struct spl_callbacks dcn401_spl_callbacks = {
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
	spl_scaling_quality->integer_scaling = scaling_quality->integer_scaling;
}
static void populate_taps_from_spltaps(struct scaling_taps *scaling_quality,
		const struct spl_taps *spl_scaling_quality)
{
	scaling_quality->h_taps_c = spl_scaling_quality->h_taps_c + 1;
	scaling_quality->h_taps = spl_scaling_quality->h_taps + 1;
	scaling_quality->v_taps_c = spl_scaling_quality->v_taps_c + 1;
	scaling_quality->v_taps = spl_scaling_quality->v_taps + 1;
}
static void populate_ratios_from_splratios(struct scaling_ratios *ratios,
		const struct ratio *spl_ratios)
{
	ratios->horz = dc_fixpt_from_ux_dy(spl_ratios->h_scale_ratio >> 5, 3, 19);
	ratios->vert = dc_fixpt_from_ux_dy(spl_ratios->v_scale_ratio >> 5, 3, 19);
	ratios->horz_c = dc_fixpt_from_ux_dy(spl_ratios->h_scale_ratio_c >> 5, 3, 19);
	ratios->vert_c = dc_fixpt_from_ux_dy(spl_ratios->v_scale_ratio_c >> 5, 3, 19);
}
static void populate_inits_from_splinits(struct scl_inits *inits,
		const struct init *spl_inits)
{
	inits->h = dc_fixpt_from_int_dy(spl_inits->h_filter_init_int, spl_inits->h_filter_init_frac >> 5, 0, 19);
	inits->v = dc_fixpt_from_int_dy(spl_inits->v_filter_init_int, spl_inits->v_filter_init_frac >> 5, 0, 19);
	inits->h_c = dc_fixpt_from_int_dy(spl_inits->h_filter_init_int_c, spl_inits->h_filter_init_frac_c >> 5, 0, 19);
	inits->v_c = dc_fixpt_from_int_dy(spl_inits->v_filter_init_int_c, spl_inits->v_filter_init_frac_c >> 5, 0, 19);
}
static void populate_splformat_from_format(enum spl_pixel_format *spl_pixel_format, const enum pixel_format pixel_format)
{
	if (pixel_format < PIXEL_FORMAT_INVALID)
		*spl_pixel_format = (enum spl_pixel_format)pixel_format;
	else
		*spl_pixel_format = SPL_PIXEL_FORMAT_INVALID;
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
		spl_in->callbacks = dcn2_spl_callbacks;
		break;
	case DCN_VERSION_3_2:
		spl_in->callbacks = dcn32_spl_callbacks;
		break;
	case DCN_VERSION_4_01:
		spl_in->callbacks = dcn401_spl_callbacks;
		break;
	default:
		spl_in->callbacks = dcn2_spl_callbacks;
	}
	// Make format field from spl_in point to plane_res scl_data format
	populate_splformat_from_format(&spl_in->basic_in.format, pipe_ctx->plane_res.scl_data.format);
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
	spl_in->basic_in.num_h_slices_recout_width_align.use_recout_width_aligned = false;
	spl_in->basic_in.num_h_slices_recout_width_align.num_slices_recout_width.mpc_num_h_slices =
		resource_get_mpc_slice_count(pipe_ctx);

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
		spl_in->basic_in.mpc_h_slice_index = 0;
	else
		spl_in->basic_in.mpc_h_slice_index = resource_get_mpc_slice_index(pipe_ctx);

	populate_splrect_from_rect(&spl_in->basic_out.odm_slice_rect, &odm_slice_src);
	spl_in->basic_out.odm_combine_factor = 0;
	spl_in->odm_slice_index = resource_get_odm_slice_index(pipe_ctx);
	// Make spl input basic out info output_size width point to stream h active
	spl_in->basic_out.output_size.width =
		stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right + pipe_ctx->hblank_borrow;
	// Make spl input basic out info output_size height point to v active
	spl_in->basic_out.output_size.height =
		stream->timing.v_addressable + stream->timing.v_border_bottom + stream->timing.v_border_top;
	spl_in->basic_out.max_downscale_src_width =
			pipe_ctx->stream->ctx->dc->debug.max_downscale_src_width;
	spl_in->basic_out.always_scale = pipe_ctx->stream->ctx->dc->debug.always_scale;
	// Make spl input basic output info alpha_en field point to plane res scl_data lb_params alpha_en
	spl_in->basic_out.alpha_en = pipe_ctx->plane_res.scl_data.lb_params.alpha_en;
	spl_in->basic_out.use_two_pixels_per_container = pipe_ctx->stream_res.tg->funcs->is_two_pixels_per_container(&stream->timing);
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
	unsigned int sharpness_setting = pipe_ctx->stream->ctx->dc->debug.force_sharpness;
	unsigned int force_sharpness_level = pipe_ctx->stream->ctx->dc->debug.force_sharpness_level;
	if (sharpness_setting == SHARPNESS_HW_OFF)
		spl_in->adaptive_sharpness.enable = false;
	else if (sharpness_setting == SHARPNESS_ZERO) {
		spl_in->adaptive_sharpness.enable = true;
		spl_in->adaptive_sharpness.sharpness_level = 0;
	} else if (sharpness_setting == SHARPNESS_CUSTOM) {
		spl_in->adaptive_sharpness.sharpness_range.sdr_rgb_min = 0;
		spl_in->adaptive_sharpness.sharpness_range.sdr_rgb_max = 1750;
		spl_in->adaptive_sharpness.sharpness_range.sdr_rgb_mid = 750;
		spl_in->adaptive_sharpness.sharpness_range.sdr_yuv_min = 0;
		spl_in->adaptive_sharpness.sharpness_range.sdr_yuv_max = 3500;
		spl_in->adaptive_sharpness.sharpness_range.sdr_yuv_mid = 1500;
		spl_in->adaptive_sharpness.sharpness_range.hdr_rgb_min = 0;
		spl_in->adaptive_sharpness.sharpness_range.hdr_rgb_max = 2750;
		spl_in->adaptive_sharpness.sharpness_range.hdr_rgb_mid = 1500;

		if (force_sharpness_level > 0) {
			if (force_sharpness_level > 10)
				force_sharpness_level = 10;
			spl_in->adaptive_sharpness.enable = true;
			spl_in->adaptive_sharpness.sharpness_level = force_sharpness_level;
		} else if (!plane_state->adaptive_sharpness_en) {
			spl_in->adaptive_sharpness.enable = false;
			spl_in->adaptive_sharpness.sharpness_level = 0;
		} else {
			spl_in->adaptive_sharpness.enable = true;
			spl_in->adaptive_sharpness.sharpness_level = plane_state->sharpness_level;
		}
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

	spl_in->h_active = pipe_ctx->plane_res.scl_data.h_active;
	spl_in->v_active = pipe_ctx->plane_res.scl_data.v_active;

	spl_in->sharpen_policy = (enum sharpen_policy)plane_state->adaptive_sharpness_policy;
	spl_in->debug.scale_to_sharpness_policy =
		(enum scale_to_sharpness_policy)pipe_ctx->stream->ctx->dc->debug.scale_to_sharpness_policy;

	/* Check if it is stream is in fullscreen and if its HDR.
	 * Use this to determine sharpness levels
	 */
	spl_in->is_fullscreen = pipe_ctx->stream->sharpening_required;
	spl_in->is_hdr_on = dm_helpers_is_hdr_on(pipe_ctx->stream->ctx, pipe_ctx->stream);
	spl_in->sdr_white_level_nits = plane_state->sdr_white_level_nits;
}

/// @brief Translate SPL output parameters to pipe context
/// @param pipe_ctx
/// @param spl_out
void translate_SPL_out_params_to_pipe_ctx(struct pipe_ctx *pipe_ctx, struct spl_out *spl_out)
{
	// Make scaler data recout point to spl output field recout
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.recout, &spl_out->dscl_prog_data->recout);
	// Make scaler data ratios point to spl output field ratios
	populate_ratios_from_splratios(&pipe_ctx->plane_res.scl_data.ratios, &spl_out->dscl_prog_data->ratios);
	// Make scaler data viewport point to spl output field viewport
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.viewport, &spl_out->dscl_prog_data->viewport);
	// Make scaler data viewport_c point to spl output field viewport_c
	populate_rect_from_splrect(&pipe_ctx->plane_res.scl_data.viewport_c, &spl_out->dscl_prog_data->viewport_c);
	// Make scaler data taps point to spl output field scaling taps
	populate_taps_from_spltaps(&pipe_ctx->plane_res.scl_data.taps, &spl_out->dscl_prog_data->taps);
	// Make scaler data init point to spl output field init
	populate_inits_from_splinits(&pipe_ctx->plane_res.scl_data.inits, &spl_out->dscl_prog_data->init);
}
