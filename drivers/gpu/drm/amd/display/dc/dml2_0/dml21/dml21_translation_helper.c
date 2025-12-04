// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml21_wrapper.h"
#include "dml2_core_dcn4_calcs.h"
#include "dml2_internal_shared_types.h"
#include "dml2_internal_types.h"
#include "dml21_utils.h"
#include "dml21_translation_helper.h"
#include "soc_and_ip_translator.h"

static void dml21_populate_pmo_options(struct dml2_pmo_options *pmo_options,
		const struct dc *in_dc,
		const struct dml2_configuration_options *config)
{
	bool disable_fams2 = !in_dc->debug.fams2_config.bits.enable;

	/* ODM options */
	pmo_options->disable_dyn_odm = !config->minimize_dispclk_using_odm;
	pmo_options->disable_dyn_odm_for_multi_stream = true;
	pmo_options->disable_dyn_odm_for_stream_with_svp = true;

	pmo_options->disable_vblank = ((in_dc->debug.dml21_disable_pstate_method_mask >> 1) & 1);

	/* NOTE: DRR and SubVP Require FAMS2 */
	pmo_options->disable_svp = ((in_dc->debug.dml21_disable_pstate_method_mask >> 2) & 1) ||
			in_dc->debug.force_disable_subvp ||
			disable_fams2;
	pmo_options->disable_drr_clamped = ((in_dc->debug.dml21_disable_pstate_method_mask >> 3) & 1) ||
			disable_fams2;
	pmo_options->disable_drr_var = ((in_dc->debug.dml21_disable_pstate_method_mask >> 4) & 1) ||
			disable_fams2;
	pmo_options->disable_fams2 = disable_fams2;

	pmo_options->disable_drr_var_when_var_active = in_dc->debug.disable_fams_gaming == INGAME_FAMS_DISABLE ||
			in_dc->debug.disable_fams_gaming == INGAME_FAMS_MULTI_DISP_CLAMPED_ONLY;
	pmo_options->disable_drr_clamped_when_var_active = in_dc->debug.disable_fams_gaming == INGAME_FAMS_DISABLE;
}

static enum dml2_project_id dml21_dcn_revision_to_dml2_project_id(enum dce_version dcn_version)
{
	enum dml2_project_id project_id;
	switch (dcn_version) {
	case DCN_VERSION_4_01:
		project_id = dml2_project_dcn4x_stage2_auto_drr_svp;
		break;
	default:
		project_id = dml2_project_invalid;
		DC_ERR("unsupported dcn version for DML21!");
		break;
	}

	return project_id;
}

void dml21_populate_dml_init_params(struct dml2_initialize_instance_in_out *dml_init,
		const struct dml2_configuration_options *config,
		const struct dc *in_dc)
{
	dml_init->options.project_id = dml21_dcn_revision_to_dml2_project_id(in_dc->ctx->dce_version);

	if (config->use_native_soc_bb_construction) {
		in_dc->soc_and_ip_translator->translator_funcs->get_soc_bb(&dml_init->soc_bb, in_dc, config);
		in_dc->soc_and_ip_translator->translator_funcs->get_ip_caps(&dml_init->ip_caps);
	} else {
		dml_init->soc_bb = config->external_socbb_ip_params->soc_bb;
		dml_init->ip_caps = config->external_socbb_ip_params->ip_params;
	}

	dml21_populate_pmo_options(&dml_init->options.pmo_options, in_dc, config);
}

static unsigned int calc_max_hardware_v_total(const struct dc_stream_state *stream)
{
	unsigned int max_hw_v_total = stream->ctx->dc->caps.max_v_total;

	if (stream->ctx->dc->caps.vtotal_limited_by_fp2) {
		max_hw_v_total -= stream->timing.v_front_porch + 1;
	}

	return max_hw_v_total;
}

static void populate_dml21_timing_config_from_stream_state(struct dml2_timing_cfg *timing,
		struct dc_stream_state *stream,
		struct pipe_ctx *pipe_ctx,
		struct dml2_context *dml_ctx)
{
	unsigned int hblank_start, vblank_start, min_hardware_refresh_in_uhz;
	uint32_t pix_clk_100hz;

	timing->h_active = stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right + pipe_ctx->dsc_padding_params.dsc_hactive_padding;
	timing->v_active = stream->timing.v_addressable + stream->timing.v_border_bottom + stream->timing.v_border_top;
	timing->h_front_porch = stream->timing.h_front_porch;
	timing->v_front_porch = stream->timing.v_front_porch;
	timing->pixel_clock_khz = stream->timing.pix_clk_100hz / 10;
	if (pipe_ctx->dsc_padding_params.dsc_hactive_padding != 0)
		timing->pixel_clock_khz = pipe_ctx->dsc_padding_params.dsc_pix_clk_100hz / 10;
	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		timing->pixel_clock_khz *= 2;
	timing->h_total = stream->timing.h_total + pipe_ctx->dsc_padding_params.dsc_htotal_padding;
	timing->v_total = stream->timing.v_total;
	timing->h_sync_width = stream->timing.h_sync_width;
	timing->interlaced = stream->timing.flags.INTERLACE;

