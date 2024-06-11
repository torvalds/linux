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

#include "dm_services.h"
#include "core_types.h"
#include "reg_helper.h"
#include "dcn401/dcn401_dpp.h"
#include "basics/conversion.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn32/dcn32_dpp.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

void dpp401_read_state(struct dpp *dpp_base, struct dcn_dpp_state *s)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_GET(DPP_CONTROL,
		DPP_CLOCK_ENABLE, &s->is_enabled);

	// TODO: Implement for DCN4
}

void dpp401_dpp_setup(
	struct dpp *dpp_base,
	enum surface_pixel_format format,
	enum expansion_mode mode,
	struct dc_csc_transform input_csc_color_matrix,
	enum dc_color_space input_color_space,
	struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn401_dpp *dpp = TO_DCN401_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn10_input_csc_select select = INPUT_CSC_SELECT_BYPASS;
	uint32_t is_2bit = 0;
	uint32_t alpha_plane_enable = 0;
	uint32_t dealpha_en = 0, dealpha_ablnd_en = 0;
	uint32_t realpha_en = 0, realpha_ablnd_en = 0;
	uint32_t program_prealpha_dealpha = 0;
	struct out_csc_color_matrix tbl_entry;
	int i;

	REG_SET_2(FORMAT_CONTROL, 0,
		CNVC_BYPASS, 0,
		FORMAT_EXPANSION_MODE, mode);

	REG_UPDATE(FORMAT_CONTROL, FORMAT_CNV16, 0);
	REG_UPDATE(FORMAT_CONTROL, CNVC_BYPASS_MSB_ALIGN, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE_C, 0);

	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_R, 0);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_G, 1);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CROSSBAR_B, 2);

	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		pixel_format = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		pixel_format = 3;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		pixel_format = 8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		pixel_format = 10;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		pixel_format = 66;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
		pixel_format = 26; /* ARGB16161616_UNORM */
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
		pixel_format = 24;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		pixel_format = 25;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
		pixel_format = 12;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
		pixel_format = 112;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
		pixel_format = 113;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
		pixel_format = 114;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
		pixel_format = 115;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
		pixel_format = 116;
		alpha_plane_enable = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		pixel_format = 116;
		alpha_plane_enable = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
		pixel_format = 118;
		alpha_en = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		pixel_format = 119;
		alpha_en = 0;
		break;
	default:
		break;
	}

	/* Set default color space based on format if none is given. */
	color_space = input_color_space ? input_color_space : color_space;

	if (is_2bit == 1 && alpha_2bit_lut != NULL) {
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT0, alpha_2bit_lut->lut0);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT1, alpha_2bit_lut->lut1);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT2, alpha_2bit_lut->lut2);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT3, alpha_2bit_lut->lut3);
	}

	REG_SET_2(CNVC_SURFACE_PIXEL_FORMAT, 0,
		CNVC_SURFACE_PIXEL_FORMAT, pixel_format,
		CNVC_ALPHA_PLANE_ENABLE, alpha_plane_enable);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

	if (program_prealpha_dealpha) {
		dealpha_en = 1;
		realpha_en = 1;
	}
	REG_SET_2(PRE_DEALPHA, 0,
		PRE_DEALPHA_EN, dealpha_en,
		PRE_DEALPHA_ABLND_EN, dealpha_ablnd_en);
	REG_SET_2(PRE_REALPHA, 0,
		PRE_REALPHA_EN, realpha_en,
		PRE_REALPHA_ABLND_EN, realpha_ablnd_en);

	/* If input adjustment exists, program the ICSC with those values. */
	if (input_csc_color_matrix.enable_adjustment == true) {
		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] = input_csc_color_matrix.matrix[i];

		tbl_entry.color_space = input_color_space;

		if (color_space >= COLOR_SPACE_YCBCR601)
			select = INPUT_CSC_SELECT_ICSC;
		else
			select = INPUT_CSC_SELECT_BYPASS;

		dpp3_program_post_csc(dpp_base, color_space, select,
			&tbl_entry);
	} else {
		dpp3_program_post_csc(dpp_base, color_space, select, NULL);
	}
}


static struct dpp_funcs dcn401_dpp_funcs = {
	.dpp_program_gamcor_lut		= dpp3_program_gamcor_lut,
	.dpp_read_state				= dpp401_read_state,
	.dpp_reset					= dpp_reset,
	.dpp_set_scaler				= dpp401_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_gamut_remap		= NULL,
	.dpp_set_csc_adjustment		= NULL,
	.dpp_set_csc_default		= NULL,
	.dpp_program_regamma_pwl	= NULL,
	.dpp_set_pre_degam			= dpp3_set_pre_degam,
	.dpp_program_input_lut		= NULL,
	.dpp_full_bypass			= dpp401_full_bypass,
	.dpp_setup					= dpp401_dpp_setup,
	.dpp_program_degamma_pwl	= NULL,
	.dpp_program_cm_dealpha		= dpp3_program_cm_dealpha,
	.dpp_program_cm_bias		= dpp3_program_cm_bias,

	.dpp_program_blnd_lut		= NULL, // BLNDGAM is removed completely in DCN3.2 DPP
	.dpp_program_shaper_lut		= NULL, // CM SHAPER block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)
	.dpp_program_3dlut			= NULL, // CM 3DLUT block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)

