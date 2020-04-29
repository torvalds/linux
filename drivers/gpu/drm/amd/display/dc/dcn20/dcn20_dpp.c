/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "dcn20_dpp.h"
#include "basics/conversion.h"

#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

void dpp20_read_state(struct dpp *dpp_base,
		struct dcn_dpp_state *s)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET(DPP_CONTROL,
			DPP_CLOCK_ENABLE, &s->is_enabled);
	REG_GET(CM_DGAM_CONTROL,
			CM_DGAM_LUT_MODE, &s->dgam_lut_mode);
	// BGAM has no ROM, and definition is different, can't reuse same dump
	//REG_GET(CM_BLNDGAM_CONTROL,
	//		CM_BLNDGAM_LUT_MODE, &s->rgam_lut_mode);
	REG_GET(CM_GAMUT_REMAP_CONTROL,
			CM_GAMUT_REMAP_MODE, &s->gamut_remap_mode);
	if (s->gamut_remap_mode) {
		s->gamut_remap_c11_c12 = REG_READ(CM_GAMUT_REMAP_C11_C12);
		s->gamut_remap_c13_c14 = REG_READ(CM_GAMUT_REMAP_C13_C14);
		s->gamut_remap_c21_c22 = REG_READ(CM_GAMUT_REMAP_C21_C22);
		s->gamut_remap_c23_c24 = REG_READ(CM_GAMUT_REMAP_C23_C24);
		s->gamut_remap_c31_c32 = REG_READ(CM_GAMUT_REMAP_C31_C32);
		s->gamut_remap_c33_c34 = REG_READ(CM_GAMUT_REMAP_C33_C34);
	}
}

void dpp2_power_on_obuf(
		struct dpp *dpp_base,
	bool power_on)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(CM_MEM_PWR_CTRL, SHARED_MEM_PWR_DIS, power_on == true ? 1:0);

	REG_UPDATE(OBUF_MEM_PWR_CTRL,
			OBUF_MEM_PWR_FORCE, power_on == true ? 0:1);

	REG_UPDATE(DSCL_MEM_PWR_CTRL,
			LUT_MEM_PWR_FORCE, power_on == true ? 0:1);
}

void dpp2_dummy_program_input_lut(
		struct dpp *dpp_base,
		const struct dc_gamma *gamma)
{}

static void dpp2_cnv_setup (
		struct dpp *dpp_base,
		enum surface_pixel_format format,
		enum expansion_mode mode,
		struct dc_csc_transform input_csc_color_matrix,
		enum dc_color_space input_color_space,
		struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	uint32_t pixel_format = 0;
	uint32_t alpha_en = 1;
	enum dc_color_space color_space = COLOR_SPACE_SRGB;
	enum dcn20_input_csc_select select = DCN2_ICSC_SELECT_BYPASS;
	bool force_disable_cursor = false;
	struct out_csc_color_matrix tbl_entry;
	uint32_t is_2bit = 0;
	int i = 0;

	REG_SET_2(FORMAT_CONTROL, 0,
		CNVC_BYPASS, 0,
		FORMAT_EXPANSION_MODE, mode);

	//hardcode default
    //FORMAT_CONTROL. FORMAT_CNV16                                 	default 0: U0.16/S.1.15;         1: U1.15/ S.1.14
    //FORMAT_CONTROL. CNVC_BYPASS_MSB_ALIGN          				default 0: disabled              1: enabled
    //FORMAT_CONTROL. CLAMP_POSITIVE                               	default 0: disabled              1: enabled
    //FORMAT_CONTROL. CLAMP_POSITIVE_C                          	default 0: disabled              1: enabled
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CNV16, 0);
	REG_UPDATE(FORMAT_CONTROL, CNVC_BYPASS_MSB_ALIGN, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE, 0);
	REG_UPDATE(FORMAT_CONTROL, CLAMP_POSITIVE_C, 0);

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
		force_disable_cursor = false;
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		force_disable_cursor = true;
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		force_disable_cursor = true;
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		force_disable_cursor = true;
		pixel_format = 66;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		pixel_format = 22;
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
		select = DCN2_ICSC_SELECT_ICSC_A;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
		pixel_format = 112;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
		pixel_format = 113;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
		pixel_format = 114;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
		pixel_format = 115;
		color_space = COLOR_SPACE_YCBCR709;
		select = DCN2_ICSC_SELECT_ICSC_A;
		is_2bit = 1;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
		pixel_format = 118;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
		pixel_format = 119;
		break;
	default:
		break;
	}

	if (is_2bit == 1 && alpha_2bit_lut != NULL) {
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT0, alpha_2bit_lut->lut0);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT1, alpha_2bit_lut->lut1);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT2, alpha_2bit_lut->lut2);
		REG_UPDATE(ALPHA_2BIT_LUT, ALPHA_2BIT_LUT3, alpha_2bit_lut->lut3);
	}

	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, pixel_format);
	REG_UPDATE(FORMAT_CONTROL, FORMAT_CONTROL__ALPHA_EN, alpha_en);

	// if input adjustments exist, program icsc with those values
	if (input_csc_color_matrix.enable_adjustment
				== true) {
		for (i = 0; i < 12; i++)
			tbl_entry.regval[i] = input_csc_color_matrix.matrix[i];

		tbl_entry.color_space = input_color_space;

		if (color_space >= COLOR_SPACE_YCBCR601)
			select = DCN2_ICSC_SELECT_ICSC_A;
		else
			select = DCN2_ICSC_SELECT_BYPASS;

		dpp2_program_input_csc(dpp_base, color_space, select, &tbl_entry);
	} else
	dpp2_program_input_csc(dpp_base, color_space, select, NULL);

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);

	}
	dpp2_power_on_obuf(dpp_base, true);

}