	hblank_start = stream->timing.h_total - stream->timing.h_front_porch;

	timing->h_blank_end = hblank_start - stream->timing.h_addressable - pipe_ctx->dsc_padding_params.dsc_hactive_padding
		- stream->timing.h_border_left - stream->timing.h_border_right;

	if (hblank_start < stream->timing.h_addressable)
		timing->h_blank_end = 0;

	vblank_start = stream->timing.v_total - stream->timing.v_front_porch;

	timing->v_blank_end = vblank_start - stream->timing.v_addressable
		- stream->timing.v_border_top - stream->timing.v_border_bottom;

	timing->drr_config.enabled = stream->ignore_msa_timing_param;
	timing->drr_config.drr_active_variable = stream->vrr_active_variable;
	timing->drr_config.drr_active_fixed = stream->vrr_active_fixed;
	timing->drr_config.disallowed = !stream->allow_freesync;

	/* limit min refresh rate to DC cap */
	min_hardware_refresh_in_uhz = stream->timing.min_refresh_in_uhz;
	if (stream->ctx->dc->caps.max_v_total != 0) {
		if (pipe_ctx->dsc_padding_params.dsc_hactive_padding != 0) {
			pix_clk_100hz = pipe_ctx->dsc_padding_params.dsc_pix_clk_100hz;
		} else {
			pix_clk_100hz = stream->timing.pix_clk_100hz;
		}
		min_hardware_refresh_in_uhz = div64_u64((pix_clk_100hz * 100000000ULL),
				(timing->h_total * (long long)calc_max_hardware_v_total(stream)));
	}

	timing->drr_config.min_refresh_uhz = max(stream->timing.min_refresh_in_uhz, min_hardware_refresh_in_uhz);

	if (dml_ctx->config.callbacks.get_max_flickerless_instant_vtotal_increase &&
			stream->ctx->dc->config.enable_fpo_flicker_detection == 1)
		timing->drr_config.max_instant_vtotal_delta = dml_ctx->config.callbacks.get_max_flickerless_instant_vtotal_increase(stream, false);
	else
		timing->drr_config.max_instant_vtotal_delta = 0;

	if (stream->timing.flags.DSC) {
		timing->dsc.enable = dml2_dsc_enable;
		timing->dsc.overrides.num_slices = stream->timing.dsc_cfg.num_slices_h;
		timing->dsc.dsc_compressed_bpp_x16 = stream->timing.dsc_cfg.bits_per_pixel;
	} else
		timing->dsc.enable = dml2_dsc_disable;

	switch (stream->timing.display_color_depth) {
	case COLOR_DEPTH_666:
		timing->bpc = 6;
		break;
	case COLOR_DEPTH_888:
		timing->bpc = 8;
		break;
	case COLOR_DEPTH_101010:
		timing->bpc = 10;
		break;
	case COLOR_DEPTH_121212:
		timing->bpc = 12;
		break;
	case COLOR_DEPTH_141414:
		timing->bpc = 14;
		break;
	case COLOR_DEPTH_161616:
		timing->bpc = 16;
		break;
	case COLOR_DEPTH_999:
		timing->bpc = 9;
		break;
	case COLOR_DEPTH_111111:
		timing->bpc = 11;
		break;
	default:
		timing->bpc = 8;
		break;
	}

	timing->vblank_nom = timing->v_total - timing->v_active;
}

static void populate_dml21_output_config_from_stream_state(struct dml2_link_output_cfg *output,
		struct dc_stream_state *stream, const struct pipe_ctx *pipe)
{
	output->output_dp_lane_count = 4;

	switch (stream->signal) {
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DISPLAY_PORT:
		output->output_encoder = dml2_dp;
		if (check_dp2p0_output_encoder(pipe))
			output->output_encoder = dml2_dp2p0;
		break;
	case SIGNAL_TYPE_EDP:
		output->output_encoder = dml2_edp;
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		output->output_encoder = dml2_hdmi;
		break;
	default:
			output->output_encoder = dml2_dp;
	}

	switch (stream->timing.pixel_encoding) {
	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
		output->output_format = dml2_444;
		break;
	case PIXEL_ENCODING_YCBCR420:
		output->output_format = dml2_420;
		break;
	case PIXEL_ENCODING_YCBCR422:
		if (stream->timing.flags.DSC && !stream->timing.dsc_cfg.ycbcr422_simple)
			output->output_format = dml2_n422;
		else
			output->output_format = dml2_s422;
		break;
	default:
		output->output_format = dml2_444;
		break;
	}

	switch (stream->signal) {
	case SIGNAL_TYPE_NONE:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_RGB:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_VIRTUAL:
	default:
		output->output_dp_link_rate = dml2_dp_rate_na;
		break;
	}

	output->audio_sample_layout = stream->audio_info.modes->sample_size;
	output->audio_sample_rate = stream->audio_info.modes->max_bit_rate;
	output->output_disabled = true;

	//TODO : New to DML2.1. How do we populate this ?
	// output->validate_output
}

