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
#include "dcn10_dpp.h"
#include "basics/conversion.h"
#include "dcn10_cm_common.h"

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

struct dcn10_input_csc_matrix {
	enum dc_color_space color_space;
	uint16_t regval[12];
};

enum dcn10_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5
};

enum dscl_autocal_mode {
	AUTOCAL_MODE_OFF = 0,

	/* Autocal calculate the scaling ratio and initial phase and the
	 * DSCL_MODE_SEL must be set to 1
	 */
	AUTOCAL_MODE_AUTOSCALE = 1,
	/* Autocal perform auto centering without replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOCENTER = 2,
	/* Autocal perform auto centering and auto replication and the
	 * DSCL_MODE_SEL must be set to 0
	 */
	AUTOCAL_MODE_AUTOREPLICATE = 3
};

enum dscl_mode_sel {
	DSCL_MODE_SCALING_444_BYPASS = 0,
	DSCL_MODE_SCALING_444_RGB_ENABLE = 1,
	DSCL_MODE_SCALING_444_YCBCR_ENABLE = 2,
	DSCL_MODE_SCALING_420_YCBCR_ENABLE = 3,
	DSCL_MODE_SCALING_420_LUMA_BYPASS = 4,
	DSCL_MODE_SCALING_420_CHROMA_BYPASS = 5,
	DSCL_MODE_DSCL_BYPASS = 6
};

enum gamut_remap_select {
	GAMUT_REMAP_BYPASS = 0,
	GAMUT_REMAP_COEFF,
	GAMUT_REMAP_COMA_COEFF,
	GAMUT_REMAP_COMB_COEFF
};

static const struct dcn10_input_csc_matrix dcn10_input_csc_matrix[] = {
	{COLOR_SPACE_SRGB,
		{0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
	{COLOR_SPACE_SRGB_LIMITED,
		{0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
	{COLOR_SPACE_YCBCR601,
		{0x2cdd, 0x2000, 0, 0xe991, 0xe926, 0x2000, 0xf4fd, 0x10ef,
						0, 0x2000, 0x38b4, 0xe3a6} },
	{COLOR_SPACE_YCBCR601_LIMITED,
		{0x3353, 0x2568, 0, 0xe400, 0xe5dc, 0x2568, 0xf367, 0x1108,
						0, 0x2568, 0x40de, 0xdd3a} },
	{COLOR_SPACE_YCBCR709,
		{0x3265, 0x2000, 0, 0xe6ce, 0xf105, 0x2000, 0xfa01, 0xa7d, 0,
						0x2000, 0x3b61, 0xe24f} },

	{COLOR_SPACE_YCBCR709_LIMITED,
		{0x39a6, 0x2568, 0, 0xe0d6, 0xeedd, 0x2568, 0xf925, 0x9a8, 0,
						0x2568, 0x43ee, 0xdbb2} }
};



static void program_gamut_remap(
		struct dcn10_dpp *dpp,
		const uint16_t *regval,
		enum gamut_remap_select select)
{
	uint16_t selection = 0;
	struct color_matrices_reg gam_regs;

	if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
		REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
				CM_GAMUT_REMAP_MODE, 0);
		return;
	}
	switch (select) {
	case GAMUT_REMAP_COEFF:
		selection = 1;
		break;
	case GAMUT_REMAP_COMA_COEFF:
		selection = 2;
		break;
	case GAMUT_REMAP_COMB_COEFF:
		selection = 3;
		break;
	default:
		break;
	}

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_GAMUT_REMAP_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_GAMUT_REMAP_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_GAMUT_REMAP_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_GAMUT_REMAP_C12;


	if (select == GAMUT_REMAP_COEFF) {
		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);

	} else  if (select == GAMUT_REMAP_COMA_COEFF) {

		gam_regs.csc_c11_c12 = REG(CM_COMA_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_COMA_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);

	} else {

		gam_regs.csc_c11_c12 = REG(CM_COMB_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_COMB_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);
	}

	REG_SET(
			CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, selection);

}

void dpp1_cm_set_gamut_remap(
	struct dpp *dpp_base,
	const struct dpp_grph_csc_adjustment *adjust)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(dpp, NULL, GAMUT_REMAP_BYPASS);
	else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		arr_matrix[0] = adjust->temperature_matrix[0];
		arr_matrix[1] = adjust->temperature_matrix[1];
		arr_matrix[2] = adjust->temperature_matrix[2];
		arr_matrix[3] = dal_fixed31_32_zero;

		arr_matrix[4] = adjust->temperature_matrix[3];
		arr_matrix[5] = adjust->temperature_matrix[4];
		arr_matrix[6] = adjust->temperature_matrix[5];
		arr_matrix[7] = dal_fixed31_32_zero;

		arr_matrix[8] = adjust->temperature_matrix[6];
		arr_matrix[9] = adjust->temperature_matrix[7];
		arr_matrix[10] = adjust->temperature_matrix[8];
		arr_matrix[11] = dal_fixed31_32_zero;

		convert_float_matrix(
			arr_reg_val, arr_matrix, 12);

		program_gamut_remap(dpp, arr_reg_val, GAMUT_REMAP_COEFF);
	}
}