void dpp2_cnv_set_bias_scale(
		struct dpp *dpp_base,
		struct  dc_bias_and_scale *bias_and_scale)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(FCNV_FP_BIAS_R, FCNV_FP_BIAS_R, bias_and_scale->bias_red);
	REG_UPDATE(FCNV_FP_BIAS_G, FCNV_FP_BIAS_G, bias_and_scale->bias_green);
	REG_UPDATE(FCNV_FP_BIAS_B, FCNV_FP_BIAS_B, bias_and_scale->bias_blue);
	REG_UPDATE(FCNV_FP_SCALE_R, FCNV_FP_SCALE_R, bias_and_scale->scale_red);
	REG_UPDATE(FCNV_FP_SCALE_G, FCNV_FP_SCALE_G, bias_and_scale->scale_green);
	REG_UPDATE(FCNV_FP_SCALE_B, FCNV_FP_SCALE_B, bias_and_scale->scale_blue);
}

/*compute the maximum number of lines that we can fit in the line buffer*/
void dscl2_calc_lb_num_partitions(
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
		/* 420 mode: using 3rd mem from Y, Cr and Cb */
		lb_memory_size = 970 + 1290 + 484 + 484 + 484;
		lb_memory_size_c = 970 + 1290;
		lb_memory_size_a = 970 + 1290 + 484;
	} else {
		lb_memory_size = 970 + 1290 + 484;
		lb_memory_size_c = 970 + 1290 + 484;
		lb_memory_size_a = 970 + 1290 + 484;
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

void dpp2_cnv_set_alpha_keyer(
		struct dpp *dpp_base,
		struct cnv_color_keyer_params *color_keyer)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(COLOR_KEYER_CONTROL, COLOR_KEYER_EN, color_keyer->color_keyer_en);

	REG_UPDATE(COLOR_KEYER_CONTROL, COLOR_KEYER_MODE, color_keyer->color_keyer_mode);

	REG_UPDATE(COLOR_KEYER_ALPHA, COLOR_KEYER_ALPHA_LOW, color_keyer->color_keyer_alpha_low);
	REG_UPDATE(COLOR_KEYER_ALPHA, COLOR_KEYER_ALPHA_HIGH, color_keyer->color_keyer_alpha_high);

	REG_UPDATE(COLOR_KEYER_RED, COLOR_KEYER_RED_LOW, color_keyer->color_keyer_red_low);
	REG_UPDATE(COLOR_KEYER_RED, COLOR_KEYER_RED_HIGH, color_keyer->color_keyer_red_high);

	REG_UPDATE(COLOR_KEYER_GREEN, COLOR_KEYER_GREEN_LOW, color_keyer->color_keyer_green_low);
	REG_UPDATE(COLOR_KEYER_GREEN, COLOR_KEYER_GREEN_HIGH, color_keyer->color_keyer_green_high);

	REG_UPDATE(COLOR_KEYER_BLUE, COLOR_KEYER_BLUE_LOW, color_keyer->color_keyer_blue_low);
	REG_UPDATE(COLOR_KEYER_BLUE, COLOR_KEYER_BLUE_HIGH, color_keyer->color_keyer_blue_high);
}