static void populate_dml21_stream_overrides_from_stream_state(
		struct dml2_stream_parameters *stream_desc,
		struct dc_stream_state *stream,
		struct dc_stream_status *stream_status)
{
	switch (stream->debug.force_odm_combine_segments) {
	case 0:
		stream_desc->overrides.odm_mode = dml2_odm_mode_auto;
		break;
	case 1:
		stream_desc->overrides.odm_mode = dml2_odm_mode_bypass;
		break;
	case 2:
		stream_desc->overrides.odm_mode = dml2_odm_mode_combine_2to1;
		break;
	case 3:
		stream_desc->overrides.odm_mode = dml2_odm_mode_combine_3to1;
		break;
	case 4:
		stream_desc->overrides.odm_mode = dml2_odm_mode_combine_4to1;
		break;
	default:
		stream_desc->overrides.odm_mode =  dml2_odm_mode_auto;
		break;
	}
	if (!stream->ctx->dc->debug.enable_single_display_2to1_odm_policy ||
			stream->debug.force_odm_combine_segments > 0)
		stream_desc->overrides.disable_dynamic_odm = true;
	stream_desc->overrides.disable_subvp = stream->ctx->dc->debug.force_disable_subvp ||
			stream->hw_cursor_req ||
			stream_status->mall_stream_config.cursor_size_limit_subvp;
}

static enum dml2_swizzle_mode gfx_addr3_to_dml2_swizzle_mode(enum swizzle_mode_addr3_values addr3_mode)
{
	enum dml2_swizzle_mode dml2_mode = dml2_sw_linear;

	switch (addr3_mode) {
	case DC_ADDR3_SW_LINEAR:
		dml2_mode = dml2_sw_linear;
		break;
	case DC_ADDR3_SW_256B_2D:
		dml2_mode = dml2_sw_256b_2d;
		break;
	case DC_ADDR3_SW_4KB_2D:
		dml2_mode = dml2_sw_4kb_2d;
		break;
	case DC_ADDR3_SW_64KB_2D:
		dml2_mode = dml2_sw_64kb_2d;
		break;
	case DC_ADDR3_SW_256KB_2D:
		dml2_mode = dml2_sw_256kb_2d;
		break;
	default:
		/* invalid swizzle mode for DML2.1 */
		ASSERT(false);
		dml2_mode = dml2_sw_linear;
	}

	return dml2_mode;
}

static enum dml2_swizzle_mode gfx9_to_dml2_swizzle_mode(enum swizzle_mode_values gfx9_mode)
{
	enum dml2_swizzle_mode dml2_mode = dml2_sw_64kb_2d;

	switch (gfx9_mode) {
	case DC_SW_LINEAR:
		dml2_mode = dml2_sw_linear;
		break;
	case DC_SW_256_D:
	case DC_SW_256_R:
		dml2_mode = dml2_sw_256b_2d;
		break;
	case DC_SW_4KB_D:
	case DC_SW_4KB_R:
	case DC_SW_4KB_R_X:
		dml2_mode = dml2_sw_4kb_2d;
		break;
	case DC_SW_64KB_D:
	case DC_SW_64KB_D_X:
	case DC_SW_64KB_R:
	case DC_SW_64KB_R_X:
		dml2_mode = dml2_sw_64kb_2d;
		break;
	case DC_SW_256B_S:
	case DC_SW_4KB_S:
	case DC_SW_64KB_S:
	case DC_SW_VAR_S:
	case DC_SW_VAR_D:
	case DC_SW_VAR_R:
	case DC_SW_64KB_S_T:
	case DC_SW_64KB_D_T:
	case DC_SW_4KB_S_X:
	case DC_SW_4KB_D_X:
	case DC_SW_64KB_S_X:
	case DC_SW_VAR_S_X:
	case DC_SW_VAR_D_X:
	case DC_SW_VAR_R_X:
	default:
		/*
		 * invalid swizzle mode for DML2.1. This could happen because
		 * DML21 is not intended to be used by N-1 in production. To
		 * properly filter out unsupported swizzle modes, we will need
		 * to fix capability reporting when DML2.1 is used for N-1 in
		 * dc. So DML will only receive DML21 supported swizzle modes.
		 * This implementation is not added and has a low value because
		 * the supported swizzle modes should already cover most of our
		 * N-1 test cases.
		 */
		return dml2_sw_64kb_2d;
	}

	return dml2_mode;
}

static void populate_dml21_dummy_surface_cfg(struct dml2_surface_cfg *surface, const struct dc_stream_state *stream)
{
	surface->plane0.width = stream->timing.h_addressable;
	surface->plane0.height = stream->timing.v_addressable;
	surface->plane1.width = stream->timing.h_addressable;
	surface->plane1.height = stream->timing.v_addressable;
	surface->plane0.pitch = ((surface->plane0.width + 127) / 128) * 128;
	surface->plane1.pitch = 0;
	surface->dcc.enable = false;
	surface->dcc.informative.dcc_rate_plane0 = 1.0;
	surface->dcc.informative.dcc_rate_plane1 = 1.0;
	surface->dcc.informative.fraction_of_zero_size_request_plane0 = 0;
	surface->dcc.informative.fraction_of_zero_size_request_plane1 = 0;
	surface->tiling = dml2_sw_64kb_2d;
}

