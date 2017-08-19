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

#include "include/grph_object_id.h"
#include "include/fixed31_32.h"
#include "include/logger_interface.h"

#include "reg_helper.h"
#include "dcn10_dpp.h"
#include "basics/conversion.h"

#define NUM_PHASES    64
#define HORZ_MAX_TAPS 8
#define VERT_MAX_TAPS 8

#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#define REG(reg)\
	xfm->tf_regs->reg

#define CTX \
	xfm->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	xfm->tf_shift->field_name, xfm->tf_mask->field_name

enum dcn10_coef_filter_type_sel {
	SCL_COEF_LUMA_VERT_FILTER = 0,
	SCL_COEF_LUMA_HORZ_FILTER = 1,
	SCL_COEF_CHROMA_VERT_FILTER = 2,
	SCL_COEF_CHROMA_HORZ_FILTER = 3,
	SCL_COEF_ALPHA_VERT_FILTER = 4,
	SCL_COEF_ALPHA_HORZ_FILTER = 5
};

enum lb_memory_config {
	/* Enable all 3 pieces of memory */
	LB_MEMORY_CONFIG_0 = 0,

	/* Enable only the first piece of memory */
	LB_MEMORY_CONFIG_1 = 1,

	/* Enable only the second piece of memory */
	LB_MEMORY_CONFIG_2 = 2,

	/* Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the
	 * last piece of chroma memory used for the luma storage
	 */
	LB_MEMORY_CONFIG_3 = 3
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

static void program_gamut_remap(
		struct dcn10_dpp *xfm,
		const uint16_t *regval,
		enum gamut_remap_select select)
{
	 uint16_t selection = 0;

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


	if (select == GAMUT_REMAP_COEFF) {

		REG_SET_2(CM_GAMUT_REMAP_C11_C12, 0,
				CM_GAMUT_REMAP_C11, regval[0],
				CM_GAMUT_REMAP_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C13_C14, 0,
				CM_GAMUT_REMAP_C13, regval[0],
				CM_GAMUT_REMAP_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C21_C22, 0,
				CM_GAMUT_REMAP_C21, regval[0],
				CM_GAMUT_REMAP_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C23_C24, 0,
				CM_GAMUT_REMAP_C23, regval[0],
				CM_GAMUT_REMAP_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C31_C32, 0,
				CM_GAMUT_REMAP_C31, regval[0],
				CM_GAMUT_REMAP_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_GAMUT_REMAP_C33_C34, 0,
				CM_GAMUT_REMAP_C33, regval[0],
				CM_GAMUT_REMAP_C34, regval[1]);

	} else  if (select == GAMUT_REMAP_COMA_COEFF) {
		REG_SET_2(CM_COMA_C11_C12, 0,
				CM_COMA_C11, regval[0],
				CM_COMA_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C13_C14, 0,
				CM_COMA_C13, regval[0],
				CM_COMA_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C21_C22, 0,
				CM_COMA_C21, regval[0],
				CM_COMA_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C23_C24, 0,
				CM_COMA_C23, regval[0],
				CM_COMA_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C31_C32, 0,
				CM_COMA_C31, regval[0],
				CM_COMA_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C33_C34, 0,
				CM_COMA_C33, regval[0],
				CM_COMA_C34, regval[1]);

	} else {
		REG_SET_2(CM_COMB_C11_C12, 0,
				CM_COMB_C11, regval[0],
				CM_COMB_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C13_C14, 0,
				CM_COMB_C13, regval[0],
				CM_COMB_C14, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C21_C22, 0,
				CM_COMB_C21, regval[0],
				CM_COMB_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C23_C24, 0,
				CM_COMB_C23, regval[0],
				CM_COMB_C24, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C31_C32, 0,
				CM_COMB_C31, regval[0],
				CM_COMB_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMB_C33_C34, 0,
				CM_COMB_C33, regval[0],
				CM_COMB_C34, regval[1]);
	}