void dpp1_cm_set_output_csc_default(
		struct dpp *dpp_base,
		enum dc_color_space colorspace)
{

	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	uint32_t ocsc_mode = 0;

	switch (colorspace) {
		case COLOR_SPACE_SRGB:
		case COLOR_SPACE_2020_RGB_FULLRANGE:
			ocsc_mode = 0;
			break;
		case COLOR_SPACE_SRGB_LIMITED:
		case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
			ocsc_mode = 1;
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			ocsc_mode = 2;
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
		case COLOR_SPACE_2020_YCBCR:
			ocsc_mode = 3;
			break;
		case COLOR_SPACE_UNKNOWN:
		default:
			break;
	}

	REG_SET(CM_OCSC_CONTROL, 0, CM_OCSC_MODE, ocsc_mode);

}

static void dpp1_cm_get_reg_field(
		struct dcn10_dpp *dpp,
		struct xfer_func_reg *reg)
{
	reg->shifts.exp_region0_lut_offset = dpp->tf_shift->CM_RGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = dpp->tf_mask->CM_RGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = dpp->tf_shift->CM_RGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = dpp->tf_mask->CM_RGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = dpp->tf_shift->CM_RGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = dpp->tf_mask->CM_RGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = dpp->tf_shift->CM_RGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = dpp->tf_mask->CM_RGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_END_B;
	reg->masks.field_region_end = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_B;
	reg->masks.field_region_linear_slope = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_B;
	reg->shifts.exp_region_start = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_START_B;
	reg->masks.exp_region_start = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = dpp->tf_shift->CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = dpp->tf_mask->CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_B;
}

static void dpp1_cm_program_color_matrix(
		struct dcn10_dpp *dpp,
		const struct out_csc_color_matrix *tbl_entry)
{
	uint32_t mode;
	struct color_matrices_reg gam_regs;

	REG_GET(CM_OCSC_CONTROL, CM_OCSC_MODE, &mode);

	if (tbl_entry == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_OCSC_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_OCSC_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_OCSC_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_OCSC_C12;

	if (mode == 4) {

		gam_regs.csc_c11_c12 = REG(CM_OCSC_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_OCSC_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				tbl_entry->regval,
				&gam_regs);

	} else {

		gam_regs.csc_c11_c12 = REG(CM_COMB_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_COMB_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				tbl_entry->regval,
				&gam_regs);
	}
}

void dpp1_cm_set_output_csc_adjustment(
		struct dpp *dpp_base,
		const struct out_csc_color_matrix *tbl_entry)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	//enum csc_color_mode config = CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
	uint32_t ocsc_mode = 4;

	/**
	*if (tbl_entry != NULL) {
	*	switch (tbl_entry->color_space) {
	*	case COLOR_SPACE_SRGB:
	*	case COLOR_SPACE_2020_RGB_FULLRANGE:
	*		ocsc_mode = 0;
	*		break;
	*	case COLOR_SPACE_SRGB_LIMITED:
	*	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	*		ocsc_mode = 1;
	*		break;
	*	case COLOR_SPACE_YCBCR601:
	*	case COLOR_SPACE_YCBCR601_LIMITED:
	*		ocsc_mode = 2;
	*		break;
	*	case COLOR_SPACE_YCBCR709:
	*	case COLOR_SPACE_YCBCR709_LIMITED:
	*	case COLOR_SPACE_2020_YCBCR:
	*		ocsc_mode = 3;
	*		break;
	*	case COLOR_SPACE_UNKNOWN:
	*	default:
	*		break;
	*	}
	*}
	*/

	REG_SET(CM_OCSC_CONTROL, 0, CM_OCSC_MODE, ocsc_mode);
	dpp1_cm_program_color_matrix(dpp, tbl_entry);
}