static void populate_dml21_dummy_plane_cfg(struct dml2_plane_parameters *plane, const struct dc_stream_state *stream)
{
	unsigned int width, height;

	if (stream->timing.h_addressable > 3840)
		width = 3840;
	else
		width = stream->timing.h_addressable;	// 4K max

	if (stream->timing.v_addressable > 2160)
		height = 2160;
	else
		height = stream->timing.v_addressable;	// 4K max

	plane->cursor.cursor_bpp = 32;

	plane->cursor.cursor_width = 256;
	plane->cursor.num_cursors = 1;

	plane->composition.viewport.plane0.width = width;
	plane->composition.viewport.plane0.height = height;
	plane->composition.viewport.plane1.width = 0;
	plane->composition.viewport.plane1.height = 0;

	plane->composition.viewport.stationary = false;
	plane->composition.viewport.plane0.x_start = 0;
	plane->composition.viewport.plane0.y_start = 0;
	plane->composition.viewport.plane1.x_start = 0;
	plane->composition.viewport.plane1.y_start = 0;

	plane->composition.scaler_info.enabled = false;
	plane->composition.rotation_angle = dml2_rotation_0;
	plane->composition.scaler_info.plane0.h_ratio = 1.0;
	plane->composition.scaler_info.plane0.v_ratio = 1.0;
	plane->composition.scaler_info.plane1.h_ratio = 0;
	plane->composition.scaler_info.plane1.v_ratio = 0;
	plane->composition.scaler_info.plane0.h_taps = 1;
	plane->composition.scaler_info.plane0.v_taps = 1;
	plane->composition.scaler_info.plane1.h_taps = 0;
	plane->composition.scaler_info.plane1.v_taps = 0;
	plane->composition.scaler_info.rect_out_width = width;
	plane->pixel_format = dml2_444_32;

	plane->dynamic_meta_data.enable = false;
	plane->overrides.gpuvm_min_page_size_kbytes = 256;
}

static void populate_dml21_surface_config_from_plane_state(
		const struct dc *in_dc,
		struct dml2_surface_cfg *surface,
		const struct dc_plane_state *plane_state)
{
	surface->plane0.pitch = plane_state->plane_size.surface_pitch;
	surface->plane1.pitch = plane_state->plane_size.chroma_pitch;
	surface->plane0.height = plane_state->plane_size.surface_size.height;
	surface->plane0.width = plane_state->plane_size.surface_size.width;
	surface->plane1.height = plane_state->plane_size.chroma_size.height;
	surface->plane1.width = plane_state->plane_size.chroma_size.width;
	surface->dcc.enable = plane_state->dcc.enable;
	surface->dcc.informative.dcc_rate_plane0 = 1.0;
	surface->dcc.informative.dcc_rate_plane1 = 1.0;
	surface->dcc.informative.fraction_of_zero_size_request_plane0 = plane_state->dcc.independent_64b_blks;
	surface->dcc.informative.fraction_of_zero_size_request_plane1 = plane_state->dcc.independent_64b_blks_c;
	surface->dcc.plane0.pitch = plane_state->dcc.meta_pitch;
	surface->dcc.plane1.pitch = plane_state->dcc.meta_pitch_c;

	// Update swizzle / array mode based on the gfx_format
	switch (plane_state->tiling_info.gfxversion) {
	case DcGfxVersion7:
	case DcGfxVersion8:
		break;
	case DcGfxVersion9:
	case DcGfxVersion10:
	case DcGfxVersion11:
		surface->tiling = gfx9_to_dml2_swizzle_mode(plane_state->tiling_info.gfx9.swizzle);
		break;
	case DcGfxAddr3:
		surface->tiling = gfx_addr3_to_dml2_swizzle_mode(plane_state->tiling_info.gfx_addr3.swizzle);
		break;
	}
}

static const struct scaler_data *get_scaler_data_for_plane(
		struct dml2_context *dml_ctx,
		const struct dc_plane_state *in,
		const struct dc_state *context)
{
	int i;
	struct pipe_ctx *temp_pipe = &dml_ctx->v21.scratch.temp_pipe;

	memset(temp_pipe, 0, sizeof(struct pipe_ctx));

	for (i = 0; i < MAX_PIPES; i++)	{
		const struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state == in && !pipe->prev_odm_pipe) {
			temp_pipe->stream = pipe->stream;
			temp_pipe->plane_state = pipe->plane_state;
			temp_pipe->plane_res.scl_data.taps = pipe->plane_res.scl_data.taps;
			temp_pipe->stream_res = pipe->stream_res;
			temp_pipe->dsc_padding_params.dsc_hactive_padding = pipe->dsc_padding_params.dsc_hactive_padding;
			temp_pipe->dsc_padding_params.dsc_htotal_padding = pipe->dsc_padding_params.dsc_htotal_padding;
			temp_pipe->dsc_padding_params.dsc_pix_clk_100hz = pipe->dsc_padding_params.dsc_pix_clk_100hz;
			dml_ctx->config.callbacks.build_scaling_params(temp_pipe);
			break;
		}
	}

	ASSERT(i < MAX_PIPES);
	return &temp_pipe->plane_res.scl_data;
}