	.dpp_program_bias_and_scale	= NULL,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp401_set_cursor_attributes,
	.set_cursor_position		= dpp401_set_cursor_position,
	.set_optional_cursor_attributes	= dpp401_set_optional_cursor_attributes,
	.dpp_dppclk_control			= dpp1_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
	.set_cursor_matrix			= dpp401_set_cursor_matrix,
};


static struct dpp_caps dcn401_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.max_lb_partitions = 63,
	.dscl_calc_lb_num_partitions = dscl401_calc_lb_num_partitions,
};

bool dpp401_construct(
	struct dcn401_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn401_dpp_registers *tf_regs,
	const struct dcn401_dpp_shift *tf_shift,
	const struct dcn401_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn401_dpp_funcs;
	dpp->base.caps = &dcn401_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	return true;
}
/* Compute the maximum number of lines that we can fit in the line buffer */

void dscl401_calc_lb_num_partitions(
		const struct scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c)
{
	int memory_line_size_y, memory_line_size_c, memory_line_size_a,
	lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a;

	int line_size = scl_data->viewport.width < scl_data->recout.width ?
			scl_data->viewport.width : scl_data->recout.width;
	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
			scl_data->viewport_c.width : scl_data->recout.width;

	if (line_size == 0)
		line_size = 1;

	if (line_size_c == 0)
		line_size_c = 1;

	memory_line_size_y = (line_size + 5) / 6; /* +5 to ceil */
	memory_line_size_c = (line_size_c + 5) / 6; /* +5 to ceil */
	memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */

	if (lb_config == LB_MEMORY_CONFIG_1) {
		lb_memory_size = 970;
		lb_memory_size_c = 970;
		lb_memory_size_a = 970;
	} else if (lb_config == LB_MEMORY_CONFIG_2) {
		lb_memory_size = 1290;
		lb_memory_size_c = 1290;
		lb_memory_size_a = 1290;
	} else if (lb_config == LB_MEMORY_CONFIG_3) {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170 + 1170 + 1170;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			lb_memory_size = 970 + 1290 + 484 + 484 + 484;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	} else {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170;
			lb_memory_size_c = 970 + 1290 + 1170;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			lb_memory_size = 970 + 1290 + 484;
			lb_memory_size_c = 970 + 1290 + 484;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	}
	*num_part_y = lb_memory_size / memory_line_size_y;
	*num_part_c = lb_memory_size_c / memory_line_size_c;
	num_partitions_a = lb_memory_size_a / memory_line_size_a;

	if (scl_data->lb_params.alpha_en
			&& (num_partitions_a < *num_part_y))
		*num_part_y = num_partitions_a;

	if (*num_part_y > 64)
		*num_part_y = 64;
	if (*num_part_c > 64)
		*num_part_c = 64;
}

/* Compute the maximum number of lines that we can fit in the line buffer */
void dscl401_spl_calc_lb_num_partitions(
		bool alpha_en,
		const struct spl_scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c)
{
	int memory_line_size_y, memory_line_size_c, memory_line_size_a,
	lb_memory_size, lb_memory_size_c, lb_memory_size_a, num_partitions_a;

	int line_size = scl_data->viewport.width < scl_data->recout.width ?
			scl_data->viewport.width : scl_data->recout.width;
	int line_size_c = scl_data->viewport_c.width < scl_data->recout.width ?
			scl_data->viewport_c.width : scl_data->recout.width;

	if (line_size == 0)
		line_size = 1;

	if (line_size_c == 0)
		line_size_c = 1;

	memory_line_size_y = (line_size + 5) / 6; /* +5 to ceil */
	memory_line_size_c = (line_size_c + 5) / 6; /* +5 to ceil */
	memory_line_size_a = (line_size + 5) / 6; /* +5 to ceil */

	if (lb_config == LB_MEMORY_CONFIG_1) {
		lb_memory_size = 970;
		lb_memory_size_c = 970;
		lb_memory_size_a = 970;
	} else if (lb_config == LB_MEMORY_CONFIG_2) {
		lb_memory_size = 1290;
		lb_memory_size_c = 1290;
		lb_memory_size_a = 1290;
	} else if (lb_config == LB_MEMORY_CONFIG_3) {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170 + 1170 + 1170;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			/* 420 mode: luma using all 3 mem from Y, plus 3rd mem from Cr and Cb */
			lb_memory_size = 970 + 1290 + 484 + 484 + 484;
			lb_memory_size_c = 970 + 1290;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	} else {
		if (scl_data->viewport.width  == scl_data->h_active &&
			scl_data->viewport.height == scl_data->v_active) {
			/* use increased LB size for calculation only if Scaler not enabled */
			lb_memory_size = 970 + 1290 + 1170;
			lb_memory_size_c = 970 + 1290 + 1170;
			lb_memory_size_a = 970 + 1290 + 1170;
		} else {
			lb_memory_size = 970 + 1290 + 484;
			lb_memory_size_c = 970 + 1290 + 484;
			lb_memory_size_a = 970 + 1290 + 484;
		}
	}
	*num_part_y = lb_memory_size / memory_line_size_y;
	*num_part_c = lb_memory_size_c / memory_line_size_c;
	num_partitions_a = lb_memory_size_a / memory_line_size_a;

	if (alpha_en && (num_partitions_a < *num_part_y))
		*num_part_y = num_partitions_a;

	if (*num_part_y > 64)
		*num_part_y = 64;
	if (*num_part_c > 64)
		*num_part_c = 64;
}