void dpp1_cm_power_on_regamma_lut(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	REG_SET(CM_MEM_PWR_CTRL, 0,
			RGAM_MEM_PWR_FORCE, power_on == true ? 0:1);

}

void dpp1_cm_program_regamma_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	for (i = 0 ; i < num; i++) {
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_red_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_green_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_blue_reg);

	}

}

void dpp1_cm_configure_regamma_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_RGAM_LUT_INDEX, 0, CM_RGAM_LUT_INDEX, 0);
}

/*program re gamma RAM A*/
void dpp1_cm_program_regamma_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dpp1_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_RGAM_RAMA_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_RGAM_RAMA_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_RGAM_RAMA_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_RGAM_RAMA_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_RGAM_RAMA_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_RGAM_RAMA_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_RGAM_RAMA_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_RGAM_RAMA_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_RGAM_RAMA_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_RGAM_RAMA_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_RGAM_RAMA_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_RGAM_RAMA_END_CNTL2_R);
	gam_regs.region_start = REG(CM_RGAM_RAMA_REGION_0_1);
	gam_regs.region_end = REG(CM_RGAM_RAMA_REGION_32_33);

	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);

}

/*program re gamma RAM B*/
void dpp1_cm_program_regamma_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dpp1_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_RGAM_RAMB_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_RGAM_RAMB_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_RGAM_RAMB_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_RGAM_RAMB_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_RGAM_RAMB_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_RGAM_RAMB_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_RGAM_RAMB_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_RGAM_RAMB_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_RGAM_RAMB_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_RGAM_RAMB_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_RGAM_RAMB_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_RGAM_RAMB_END_CNTL2_R);
	gam_regs.region_start = REG(CM_RGAM_RAMB_REGION_0_1);
	gam_regs.region_end = REG(CM_RGAM_RAMB_REGION_32_33);

	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);
}

void dpp1_program_input_csc(
		struct dpp *dpp_base,
		enum dc_color_space color_space,
		enum dcn10_input_csc_select select)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	int i;
	int arr_size = sizeof(dcn10_input_csc_matrix)/sizeof(struct dcn10_input_csc_matrix);
	const uint16_t *regval = NULL;
	uint32_t selection = 1;
	struct color_matrices_reg gam_regs;

	if (select == INPUT_CSC_SELECT_BYPASS) {
		REG_SET(CM_ICSC_CONTROL, 0, CM_ICSC_MODE, 0);
		return;
	}

	for (i = 0; i < arr_size; i++)
		if (dcn10_input_csc_matrix[i].color_space == color_space) {
			regval = dcn10_input_csc_matrix[i].regval;
			break;
		}

	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (select == INPUT_CSC_SELECT_COMA)
		selection = 2;
	REG_SET(CM_ICSC_CONTROL, 0,
			CM_ICSC_MODE, selection);

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_ICSC_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_ICSC_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_ICSC_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_ICSC_C12;


	if (select == INPUT_CSC_SELECT_ICSC) {

		gam_regs.csc_c11_c12 = REG(CM_ICSC_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_ICSC_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);
	} else {

		gam_regs.csc_c11_c12 = REG(CM_COMA_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_COMA_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);
	}
}

/*program de gamma RAM B*/
void dpp1_program_degamma_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dpp1_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_DGAM_RAMB_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_DGAM_RAMB_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_DGAM_RAMB_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_DGAM_RAMB_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_DGAM_RAMB_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_DGAM_RAMB_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_DGAM_RAMB_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_DGAM_RAMB_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_DGAM_RAMB_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_DGAM_RAMB_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_DGAM_RAMB_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_DGAM_RAMB_END_CNTL2_R);
	gam_regs.region_start = REG(CM_DGAM_RAMB_REGION_0_1);
	gam_regs.region_end = REG(CM_DGAM_RAMB_REGION_14_15);


	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);
}