static void populate_dml21_plane_config_from_plane_state(struct dml2_context *dml_ctx,
		struct dml2_plane_parameters *plane, const struct dc_plane_state *plane_state,
		const struct dc_state *context, unsigned int stream_index)
{
	const struct scaler_data *scaler_data = get_scaler_data_for_plane(dml_ctx, plane_state, context);
	struct dc_stream_state *stream = context->streams[stream_index];

	plane->cursor.cursor_bpp = 32;
	plane->cursor.cursor_width = 256;
	plane->cursor.num_cursors = 1;

	switch (plane_state->format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		plane->pixel_format = dml2_420_8;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		plane->pixel_format = dml2_420_10;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		plane->pixel_format = dml2_444_64;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		plane->pixel_format = dml2_444_16;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		plane->pixel_format = dml2_444_8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		plane->pixel_format = dml2_rgbe_alpha;
		break;
	default:
		plane->pixel_format = dml2_444_32;
		break;
	}

	plane->composition.viewport.plane0.height = scaler_data->viewport.height;
	plane->composition.viewport.plane0.width = scaler_data->viewport.width;
	plane->composition.viewport.plane1.height = scaler_data->viewport_c.height;
	plane->composition.viewport.plane1.width = scaler_data->viewport_c.width;
	plane->composition.viewport.plane0.x_start = scaler_data->viewport.x;
	plane->composition.viewport.plane0.y_start = scaler_data->viewport.y;
	plane->composition.viewport.plane1.x_start = scaler_data->viewport_c.x;
	plane->composition.viewport.plane1.y_start = scaler_data->viewport_c.y;
	plane->composition.viewport.stationary = false;
	plane->composition.scaler_info.enabled = scaler_data->ratios.horz.value != dc_fixpt_one.value ||
		scaler_data->ratios.horz_c.value != dc_fixpt_one.value ||
		scaler_data->ratios.vert.value != dc_fixpt_one.value ||
		scaler_data->ratios.vert_c.value != dc_fixpt_one.value;

	if (!scaler_data->taps.h_taps) {
		/* Above logic determines scaling should be enabled even when there are no taps for
		 * certain cases. Hence do corrective active and disable scaling.
		 */
		plane->composition.scaler_info.enabled = false;
	} else if ((plane_state->ctx->dc->config.use_spl == true) &&
		(plane->composition.scaler_info.enabled == false)) {
		/* To enable sharpener for 1:1, scaler must be enabled.  If use_spl is set, then
		 *  allow case where ratio is 1 but taps > 1
		 */
		if ((scaler_data->taps.h_taps > 1) || (scaler_data->taps.v_taps > 1) ||
			(scaler_data->taps.h_taps_c > 1) || (scaler_data->taps.v_taps_c > 1))
			plane->composition.scaler_info.enabled = true;
	}

	/* always_scale is only used for debug purposes not used in production but has to be
	 * maintained for certain complainces. */
	if (plane_state->ctx->dc->debug.always_scale == true) {
		plane->composition.scaler_info.enabled = true;
	}

	if (plane->composition.scaler_info.enabled == false) {
		plane->composition.scaler_info.plane0.h_ratio = 1.0;
		plane->composition.scaler_info.plane0.v_ratio = 1.0;
		plane->composition.scaler_info.plane1.h_ratio = 1.0;
		plane->composition.scaler_info.plane1.v_ratio = 1.0;
	} else {
		plane->composition.scaler_info.plane0.h_ratio = (double)scaler_data->ratios.horz.value / (1ULL << 32);
		plane->composition.scaler_info.plane0.v_ratio = (double)scaler_data->ratios.vert.value / (1ULL << 32);
		plane->composition.scaler_info.plane1.h_ratio = (double)scaler_data->ratios.horz_c.value / (1ULL << 32);
		plane->composition.scaler_info.plane1.v_ratio = (double)scaler_data->ratios.vert_c.value / (1ULL << 32);
	}

	if (!scaler_data->taps.h_taps) {
		plane->composition.scaler_info.plane0.h_taps = 1;
		plane->composition.scaler_info.plane1.h_taps = 1;
	} else {
		plane->composition.scaler_info.plane0.h_taps = scaler_data->taps.h_taps;
		plane->composition.scaler_info.plane1.h_taps = scaler_data->taps.h_taps_c;
	}
	if (!scaler_data->taps.v_taps) {
		plane->composition.scaler_info.plane0.v_taps = 1;
		plane->composition.scaler_info.plane1.v_taps = 1;
	} else {
		plane->composition.scaler_info.plane0.v_taps = scaler_data->taps.v_taps;
		plane->composition.scaler_info.plane1.v_taps = scaler_data->taps.v_taps_c;
	}

	plane->composition.viewport.stationary = false;

	if (plane_state->mcm_luts.lut3d_data.lut3d_src == DC_CM2_TRANSFER_FUNC_SOURCE_VIDMEM) {
		plane->tdlut.setup_for_tdlut = true;

		switch (plane_state->mcm_luts.lut3d_data.gpu_mem_params.layout) {
		case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_RGB:
		case DC_CM2_GPU_MEM_LAYOUT_3D_SWIZZLE_LINEAR_BGR:
			plane->tdlut.tdlut_addressing_mode = dml2_tdlut_sw_linear;
			break;
		case DC_CM2_GPU_MEM_LAYOUT_1D_PACKED_LINEAR:
			plane->tdlut.tdlut_addressing_mode = dml2_tdlut_simple_linear;
			break;
		}

		switch (plane_state->mcm_luts.lut3d_data.gpu_mem_params.size) {
		case DC_CM2_GPU_MEM_SIZE_171717:
			plane->tdlut.tdlut_width_mode = dml2_tdlut_width_17_cube;
			break;
		case DC_CM2_GPU_MEM_SIZE_TRANSFORMED:
		default:
			//plane->tdlut.tdlut_width_mode = dml2_tdlut_width_flatten; // dml2_tdlut_width_flatten undefined
			break;
		}
	}
	plane->tdlut.setup_for_tdlut |= dml_ctx->config.force_tdlut_enable;

	plane->dynamic_meta_data.enable = false;
	plane->dynamic_meta_data.lines_before_active_required = 0;
	plane->dynamic_meta_data.transmitted_bytes = 0;

	plane->composition.scaler_info.rect_out_width = plane_state->dst_rect.width;
	plane->composition.rotation_angle = (enum dml2_rotation_angle) plane_state->rotation;
	plane->stream_index = stream_index;

	plane->overrides.gpuvm_min_page_size_kbytes = 256;

	plane->immediate_flip = plane_state->flip_immediate;

	plane->composition.rect_out_height_spans_vactive =
		plane_state->dst_rect.height >= stream->src.height &&
		stream->dst.height >= stream->timing.v_addressable;
}