	REG_SET(
			CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, selection);

}

void dcn10_dpp_cm_set_gamut_remap(
	struct transform *xfm,
	const struct xfm_grph_csc_adjustment *adjust)
{
	struct dcn10_dpp *dcn_xfm = TO_DCN10_DPP(xfm);

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(dcn_xfm, NULL, GAMUT_REMAP_BYPASS);
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

		program_gamut_remap(dcn_xfm, arr_reg_val, GAMUT_REMAP_COEFF);
	}
}

void dcn10_dpp_cm_set_output_csc_default(
		struct transform *xfm_base,
		const struct default_adjustment *default_adjust)
{

	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
	uint32_t ocsc_mode = 0;

	if (default_adjust != NULL) {
		switch (default_adjust->out_color_space) {
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
	}

	REG_SET(CM_OCSC_CONTROL, 0, CM_OCSC_MODE, ocsc_mode);

}

static void dcn10_dpp_cm_program_color_matrix(
		struct dcn10_dpp *xfm,
		const struct out_csc_color_matrix *tbl_entry)
{
	uint32_t mode;

	REG_GET(CM_OCSC_CONTROL, CM_OCSC_MODE, &mode);

	if (tbl_entry == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (mode == 4) {
		/*R*/
		REG_SET_2(CM_OCSC_C11_C12, 0,
			CM_OCSC_C11, tbl_entry->regval[0],
			CM_OCSC_C12, tbl_entry->regval[1]);

		REG_SET_2(CM_OCSC_C13_C14, 0,
			CM_OCSC_C13, tbl_entry->regval[2],
			CM_OCSC_C14, tbl_entry->regval[3]);

		/*G*/
		REG_SET_2(CM_OCSC_C21_C22, 0,
			CM_OCSC_C21, tbl_entry->regval[4],
			CM_OCSC_C22, tbl_entry->regval[5]);

		REG_SET_2(CM_OCSC_C23_C24, 0,
			CM_OCSC_C23, tbl_entry->regval[6],
			CM_OCSC_C24, tbl_entry->regval[7]);

		/*B*/
		REG_SET_2(CM_OCSC_C31_C32, 0,
			CM_OCSC_C31, tbl_entry->regval[8],
			CM_OCSC_C32, tbl_entry->regval[9]);

		REG_SET_2(CM_OCSC_C33_C34, 0,
			CM_OCSC_C33, tbl_entry->regval[10],
			CM_OCSC_C34, tbl_entry->regval[11]);
	} else {
		/*R*/
		REG_SET_2(CM_COMB_C11_C12, 0,
			CM_COMB_C11, tbl_entry->regval[0],
			CM_COMB_C12, tbl_entry->regval[1]);

		REG_SET_2(CM_COMB_C13_C14, 0,
			CM_COMB_C13, tbl_entry->regval[2],
			CM_COMB_C14, tbl_entry->regval[3]);

		/*G*/
		REG_SET_2(CM_COMB_C21_C22, 0,
			CM_COMB_C21, tbl_entry->regval[4],
			CM_COMB_C22, tbl_entry->regval[5]);

		REG_SET_2(CM_COMB_C23_C24, 0,
			CM_COMB_C23, tbl_entry->regval[6],
			CM_COMB_C24, tbl_entry->regval[7]);

		/*B*/
		REG_SET_2(CM_COMB_C31_C32, 0,
			CM_COMB_C31, tbl_entry->regval[8],
			CM_COMB_C32, tbl_entry->regval[9]);

		REG_SET_2(CM_COMB_C33_C34, 0,
			CM_COMB_C33, tbl_entry->regval[10],
			CM_COMB_C34, tbl_entry->regval[11]);
	}
}

void dcn10_dpp_cm_set_output_csc_adjustment(
		struct transform *xfm_base,
		const struct out_csc_color_matrix *tbl_entry)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
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
	dcn10_dpp_cm_program_color_matrix(xfm, tbl_entry);
}

void dcn10_dpp_cm_power_on_regamma_lut(
	struct transform *xfm_base,
	bool power_on)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
	REG_SET(CM_MEM_PWR_CTRL, 0,
			RGAM_MEM_PWR_FORCE, power_on == true ? 0:1);

}

void dcn10_dpp_cm_program_regamma_lut(
		struct transform *xfm_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);
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