/*program de gamma RAM A*/
void dpp1_program_degamma_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dpp1_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_DGAM_RAMA_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_DGAM_RAMA_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_DGAM_RAMA_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_DGAM_RAMA_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_DGAM_RAMA_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_DGAM_RAMA_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_DGAM_RAMA_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_DGAM_RAMA_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_DGAM_RAMA_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_DGAM_RAMA_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_DGAM_RAMA_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_DGAM_RAMA_END_CNTL2_R);
	gam_regs.region_start = REG(CM_DGAM_RAMA_REGION_0_1);
	gam_regs.region_end = REG(CM_DGAM_RAMA_REGION_14_15);

	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);
}

void dpp1_power_on_degamma_lut(
		struct dpp *dpp_base,
	bool power_on)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_SET(CM_MEM_PWR_CTRL, 0,
			SHARED_MEM_PWR_DIS, power_on == true ? 0:1);

}

static void dpp1_enable_cm_block(
		struct dpp *dpp_base)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_UPDATE(CM_CMOUT_CONTROL, CM_CMOUT_ROUND_TRUNC_MODE, 8);
	REG_UPDATE(CM_CONTROL, CM_BYPASS_EN, 0);
}

void dpp1_set_degamma(
		struct dpp *dpp_base,
		enum ipp_degamma_mode mode)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	dpp1_enable_cm_block(dpp_base);

	switch (mode) {
	case IPP_DEGAMMA_MODE_BYPASS:
		/* Setting de gamma bypass for now */
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 0);
		break;
	case IPP_DEGAMMA_MODE_HW_sRGB:
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 1);
		break;
	case IPP_DEGAMMA_MODE_HW_xvYCC:
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 2);
			break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

void dpp1_degamma_ram_select(
		struct dpp *dpp_base,
							bool use_ram_a)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	if (use_ram_a)
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 3);
	else
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 4);

}

static bool dpp1_degamma_ram_inuse(
		struct dpp *dpp_base,
							bool *ram_a_inuse)
{
	bool ret = false;
	uint32_t status_reg = 0;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_GET(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_DGAM_CONFIG_STATUS,
			&status_reg);

	if (status_reg == 9) {
		*ram_a_inuse = true;
		ret = true;
	} else if (status_reg == 10) {
		*ram_a_inuse = false;
		ret = true;
	}
	return ret;
}

void dpp1_program_degamma_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num,
		bool is_ram_a)
{
	uint32_t i;

	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	REG_UPDATE(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_LUT_HOST_EN, 0);
	REG_UPDATE(CM_DGAM_LUT_WRITE_EN_MASK,
				   CM_DGAM_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_DGAM_LUT_WRITE_EN_MASK, CM_DGAM_LUT_WRITE_SEL,
					is_ram_a == true ? 0:1);

	REG_SET(CM_DGAM_LUT_INDEX, 0, CM_DGAM_LUT_INDEX, 0);
	for (i = 0 ; i < num; i++) {
		REG_SET(CM_DGAM_LUT_DATA, 0, CM_DGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_DGAM_LUT_DATA, 0, CM_DGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(CM_DGAM_LUT_DATA, 0, CM_DGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(CM_DGAM_LUT_DATA, 0,
				CM_DGAM_LUT_DATA, rgb[i].delta_red_reg);
		REG_SET(CM_DGAM_LUT_DATA, 0,
				CM_DGAM_LUT_DATA, rgb[i].delta_green_reg);
		REG_SET(CM_DGAM_LUT_DATA, 0,
				CM_DGAM_LUT_DATA, rgb[i].delta_blue_reg);
	}
}

void dpp1_set_degamma_pwl(struct dpp *dpp_base,
								 const struct pwl_params *params)
{
	bool is_ram_a = true;

	dpp1_power_on_degamma_lut(dpp_base, true);
	dpp1_enable_cm_block(dpp_base);
	dpp1_degamma_ram_inuse(dpp_base, &is_ram_a);
	if (is_ram_a == true)
		dpp1_program_degamma_lutb_settings(dpp_base, params);
	else
		dpp1_program_degamma_luta_settings(dpp_base, params);

	dpp1_program_degamma_lut(dpp_base, params->rgb_resulted,
							params->hw_points_num, !is_ram_a);
	dpp1_degamma_ram_select(dpp_base, !is_ram_a);
}

void dpp1_full_bypass(struct dpp *dpp_base)
{
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	/* Input pixel format: ARGB8888 */
	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, 0x8);

	/* Zero expansion */
	REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			FORMAT_CONTROL__ALPHA_EN, 0,
			FORMAT_EXPANSION_MODE, 0);

	/* COLOR_KEYER_CONTROL.COLOR_KEYER_EN = 0 this should be default */
	if (dpp->tf_mask->CM_BYPASS_EN)
		REG_SET(CM_CONTROL, 0, CM_BYPASS_EN, 1);

	/* Setting degamma bypass for now */
	REG_SET(CM_DGAM_CONTROL, 0, CM_DGAM_LUT_MODE, 0);
}