//TODO : Could be possibly moved to a common helper layer.
static bool dml21_wrapper_get_plane_id(const struct dc_state *context, unsigned int stream_id, const struct dc_plane_state *plane, unsigned int *plane_id)
{
	int i, j;

	if (!plane_id)
		return false;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->stream_id == stream_id) {
			for (j = 0; j < context->stream_status[i].plane_count; j++) {
				if (context->stream_status[i].plane_states[j] == plane) {
					*plane_id = (i << 16) | j;
					return true;
				}
			}
		}
	}

	return false;
}

static unsigned int map_stream_to_dml21_display_cfg(const struct dml2_context *dml_ctx, const struct dc_stream_state *stream)
{
	int i = 0;
	int location = -1;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[i] && dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[i] == stream->stream_id) {
			location = i;
			break;
		}
	}

	return location;
}

unsigned int map_plane_to_dml21_display_cfg(const struct dml2_context *dml_ctx, unsigned int stream_id,
		const struct dc_plane_state *plane, const struct dc_state *context)
{
	unsigned int plane_id;
	int i = 0;
	int location = -1;

	if (!dml21_wrapper_get_plane_id(context, stream_id, plane, &plane_id)) {
		ASSERT(false);
		return -1;
	}

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[i] && dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[i] == plane_id) {
			location = i;
			break;
		}
	}

	return location;
}

static enum dml2_uclk_pstate_change_strategy dml21_force_pstate_method_to_uclk_state_change_strategy(enum dml2_force_pstate_methods force_pstate_method)
{
	enum dml2_uclk_pstate_change_strategy val = dml2_uclk_pstate_change_strategy_auto;

	switch (force_pstate_method) {
	case dml2_force_pstate_method_vactive:
		val = dml2_uclk_pstate_change_strategy_force_vactive;
		break;
	case dml2_force_pstate_method_vblank:
		val = dml2_uclk_pstate_change_strategy_force_vblank;
		break;
	case dml2_force_pstate_method_drr:
		val = dml2_uclk_pstate_change_strategy_force_drr;
		break;
	case dml2_force_pstate_method_subvp:
		val = dml2_uclk_pstate_change_strategy_force_mall_svp;
		break;
	case dml2_force_pstate_method_auto:
	default:
		val = dml2_uclk_pstate_change_strategy_auto;
	}

	return val;
}