void dcn10_dpp_cm_configure_regamma_lut(
		struct transform *xfm_base,
		bool is_ram_a)
{
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_RGAM_LUT_INDEX, 0, CM_RGAM_LUT_INDEX, 0);
}

/*program re gamma RAM A*/
void dcn10_dpp_cm_program_regamma_luta_settings(
		struct transform *xfm_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	REG_SET_2(CM_RGAM_RAMA_START_CNTL_B, 0,
		CM_RGAM_RAMA_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_RGAM_RAMA_START_CNTL_G, 0,
		CM_RGAM_RAMA_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_RGAM_RAMA_START_CNTL_R, 0,
		CM_RGAM_RAMA_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_B, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_G, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_R, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_B, 0,
		CM_RGAM_RAMA_EXP_REGION_END_B, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_B, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_B, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_G, 0,
		CM_RGAM_RAMA_EXP_REGION_END_G, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_G, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_G, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_R, 0,
		CM_RGAM_RAMA_EXP_REGION_END_R, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_R, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_R, params->arr_points[1].custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_RGAM_RAMA_REGION_0_1, 0,
		CM_RGAM_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_2_3, 0,
		CM_RGAM_RAMA_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_4_5, 0,
		CM_RGAM_RAMA_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_6_7, 0,
		CM_RGAM_RAMA_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_8_9, 0,
		CM_RGAM_RAMA_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_10_11, 0,
		CM_RGAM_RAMA_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_12_13, 0,
		CM_RGAM_RAMA_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_14_15, 0,
		CM_RGAM_RAMA_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_16_17, 0,
		CM_RGAM_RAMA_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_18_19, 0,
		CM_RGAM_RAMA_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_20_21, 0,
		CM_RGAM_RAMA_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_22_23, 0,
		CM_RGAM_RAMA_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_24_25, 0,
		CM_RGAM_RAMA_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_26_27, 0,
		CM_RGAM_RAMA_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_28_29, 0,
		CM_RGAM_RAMA_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_30_31, 0,
		CM_RGAM_RAMA_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_32_33, 0,
		CM_RGAM_RAMA_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);
}

/*program re gamma RAM B*/
void dcn10_dpp_cm_program_regamma_lutb_settings(
		struct transform *xfm_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_dpp *xfm = TO_DCN10_DPP(xfm_base);

	REG_SET_2(CM_RGAM_RAMB_START_CNTL_B, 0,
		CM_RGAM_RAMB_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_RGAM_RAMB_START_CNTL_G, 0,
		CM_RGAM_RAMB_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_RGAM_RAMB_START_CNTL_R, 0,
		CM_RGAM_RAMB_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_B, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_G, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_R, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_B, 0,
		CM_RGAM_RAMB_EXP_REGION_END_B, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_B, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_B, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_G, 0,
		CM_RGAM_RAMB_EXP_REGION_END_G, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_G, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_G, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_R, 0,
		CM_RGAM_RAMB_EXP_REGION_END_R, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_R, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_R, params->arr_points[1].custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_RGAM_RAMB_REGION_0_1, 0,
		CM_RGAM_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_2_3, 0,
		CM_RGAM_RAMB_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_4_5, 0,
		CM_RGAM_RAMB_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_6_7, 0,
		CM_RGAM_RAMB_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_8_9, 0,
		CM_RGAM_RAMB_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_10_11, 0,
		CM_RGAM_RAMB_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_12_13, 0,
		CM_RGAM_RAMB_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_14_15, 0,
		CM_RGAM_RAMB_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_16_17, 0,
		CM_RGAM_RAMB_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_18_19, 0,
		CM_RGAM_RAMB_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_20_21, 0,
		CM_RGAM_RAMB_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_22_23, 0,
		CM_RGAM_RAMB_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_24_25, 0,
		CM_RGAM_RAMB_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_26_27, 0,
		CM_RGAM_RAMB_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_28_29, 0,
		CM_RGAM_RAMB_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_30_31, 0,
		CM_RGAM_RAMB_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_32_33, 0,
		CM_RGAM_RAMB_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);

}
