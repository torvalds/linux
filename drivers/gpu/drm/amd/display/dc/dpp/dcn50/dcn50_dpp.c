// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "dm_services.h"
#include "core_types.h"
#include "reg_helper.h"
#include "dcn401/dcn401_dpp.h"
#include "dcn50_dpp.h"
#include "basics/conversion.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn35/dcn35_dpp.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

void dpp50_set_pregam_state(
	struct dpp *dpp_base,
	enum dc_transfer_func_predefined tr,
	enum dc_scaling_linearity scaling)
{
	struct dcn50_dpp *dpp = TO_DCN50_DPP(dpp_base);
	enum pregam_mode pre_degam_en = PREGAM_DEGAM;
	enum degam_lut degamma_lut_selection = 0;

	if (scaling == DC_SCALING_LINEARITY_SOURCE) {
		//If scaling in non-linear, apply regamma
		REG_SET_2(PRE_GAM, 0,
			PRE_GAM_MODE, PREGAM_REGAM,
			PRE_REGAM_SELECT, REGAM_20);
	} else {
		//If scaling in linear, apply degamma based on TF
		switch (tr) {
		case TRANSFER_FUNCTION_SRGB:
			degamma_lut_selection = DEGAM_SRGB;
			break;
		case TRANSFER_FUNCTION_GAMMA22:
			degamma_lut_selection = DEGAM_GAMMA_22;
			break;
		case TRANSFER_FUNCTION_GAMMA24:
			degamma_lut_selection = DEGAM_GAMMA_24;
			break;
		case TRANSFER_FUNCTION_GAMMA26:
			degamma_lut_selection = DEGAM_GAMMA_26;
			break;
		case TRANSFER_FUNCTION_BT709:
			degamma_lut_selection = DEGAM_BT2020;
			break;
		case TRANSFER_FUNCTION_PQ:
			degamma_lut_selection = DEGAM_BT2100PQ;
			break;
		case TRANSFER_FUNCTION_HLG:
			degamma_lut_selection = DEGAM_BT2100HLG;
			break;
		case TRANSFER_FUNCTION_LINEAR:
		case TRANSFER_FUNCTION_UNITY:
		case TRANSFER_FUNCTION_HLG12:
		default:
			pre_degam_en = PREGAM_BYPASS;
			break;
		}

		REG_SET_2(PRE_GAM, 0,
			PRE_GAM_MODE, pre_degam_en,
			PRE_DEGAM_SELECT, degamma_lut_selection);
	}
}

void dpp50_dpp_setup(
	struct dpp *dpp_base,
	enum surface_pixel_format format,
	enum expansion_mode mode,
	struct dc_csc_transform input_csc_color_matrix,
	enum dc_color_space input_color_space,
	struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn50_dpp *dpp = TO_DCN50_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn10_input_csc_select select = INPUT_CSC_SELECT_BYPASS;
	uint32_t is_2bit = 0;
	uint32_t alpha_plane_enable = 0;
	uint32_t dealpha_en = 0, dealpha_ablnd_en = 0;
	uint32_t realpha_en = 0, realpha_ablnd_en = 0;
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
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P208:
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P208:
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P210:
		pixel_format = 66;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P210:
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrCb_P212:
		pixel_format = 68;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbCr_P212:
		pixel_format = 69;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	/* Packed 422 formats*/
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
		pixel_format = 72;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr:
		pixel_format = 73;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
		pixel_format = 74;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
		pixel_format = 75;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
		pixel_format = 76;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
		pixel_format = 77;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
		pixel_format = 78;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
		pixel_format = 79;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
		pixel_format = 80;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
		pixel_format = 81;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
		pixel_format = 82;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
		pixel_format = 83;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		// color space and select subjected to change
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

		if (dpp3_should_bypass_post_csc_for_colorspace(color_space))
			select = INPUT_CSC_SELECT_BYPASS;
		else
			select = INPUT_CSC_SELECT_ICSC;

		dpp3_program_post_csc(dpp_base, color_space, select,
			&tbl_entry);
	} else {
		dpp3_program_post_csc(dpp_base, color_space, select, NULL);
	}
}

static struct dpp_funcs dcn50_dpp_funcs = {
	.dpp_program_gamcor_lut		= dpp3_program_gamcor_lut,
	.dpp_read_state				= dpp401_read_state,
	.dpp_reset					= dpp_reset,
	.dpp_set_scaler				= dpp401_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_gamut_remap		= NULL,
	.dpp_set_csc_adjustment		= NULL,
	.dpp_set_csc_default		= NULL,
	.dpp_program_regamma_pwl	= NULL,
	.dpp_set_pre_degam			= NULL,
	.dpp_program_input_lut		= NULL,
	.dpp_full_bypass			= NULL,
	.dpp_setup					= dpp50_dpp_setup,
	.dpp_program_degamma_pwl	= NULL,
	.dpp_program_cm_dealpha		= dpp3_program_cm_dealpha,
	.dpp_program_cm_bias		= dpp3_program_cm_bias,

	.dpp_program_blnd_lut		= NULL, // BLNDGAM is removed completely in DCN3.2 DPP
	.dpp_program_shaper_lut		= NULL, // CM SHAPER block is removed in DCN3.2 DPP,
									//(it is in MPCC, programmable before or after BLND)
	.dpp_program_3dlut			= NULL, // CM 3DLUT block is removed in DCN3.2 DPP,
									//(it is in MPCC, programmable before or after BLND)

	.dpp_program_bias_and_scale	= dpp35_program_bias_and_scale_fcnv,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp401_set_cursor_attributes,
	.set_cursor_position		= dpp401_set_cursor_position,
	.set_optional_cursor_attributes	= dpp401_set_optional_cursor_attributes,
	.dpp_dppclk_control			= dpp1_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
	.dpp_read_reg_state			= dpp30_read_reg_state,
	.set_cursor_matrix			= dpp401_set_cursor_matrix,
	.dpp_set_pregam_state		= dpp50_set_pregam_state,
};

static struct dpp_caps dcn50_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.max_lb_partitions = 63,
	.dscl_calc_lb_num_partitions = dscl401_calc_lb_num_partitions,
};

bool dpp50_construct(
	struct dcn50_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn50_dpp_registers *tf_regs,
	const struct dcn50_dpp_shift *tf_shift,
	const struct dcn50_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn50_dpp_funcs;
	dpp->base.caps = &dcn50_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	return true;
}