bool dml21_map_dc_state_into_dml_display_cfg(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx)
{
	int stream_index, plane_index;
	int disp_cfg_stream_location, disp_cfg_plane_location;
	struct dml2_display_cfg *dml_dispcfg = &dml_ctx->v21.display_config;
	unsigned int plane_count = 0;

	memset(&dml_ctx->v21.dml_to_dc_pipe_mapping, 0, sizeof(struct dml2_dml_to_dc_pipe_mapping));

	dml_dispcfg->gpuvm_enable = dml_ctx->config.gpuvm_enable;
	dml_dispcfg->gpuvm_max_page_table_levels = 4;
	dml_dispcfg->hostvm_enable = false;
	dml_dispcfg->minimize_det_reallocation = true;
	dml_dispcfg->overrides.enable_subvp_implicit_pmo = true;

	if (in_dc->debug.disable_unbounded_requesting) {
		dml_dispcfg->overrides.hw.force_unbounded_requesting.enable = true;
		dml_dispcfg->overrides.hw.force_unbounded_requesting.value = false;
	}

	for (stream_index = 0; stream_index < context->stream_count; stream_index++) {
		disp_cfg_stream_location = map_stream_to_dml21_display_cfg(dml_ctx, context->streams[stream_index]);

		if (disp_cfg_stream_location < 0)
			disp_cfg_stream_location = dml_dispcfg->num_streams++;

		ASSERT(disp_cfg_stream_location >= 0 && disp_cfg_stream_location < __DML2_WRAPPER_MAX_STREAMS_PLANES__);
		populate_dml21_timing_config_from_stream_state(&dml_dispcfg->stream_descriptors[disp_cfg_stream_location].timing, context->streams[stream_index], &context->res_ctx.pipe_ctx[stream_index], dml_ctx);
		populate_dml21_output_config_from_stream_state(&dml_dispcfg->stream_descriptors[disp_cfg_stream_location].output, context->streams[stream_index], &context->res_ctx.pipe_ctx[stream_index]);
		populate_dml21_stream_overrides_from_stream_state(&dml_dispcfg->stream_descriptors[disp_cfg_stream_location], context->streams[stream_index], &context->stream_status[stream_index]);

		dml_dispcfg->stream_descriptors[disp_cfg_stream_location].overrides.hw.twait_budgeting.fclk_pstate = dml2_twait_budgeting_setting_if_needed;
		dml_dispcfg->stream_descriptors[disp_cfg_stream_location].overrides.hw.twait_budgeting.uclk_pstate = dml2_twait_budgeting_setting_if_needed;
		dml_dispcfg->stream_descriptors[disp_cfg_stream_location].overrides.hw.twait_budgeting.stutter_enter_exit = dml2_twait_budgeting_setting_if_needed;

		dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[disp_cfg_stream_location] = context->streams[stream_index]->stream_id;
		dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id_valid[disp_cfg_stream_location] = true;

		if (context->stream_status[stream_index].plane_count == 0) {
			disp_cfg_plane_location = dml_dispcfg->num_planes++;
			populate_dml21_dummy_surface_cfg(&dml_dispcfg->plane_descriptors[disp_cfg_plane_location].surface, context->streams[stream_index]);
			populate_dml21_dummy_plane_cfg(&dml_dispcfg->plane_descriptors[disp_cfg_plane_location], context->streams[stream_index]);
			dml_dispcfg->plane_descriptors[disp_cfg_plane_location].stream_index = disp_cfg_stream_location;
		} else {
			for (plane_index = 0; plane_index < context->stream_status[stream_index].plane_count; plane_index++) {
				disp_cfg_plane_location = map_plane_to_dml21_display_cfg(dml_ctx, context->streams[stream_index]->stream_id, context->stream_status[stream_index].plane_states[plane_index], context);

				if (disp_cfg_plane_location < 0)
					disp_cfg_plane_location = dml_dispcfg->num_planes++;

				ASSERT(disp_cfg_plane_location >= 0 && disp_cfg_plane_location < __DML2_WRAPPER_MAX_STREAMS_PLANES__);

				populate_dml21_surface_config_from_plane_state(in_dc, &dml_dispcfg->plane_descriptors[disp_cfg_plane_location].surface, context->stream_status[stream_index].plane_states[plane_index]);
				populate_dml21_plane_config_from_plane_state(dml_ctx, &dml_dispcfg->plane_descriptors[disp_cfg_plane_location], context->stream_status[stream_index].plane_states[plane_index], context, stream_index);
				dml_dispcfg->plane_descriptors[disp_cfg_plane_location].stream_index = disp_cfg_stream_location;

				if (dml21_wrapper_get_plane_id(context, context->streams[stream_index]->stream_id, context->stream_status[stream_index].plane_states[plane_index], &dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[disp_cfg_plane_location]))
					dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id_valid[disp_cfg_plane_location] = true;

				/* apply forced pstate policy */
				if (dml_ctx->config.pmo.force_pstate_method_enable) {
					dml_dispcfg->plane_descriptors[disp_cfg_plane_location].overrides.uclk_pstate_change_strategy =
							dml21_force_pstate_method_to_uclk_state_change_strategy(dml_ctx->config.pmo.force_pstate_method_values[stream_index]);
				}

				plane_count++;
			}
		}
	}

	if (plane_count == 0) {
		dml_dispcfg->overrides.all_streams_blanked = true;
	}

	return true;
}