void dpp2_set_cursor_attributes(
		struct dpp *dpp_base,
		struct dc_cursor_attributes *cursor_attributes)
{
	enum dc_cursor_color_format color_format = cursor_attributes->color_format;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	int cur_rom_en = 0;

	if (color_format == CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA ||
		color_format == CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA) {
		if (cursor_attributes->attribute_flags.bits.ENABLE_CURSOR_DEGAMMA) {
			cur_rom_en = 1;
		}
	}

	REG_UPDATE_3(CURSOR0_CONTROL,
			CUR0_MODE, color_format,
			CUR0_EXPANSION_MODE, 0,
			CUR0_ROM_EN, cur_rom_en);

	if (color_format == CURSOR_MODE_MONO) {
		/* todo: clarify what to program these to */
		REG_UPDATE(CURSOR0_COLOR0,
				CUR0_COLOR0, 0x00000000);
		REG_UPDATE(CURSOR0_COLOR1,
				CUR0_COLOR1, 0xFFFFFFFF);
	}
}

void oppn20_dummy_program_regamma_pwl(
		struct dpp *dpp,
		const struct pwl_params *params,
		enum opp_regamma mode)
{}

static struct dpp_funcs dcn20_dpp_funcs = {
	.dpp_read_state = dpp20_read_state,
	.dpp_reset = dpp_reset,
	.dpp_set_scaler = dpp1_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps = dpp1_get_optimal_number_of_taps,
	.dpp_set_gamut_remap = dpp2_cm_set_gamut_remap,
	.dpp_set_csc_adjustment = NULL,
	.dpp_set_csc_default = NULL,
	.dpp_program_regamma_pwl = oppn20_dummy_program_regamma_pwl,
	.dpp_set_degamma		= dpp2_set_degamma,
	.dpp_program_input_lut		= dpp2_dummy_program_input_lut,
	.dpp_full_bypass		= dpp1_full_bypass,
	.dpp_setup			= dpp2_cnv_setup,
	.dpp_program_degamma_pwl	= dpp2_set_degamma_pwl,
	.dpp_program_blnd_lut = dpp20_program_blnd_lut,
	.dpp_program_shaper_lut = dpp20_program_shaper,
	.dpp_program_3dlut = dpp20_program_3dlut,
	.dpp_program_bias_and_scale = NULL,
	.dpp_cnv_set_alpha_keyer = dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes = dpp2_set_cursor_attributes,
	.set_cursor_position = dpp1_set_cursor_position,
	.set_optional_cursor_attributes = dpp1_cnv_set_optional_cursor_attributes,
	.dpp_dppclk_control = dpp1_dppclk_control,
	.dpp_set_hdr_multiplier = dpp2_set_hdr_multiplier,
};

static struct dpp_caps dcn20_dpp_cap = {
	.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT,
	.dscl_calc_lb_num_partitions = dscl2_calc_lb_num_partitions,
};

bool dpp2_construct(
	struct dcn20_dpp *dpp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn2_dpp_registers *tf_regs,
	const struct dcn2_dpp_shift *tf_shift,
	const struct dcn2_dpp_mask *tf_mask)
{
	dpp->base.ctx = ctx;

	dpp->base.inst = inst;
	dpp->base.funcs = &dcn20_dpp_funcs;
	dpp->base.caps = &dcn20_dpp_cap;

	dpp->tf_regs = tf_regs;
	dpp->tf_shift = tf_shift;
	dpp->tf_mask = tf_mask;

	dpp->lb_pixel_depth_supported =
		LB_PIXEL_DEPTH_18BPP |
		LB_PIXEL_DEPTH_24BPP |
		LB_PIXEL_DEPTH_30BPP;

	dpp->lb_bits_per_entry = LB_BITS_PER_ENTRY;
	dpp->lb_memory_size = LB_TOTAL_NUMBER_OF_ENTRIES; /*0x1404*/

	return true;
}