static bool dpp1_ingamma_ram_inuse(struct dpp *dpp_base,
							bool *ram_a_inuse)
{
	bool in_use = false;
	uint32_t status_reg = 0;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);

	REG_GET(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_DGAM_CONFIG_STATUS,
				&status_reg);

	// 1 => IGAM_RAMA, 3 => IGAM_RAMA & DGAM_ROMA, 4 => IGAM_RAMA & DGAM_ROMB
	if (status_reg == 1 || status_reg == 3 || status_reg == 4) {
		*ram_a_inuse = true;
		in_use = true;
	// 2 => IGAM_RAMB, 5 => IGAM_RAMB & DGAM_ROMA, 6 => IGAM_RAMB & DGAM_ROMB
	} else if (status_reg == 2 || status_reg == 5 || status_reg == 6) {
		*ram_a_inuse = false;
		in_use = true;
	}
	return in_use;
}

/*
 * Input gamma LUT currently supports 256 values only. This means input color
 * can have a maximum of 8 bits per channel (= 256 possible values) in order to
 * have a one-to-one mapping with the LUT. Truncation will occur with color
 * values greater than 8 bits.
 *
 * In the future, this function should support additional input gamma methods,
 * such as piecewise linear mapping, and input gamma bypass.
 */
void dpp1_program_input_lut(
		struct dpp *dpp_base,
		const struct dc_gamma *gamma)
{
	int i;
	struct dcn10_dpp *dpp = TO_DCN10_DPP(dpp_base);
	bool rama_occupied = false;
	uint32_t ram_num;
	// Power on LUT memory.
	REG_SET(CM_MEM_PWR_CTRL, 0, SHARED_MEM_PWR_DIS, 1);
	dpp1_enable_cm_block(dpp_base);
	// Determine whether to use RAM A or RAM B
	dpp1_ingamma_ram_inuse(dpp_base, &rama_occupied);
	if (!rama_occupied)
		REG_UPDATE(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_LUT_SEL, 0);
	else
		REG_UPDATE(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_LUT_SEL, 1);
	// RW mode is 256-entry LUT
	REG_UPDATE(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_LUT_RW_MODE, 0);
	// IGAM Input format should be 8 bits per channel.
	REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 0);
	// Do not mask any R,G,B values
	REG_UPDATE(CM_IGAM_LUT_RW_CONTROL, CM_IGAM_LUT_WRITE_EN_MASK, 7);
	// LUT-256, unsigned, integer, new u0.12 format
	REG_UPDATE_3(
		CM_IGAM_CONTROL,
		CM_IGAM_LUT_FORMAT_R, 3,
		CM_IGAM_LUT_FORMAT_G, 3,
		CM_IGAM_LUT_FORMAT_B, 3);
	// Start at index 0 of IGAM LUT
	REG_UPDATE(CM_IGAM_LUT_RW_INDEX, CM_IGAM_LUT_RW_INDEX, 0);
	for (i = 0; i < gamma->num_entries; i++) {
		REG_SET(CM_IGAM_LUT_SEQ_COLOR, 0, CM_IGAM_LUT_SEQ_COLOR,
				dal_fixed31_32_round(
					gamma->entries.red[i]));
		REG_SET(CM_IGAM_LUT_SEQ_COLOR, 0, CM_IGAM_LUT_SEQ_COLOR,
				dal_fixed31_32_round(
					gamma->entries.green[i]));
		REG_SET(CM_IGAM_LUT_SEQ_COLOR, 0, CM_IGAM_LUT_SEQ_COLOR,
				dal_fixed31_32_round(
					gamma->entries.blue[i]));
	}
	// Power off LUT memory
	REG_SET(CM_MEM_PWR_CTRL, 0, SHARED_MEM_PWR_DIS, 0);
	// Enable IGAM LUT on ram we just wrote to. 2 => RAMA, 3 => RAMB
	REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, rama_occupied ? 3 : 2);
	REG_GET(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, &ram_num);
}