void dml21_copy_clocks_to_dc_state(struct dml2_context *in_ctx, struct dc_state *context)
{
	/* TODO these should be the max of active, svp prefetch and idle should be tracked seperately */
	context->bw_ctx.bw.dcn.clk.dispclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.active.dcfclk_khz;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.active.uclk_khz;
	context->bw_ctx.bw.dcn.clk.fclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.active.fclk_khz;
	context->bw_ctx.bw.dcn.clk.idle_dramclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.idle.uclk_khz;
	context->bw_ctx.bw.dcn.clk.idle_fclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.idle.fclk_khz;
	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.deepsleep_dcfclk_khz;
	context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = in_ctx->v21.mode_programming.programming->fclk_pstate_supported;
	context->bw_ctx.bw.dcn.clk.p_state_change_support = in_ctx->v21.mode_programming.programming->uclk_pstate_supported;
	context->bw_ctx.bw.dcn.clk.dtbclk_en = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.dtbrefclk_khz > 0;
	context->bw_ctx.bw.dcn.clk.ref_dtbclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.dtbrefclk_khz;
	context->bw_ctx.bw.dcn.clk.socclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.socclk_khz;
	context->bw_ctx.bw.dcn.clk.subvp_prefetch_dramclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.svp_prefetch_no_throttle.uclk_khz;
	context->bw_ctx.bw.dcn.clk.subvp_prefetch_fclk_khz = in_ctx->v21.mode_programming.programming->min_clocks.dcn4x.svp_prefetch_no_throttle.fclk_khz;
	context->bw_ctx.bw.dcn.clk.stutter_efficiency.base_efficiency = in_ctx->v21.mode_programming.programming->stutter.base_percent_efficiency;
	context->bw_ctx.bw.dcn.clk.stutter_efficiency.low_power_efficiency = in_ctx->v21.mode_programming.programming->stutter.low_power_percent_efficiency;
}

static struct dml2_dchub_watermark_regs *wm_set_index_to_dc_wm_set(union dcn_watermark_set *watermarks, const enum dml2_dchub_watermark_reg_set_index wm_index)
{
	struct dml2_dchub_watermark_regs *wm_regs = NULL;

	switch (wm_index) {
	case DML2_DCHUB_WATERMARK_SET_A:
		wm_regs = &watermarks->dcn4x.a;
		break;
	case DML2_DCHUB_WATERMARK_SET_B:
		wm_regs = &watermarks->dcn4x.b;
		break;
	case DML2_DCHUB_WATERMARK_SET_C:
		wm_regs = &watermarks->dcn4x.c;
		break;
	case DML2_DCHUB_WATERMARK_SET_D:
		wm_regs = &watermarks->dcn4x.d;
		break;
	case DML2_DCHUB_WATERMARK_SET_NUM:
	default:
		/* invalid wm set index */
		wm_regs = NULL;
	}

	return wm_regs;
}

void dml21_extract_watermark_sets(const struct dc *in_dc, union dcn_watermark_set *watermarks, struct dml2_context *in_ctx)
{
	const struct dml2_display_cfg_programming *programming = in_ctx->v21.mode_programming.programming;

	unsigned int wm_index;

	/* copy watermark sets from DML */
	for (wm_index = 0; wm_index < programming->global_regs.num_watermark_sets; wm_index++) {
		struct dml2_dchub_watermark_regs *wm_regs = wm_set_index_to_dc_wm_set(watermarks, wm_index);

		if (wm_regs)
			memcpy(wm_regs,
				&programming->global_regs.wm_regs[wm_index],
				sizeof(struct dml2_dchub_watermark_regs));
	}
}

void dml21_map_hw_resources(struct dml2_context *dml_ctx)
{
	unsigned int i = 0;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[i] = dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_stream_id[i];
		dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[i] = true;
		dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[i] = dml_ctx->v21.dml_to_dc_pipe_mapping.disp_cfg_to_plane_id[i];
		dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id_valid[i] = true;
	}

}

void dml21_get_pipe_mcache_config(
	struct dc_state *context,
	struct pipe_ctx *pipe_ctx,
	struct dml2_per_plane_programming *pln_prog,
	struct dml2_pipe_configuration_descriptor *mcache_pipe_config)
{
	mcache_pipe_config->plane0.viewport_x_start = pipe_ctx->plane_res.scl_data.viewport.x;
	mcache_pipe_config->plane0.viewport_width = pipe_ctx->plane_res.scl_data.viewport.width;

	mcache_pipe_config->plane1.viewport_x_start = pipe_ctx->plane_res.scl_data.viewport_c.x;
	mcache_pipe_config->plane1.viewport_width = pipe_ctx->plane_res.scl_data.viewport_c.width;

	mcache_pipe_config->plane1_enabled =
			dml21_is_plane1_enabled(pln_prog->plane_descriptor->pixel_format);
}

void dml21_set_dc_p_state_type(
		struct pipe_ctx *pipe_ctx,
		struct dml2_per_stream_programming *stream_programming,
		bool sub_vp_enabled)
{
	switch (stream_programming->uclk_pstate_method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		pipe_ctx->p_state_type = P_STATE_V_ACTIVE;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		if (sub_vp_enabled)
			pipe_ctx->p_state_type = P_STATE_V_BLANK_SUB_VP;
		else
			pipe_ctx->p_state_type = P_STATE_V_BLANK;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
		pipe_ctx->p_state_type = P_STATE_SUB_VP;
		break;
	case dml2_pstate_method_fw_drr:
		if (sub_vp_enabled)
			pipe_ctx->p_state_type = P_STATE_DRR_SUB_VP;
		else
			pipe_ctx->p_state_type = P_STATE_FPO;
		break;
	default:
		pipe_ctx->p_state_type = P_STATE_UNKNOWN;
		break;
	}
}

