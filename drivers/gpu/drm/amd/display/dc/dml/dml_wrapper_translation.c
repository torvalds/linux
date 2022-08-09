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

#ifdef DML_WRAPPER_TRANSLATION_

static void gfx10array_mode_to_dml_params(
		enum array_mode_values array_mode,
		enum legacy_tiling_compat_level compat_level,
		unsigned int *sw_mode)
{
	switch (array_mode) {
	case DC_ARRAY_LINEAR_ALLIGNED:
	case DC_ARRAY_LINEAR_GENERAL:
		*sw_mode = dm_sw_linear;
		break;
	case DC_ARRAY_2D_TILED_THIN1:
// DC_LEGACY_TILING_ADDR_GEN_ZERO - undefined as per current code hence removed
#if 0
		if (compat_level == DC_LEGACY_TILING_ADDR_GEN_ZERO)
			*sw_mode = dm_sw_gfx7_2d_thin_l_vp;
		else
			*sw_mode = dm_sw_gfx7_2d_thin_gl;
#endif
		break;
	default:
		ASSERT(0); /* Not supported */
		break;
	}
}

static void swizzle_to_dml_params(
		enum swizzle_mode_values swizzle,
		unsigned int *sw_mode)
{
	switch (swizzle) {
	case DC_SW_LINEAR:
		*sw_mode = dm_sw_linear;
		break;
	case DC_SW_4KB_S:
		*sw_mode = dm_sw_4kb_s;
		break;
	case DC_SW_4KB_S_X:
		*sw_mode = dm_sw_4kb_s_x;
		break;
	case DC_SW_4KB_D:
		*sw_mode = dm_sw_4kb_d;
		break;
	case DC_SW_4KB_D_X:
		*sw_mode = dm_sw_4kb_d_x;
		break;
	case DC_SW_64KB_S:
		*sw_mode = dm_sw_64kb_s;
		break;
	case DC_SW_64KB_S_X:
		*sw_mode = dm_sw_64kb_s_x;
		break;
	case DC_SW_64KB_S_T:
		*sw_mode = dm_sw_64kb_s_t;
		break;
	case DC_SW_64KB_D:
		*sw_mode = dm_sw_64kb_d;
		break;
	case DC_SW_64KB_D_X:
		*sw_mode = dm_sw_64kb_d_x;
		break;
	case DC_SW_64KB_D_T:
		*sw_mode = dm_sw_64kb_d_t;
		break;
	case DC_SW_64KB_R_X:
		*sw_mode = dm_sw_64kb_r_x;
		break;
	case DC_SW_VAR_S:
		*sw_mode = dm_sw_var_s;
		break;
	case DC_SW_VAR_S_X:
		*sw_mode = dm_sw_var_s_x;
		break;
	case DC_SW_VAR_D:
		*sw_mode = dm_sw_var_d;
		break;
	case DC_SW_VAR_D_X:
		*sw_mode = dm_sw_var_d_x;
		break;

	default:
		ASSERT(0); /* Not supported */
		break;
	}
}

static void dc_timing_to_dml_timing(const struct dc_crtc_timing *timing, struct _vcs_dpi_display_pipe_dest_params_st *dest)
{
	dest->hblank_start = timing->h_total - timing->h_front_porch;
	dest->hblank_end = dest->hblank_start
			- timing->h_addressable
			- timing->h_border_left
			- timing->h_border_right;
	dest->vblank_start = timing->v_total - timing->v_front_porch;
	dest->vblank_end = dest->vblank_start
			- timing->v_addressable
			- timing->v_border_top
			- timing->v_border_bottom;
	dest->htotal = timing->h_total;
	dest->vtotal = timing->v_total;
	dest->hactive = timing->h_addressable;
	dest->vactive = timing->v_addressable;
	dest->interlaced = timing->flags.INTERLACE;
	dest->pixel_rate_mhz = timing->pix_clk_100hz/10000.0;
	if (timing->timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		dest->pixel_rate_mhz *= 2;
}

static enum odm_combine_mode get_dml_odm_combine(const struct pipe_ctx *pipe)
{
	int odm_split_count = 0;
	enum odm_combine_mode combine_mode = dm_odm_combine_mode_disabled;
	struct pipe_ctx *next_pipe = pipe->next_odm_pipe;

	// Traverse pipe tree to determine odm split count
	while (next_pipe) {
		odm_split_count++;
		next_pipe = next_pipe->next_odm_pipe;
	}
	pipe = pipe->prev_odm_pipe;
	while (pipe) {
		odm_split_count++;
		pipe = pipe->prev_odm_pipe;
	}

	// Translate split to DML odm combine factor
	switch (odm_split_count) {
	case 1:
		combine_mode = dm_odm_combine_mode_2to1;
		break;
	case 3:
		combine_mode = dm_odm_combine_mode_4to1;
		break;
	default:
		combine_mode = dm_odm_combine_mode_disabled;
	}

	return combine_mode;
}

static int get_dml_output_type(enum signal_type dc_signal)
{
	int dml_output_type = -1;

	switch (dc_signal) {
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DISPLAY_PORT:
		dml_output_type = dm_dp;
		break;
	case SIGNAL_TYPE_EDP:
		dml_output_type = dm_edp;
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		dml_output_type = dm_hdmi;
		break;
	default:
		break;
	}

	return dml_output_type;
}

static void populate_color_depth_and_encoding_from_timing(const struct dc_crtc_timing *timing, struct _vcs_dpi_display_output_params_st *dout)
{
	int output_bpc = 0;

	switch (timing->display_color_depth) {
	case COLOR_DEPTH_666:
		output_bpc = 6;
		break;
	case COLOR_DEPTH_888:
		output_bpc = 8;
		break;
	case COLOR_DEPTH_101010:
		output_bpc = 10;
		break;
	case COLOR_DEPTH_121212:
		output_bpc = 12;
		break;
	case COLOR_DEPTH_141414:
		output_bpc = 14;
		break;
	case COLOR_DEPTH_161616:
		output_bpc = 16;
		break;
	case COLOR_DEPTH_999:
		output_bpc = 9;
		break;
	case COLOR_DEPTH_111111:
		output_bpc = 11;
		break;
	default:
		output_bpc = 8;
		break;
	}

	switch (timing->pixel_encoding) {
	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
		dout->output_format = dm_444;
		dout->output_bpp = output_bpc * 3;
		break;
	case PIXEL_ENCODING_YCBCR420:
		dout->output_format = dm_420;
		dout->output_bpp = (output_bpc * 3.0) / 2;
		break;
	case PIXEL_ENCODING_YCBCR422:
		if (timing->flags.DSC && !timing->dsc_cfg.ycbcr422_simple)
			dout->output_format = dm_n422;
		else
			dout->output_format = dm_s422;
		dout->output_bpp = output_bpc * 2;
		break;
	default:
		dout->output_format = dm_444;
		dout->output_bpp = output_bpc * 3;
	}
}

static enum source_format_class dc_source_format_to_dml_source_format(enum surface_pixel_format dc_format)
{
	enum source_format_class dml_format = dm_444_32;

	switch (dc_format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dml_format = dm_420_8;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		dml_format = dm_420_10;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		dml_format = dm_444_64;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		dml_format = dm_444_16;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		dml_format = dm_444_8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		dml_format = dm_rgbe_alpha;
		break;
	default:
		dml_format = dm_444_32;
		break;
	}

	return dml_format;
}

#endif
