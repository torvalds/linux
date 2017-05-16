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

#include "dm_services.h"
#include "dcn10_ipp.h"
#include "reg_helper.h"

#define REG(reg) \
	(ippn10->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	ippn10->ipp_shift->field_name, ippn10->ipp_mask->field_name

#define CTX \
	ippn10->base.ctx


struct dcn10_input_csc_matrix {
	enum dc_color_space color_space;
	uint32_t regval[12];
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

enum dcn10_input_csc_select {
	INPUT_CSC_SELECT_BYPASS = 0,
	INPUT_CSC_SELECT_ICSC,
	INPUT_CSC_SELECT_COMA
};

static void dcn10_program_input_csc(
		struct input_pixel_processor *ipp,
		enum dc_color_space color_space,
		enum dcn10_input_csc_select select)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	int i;
	int arr_size = sizeof(dcn10_input_csc_matrix)/sizeof(struct dcn10_input_csc_matrix);
	const uint32_t *regval = NULL;
	uint32_t selection = 1;

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

	if (select == INPUT_CSC_SELECT_ICSC) {
		/*R*/
		REG_SET_2(CM_ICSC_C11_C12, 0,
			CM_ICSC_C11, regval[0],
			CM_ICSC_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_ICSC_C13_C14, 0,
			CM_ICSC_C13, regval[0],
			CM_ICSC_C14, regval[1]);
		/*G*/
		regval += 2;
		REG_SET_2(CM_ICSC_C21_C22, 0,
			CM_ICSC_C21, regval[0],
			CM_ICSC_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_ICSC_C23_C24, 0,
			CM_ICSC_C23, regval[0],
			CM_ICSC_C24, regval[1]);
		/*B*/
		regval += 2;
		REG_SET_2(CM_ICSC_C31_C32, 0,
			CM_ICSC_C31, regval[0],
			CM_ICSC_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_ICSC_C33_C34, 0,
			CM_ICSC_C33, regval[0],
			CM_ICSC_C34, regval[1]);
	} else {
		/*R*/
		REG_SET_2(CM_COMA_C11_C12, 0,
			CM_COMA_C11, regval[0],
			CM_COMA_C12, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C13_C14, 0,
			CM_COMA_C13, regval[0],
			CM_COMA_C14, regval[1]);
		/*G*/
		regval += 2;
		REG_SET_2(CM_COMA_C21_C22, 0,
			CM_COMA_C21, regval[0],
			CM_COMA_C22, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C23_C24, 0,
			CM_COMA_C23, regval[0],
			CM_COMA_C24, regval[1]);
		/*B*/
		regval += 2;
		REG_SET_2(CM_COMA_C31_C32, 0,
			CM_COMA_C31, regval[0],
			CM_COMA_C32, regval[1]);
		regval += 2;
		REG_SET_2(CM_COMA_C33_C34, 0,
			CM_COMA_C33, regval[0],
			CM_COMA_C34, regval[1]);
	}
}

/*program de gamma RAM B*/
static void dcn10_ipp_program_degamma_lutb_settings(
		struct input_pixel_processor *ipp,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	REG_SET_2(CM_DGAM_RAMB_START_CNTL_B, 0,
		CM_DGAM_RAMB_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMB_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(CM_DGAM_RAMB_START_CNTL_G, 0,
		CM_DGAM_RAMB_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMB_EXP_REGION_START_SEGMENT_G, 0);

	REG_SET_2(CM_DGAM_RAMB_START_CNTL_R, 0,
		CM_DGAM_RAMB_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMB_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_DGAM_RAMB_SLOPE_CNTL_B, 0,
		CM_DGAM_RAMB_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMB_SLOPE_CNTL_G, 0,
		CM_DGAM_RAMB_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMB_SLOPE_CNTL_R, 0,
		CM_DGAM_RAMB_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMB_END_CNTL1_B, 0,
		CM_DGAM_RAMB_EXP_REGION_END_B, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMB_END_CNTL2_B, 0,
		CM_DGAM_RAMB_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMB_EXP_REGION_END_BASE_B, params->arr_points[2].custom_float_slope);

	REG_SET(CM_DGAM_RAMB_END_CNTL1_G, 0,
		CM_DGAM_RAMB_EXP_REGION_END_G, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMB_END_CNTL2_G, 0,
		CM_DGAM_RAMB_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMB_EXP_REGION_END_BASE_G, params->arr_points[2].custom_float_slope);

	REG_SET(CM_DGAM_RAMB_END_CNTL1_R, 0,
		CM_DGAM_RAMB_EXP_REGION_END_R, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMB_END_CNTL2_R, 0,
		CM_DGAM_RAMB_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMB_EXP_REGION_END_BASE_R, params->arr_points[2].custom_float_slope);

	curve = params->arr_curve_points;
	REG_SET_4(CM_DGAM_RAMB_REGION_0_1, 0,
		CM_DGAM_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION1_LUT_OFFSET, 	curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_2_3, 0,
		CM_DGAM_RAMB_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_4_5, 0,
		CM_DGAM_RAMB_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_6_7, 0,
		CM_DGAM_RAMB_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_8_9, 0,
		CM_DGAM_RAMB_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_10_11, 0,
		CM_DGAM_RAMB_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_12_13, 0,
		CM_DGAM_RAMB_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMB_REGION_14_15, 0,
		CM_DGAM_RAMB_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMB_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMB_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMB_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);
}

/*program de gamma RAM A*/
static void dcn10_ipp_program_degamma_luta_settings(
		struct input_pixel_processor *ipp,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	REG_SET_2(CM_DGAM_RAMA_START_CNTL_B, 0,
		CM_DGAM_RAMA_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(CM_DGAM_RAMA_START_CNTL_G, 0,
		CM_DGAM_RAMA_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMA_EXP_REGION_START_SEGMENT_G, 0);

	REG_SET_2(CM_DGAM_RAMA_START_CNTL_R, 0,
		CM_DGAM_RAMA_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_DGAM_RAMA_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_DGAM_RAMA_SLOPE_CNTL_B, 0,
		CM_DGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMA_SLOPE_CNTL_G, 0,
		CM_DGAM_RAMA_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMA_SLOPE_CNTL_R, 0,
		CM_DGAM_RAMA_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_DGAM_RAMA_END_CNTL1_B, 0,
		CM_DGAM_RAMA_EXP_REGION_END_B, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMA_END_CNTL2_B, 0,
		CM_DGAM_RAMA_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMA_EXP_REGION_END_BASE_B, params->arr_points[2].custom_float_slope);

	REG_SET(CM_DGAM_RAMA_END_CNTL1_G, 0,
		CM_DGAM_RAMA_EXP_REGION_END_G, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMA_END_CNTL2_G, 0,
		CM_DGAM_RAMA_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMA_EXP_REGION_END_BASE_G, params->arr_points[2].custom_float_slope);

	REG_SET(CM_DGAM_RAMA_END_CNTL1_R, 0,
		CM_DGAM_RAMA_EXP_REGION_END_R, params->arr_points[1].custom_float_x);

	REG_SET_2(CM_DGAM_RAMA_END_CNTL2_R, 0,
		CM_DGAM_RAMA_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_y,
		CM_DGAM_RAMA_EXP_REGION_END_BASE_R, params->arr_points[2].custom_float_slope);

	curve = params->arr_curve_points;
	REG_SET_4(CM_DGAM_RAMA_REGION_0_1, 0,
		CM_DGAM_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_2_3, 0,
		CM_DGAM_RAMA_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_4_5, 0,
		CM_DGAM_RAMA_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_6_7, 0,
		CM_DGAM_RAMA_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_8_9, 0,
		CM_DGAM_RAMA_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_10_11, 0,
		CM_DGAM_RAMA_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_12_13, 0,
		CM_DGAM_RAMA_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_DGAM_RAMA_REGION_14_15, 0,
		CM_DGAM_RAMA_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_DGAM_RAMA_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_DGAM_RAMA_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_DGAM_RAMA_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);
}

static void ipp_power_on_degamma_lut(
	struct input_pixel_processor *ipp,
	bool power_on)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	REG_SET(CM_MEM_PWR_CTRL, 0,
			SHARED_MEM_PWR_DIS, power_on == true ? 0:1);

}

static void ipp_program_degamma_lut(
		struct input_pixel_processor *ipp,
		const struct pwl_result_data *rgb,
		uint32_t num,
		bool is_ram_a)
{
	uint32_t i;

	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
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

static void dcn10_ipp_enable_cm_block(
		struct input_pixel_processor *ipp)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	REG_UPDATE(DPP_CONTROL, DPP_CLOCK_ENABLE, 1);
	REG_UPDATE(CM_CONTROL, CM_BYPASS_EN, 0);
}


static void dcn10_ipp_full_bypass(struct input_pixel_processor *ipp)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	/* Input pixel format: ARGB8888 */
	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, 0x8);

	/* Zero expansion */
	REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			ALPHA_EN, 0,
			FORMAT_EXPANSION_MODE, 0);

	/* COLOR_KEYER_CONTROL.COLOR_KEYER_EN = 0 this should be default */
	REG_SET(CM_CONTROL, 0, CM_BYPASS_EN, 1);

	/* Setting degamma bypass for now */
	REG_SET(CM_DGAM_CONTROL, 0, CM_DGAM_LUT_MODE, 0);
	REG_SET(CM_IGAM_CONTROL, 0, CM_IGAM_LUT_MODE, 0);
}

static void dcn10_ipp_set_degamma(
		struct input_pixel_processor *ipp,
		enum ipp_degamma_mode mode)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	dcn10_ipp_enable_cm_block(ipp);

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

static bool dcn10_cursor_program_control(
		struct dcn10_ipp *ippn10,
		bool pixel_data_invert,
		enum dc_cursor_color_format color_format)
{
	REG_SET_2(CURSOR_SETTINS, 0,
			/* no shift of the cursor HDL schedule */
			CURSOR0_DST_Y_OFFSET, 0,
			 /* used to shift the cursor chunk request deadline */
			CURSOR0_CHUNK_HDL_ADJUST, 3);

	REG_UPDATE_2(CURSOR0_CONTROL,
			CUR0_MODE, color_format,
			CUR0_INVERT_MODE, 0);

	if (color_format == CURSOR_MODE_MONO) {
		/* todo: clarify what to program these to */
		REG_UPDATE(CURSOR0_COLOR0,
				CUR0_COLOR0, 0x00000000);
		REG_UPDATE(CURSOR0_COLOR1,
				CUR0_COLOR1, 0xFFFFFFFF);
	}

	/* TODO: Fixed vs float */

	REG_UPDATE_3(FORMAT_CONTROL,
				CNVC_BYPASS, 0,
				ALPHA_EN, 1,
				FORMAT_EXPANSION_MODE, 0);

	REG_UPDATE(CURSOR0_CONTROL,
			CUR0_EXPANSION_MODE, 0);

	if (0 /*attributes->attribute_flags.bits.MIN_MAX_INVERT*/) {
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_MAX,
				0 /* TODO */);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_MIN,
				0 /* TODO */);
	}

	return true;
}

enum cursor_pitch {
	CURSOR_PITCH_64_PIXELS = 0,
	CURSOR_PITCH_128_PIXELS,
	CURSOR_PITCH_256_PIXELS
};

enum cursor_lines_per_chunk {
	CURSOR_LINE_PER_CHUNK_2 = 1,
	CURSOR_LINE_PER_CHUNK_4,
	CURSOR_LINE_PER_CHUNK_8,
	CURSOR_LINE_PER_CHUNK_16
};

static enum cursor_pitch dcn10_get_cursor_pitch(
		unsigned int pitch)
{
	enum cursor_pitch hw_pitch;

	switch (pitch) {
	case 64:
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	case 128:
		hw_pitch = CURSOR_PITCH_128_PIXELS;
		break;
	case 256:
		hw_pitch = CURSOR_PITCH_256_PIXELS;
		break;
	default:
		DC_ERR("Invalid cursor pitch of %d. "
				"Only 64/128/256 is supported on DCN.\n", pitch);
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	}
	return hw_pitch;
}

static enum cursor_lines_per_chunk dcn10_get_lines_per_chunk(
		unsigned int cur_width,
		enum dc_cursor_color_format format)
{
	enum cursor_lines_per_chunk line_per_chunk;

	if (format == CURSOR_MODE_MONO)
		/* impl B. expansion in CUR Buffer reader */
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 32)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 64)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_8;
	else if (cur_width <= 128)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_4;
	else
		line_per_chunk = CURSOR_LINE_PER_CHUNK_2;

	return line_per_chunk;
}

static void dcn10_cursor_set_attributes(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_attributes *attr)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	enum cursor_pitch hw_pitch = dcn10_get_cursor_pitch(attr->pitch);
	enum cursor_lines_per_chunk lpc = dcn10_get_lines_per_chunk(
			attr->width, attr->color_format);

	ippn10->curs_attr = *attr;

	REG_UPDATE(CURSOR_SURFACE_ADDRESS_HIGH,
			CURSOR_SURFACE_ADDRESS_HIGH, attr->address.high_part);
	REG_UPDATE(CURSOR_SURFACE_ADDRESS,
			CURSOR_SURFACE_ADDRESS, attr->address.low_part);

	REG_UPDATE_2(CURSOR_SIZE,
			CURSOR_WIDTH, attr->width,
			CURSOR_HEIGHT, attr->height);

	REG_UPDATE_3(CURSOR_CONTROL,
			CURSOR_MODE, attr->color_format,
			CURSOR_PITCH, hw_pitch,
			CURSOR_LINES_PER_CHUNK, lpc);

	dcn10_cursor_program_control(ippn10,
			attr->attribute_flags.bits.INVERT_PIXEL_DATA,
			attr->color_format);
}

static void dcn10_cursor_set_position(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_position *pos,
		const struct dc_cursor_mi_param *param)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	int src_x_offset = pos->x - pos->x_hotspot - param->viewport_x_start;
	uint32_t cur_en = pos->enable ? 1 : 0;
	uint32_t dst_x_offset = (src_x_offset >= 0) ? src_x_offset : 0;

	/*
	 * Guard aganst cursor_set_position() from being called with invalid
	 * attributes
	 *
	 * TODO: Look at combining cursor_set_position() and
	 * cursor_set_attributes() into cursor_update()
	 */
	if (ippn10->curs_attr.address.quad_part == 0)
		return;

	dst_x_offset *= param->ref_clk_khz;
	dst_x_offset /= param->pixel_clk_khz;

	ASSERT(param->h_scale_ratio.value);

	if (param->h_scale_ratio.value)
		dst_x_offset = dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(dst_x_offset),
				param->h_scale_ratio));

	if (src_x_offset >= (int)param->viewport_width)
		cur_en = 0;  /* not visible beyond right edge*/

	if (src_x_offset + (int)ippn10->curs_attr.width < 0)
		cur_en = 0;  /* not visible beyond left edge*/

	if (cur_en && REG_READ(CURSOR_SURFACE_ADDRESS) == 0)
		dcn10_cursor_set_attributes(ipp, &ippn10->curs_attr);
	REG_UPDATE(CURSOR_CONTROL,
			CURSOR_ENABLE, cur_en);
	REG_UPDATE(CURSOR0_CONTROL,
			CUR0_ENABLE, cur_en);

	REG_SET_2(CURSOR_POSITION, 0,
			CURSOR_X_POSITION, pos->x,
			CURSOR_Y_POSITION, pos->y);

	REG_SET_2(CURSOR_HOT_SPOT, 0,
			CURSOR_HOT_SPOT_X, pos->x_hotspot,
			CURSOR_HOT_SPOT_Y, pos->y_hotspot);

	REG_SET(CURSOR_DST_OFFSET, 0,
			CURSOR_DST_X_OFFSET, dst_x_offset);
	/* TODO Handle surface pixel formats other than 4:4:4 */
}

enum pixel_format_description {
	PIXEL_FORMAT_FIXED = 0,
	PIXEL_FORMAT_FIXED16,
	PIXEL_FORMAT_FLOAT

};

static void dcn10_setup_format_flags(enum surface_pixel_format input_format,\
						enum pixel_format_description *fmt)
{

	if (input_format == SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F ||
		input_format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F)
		*fmt = PIXEL_FORMAT_FLOAT;
	else if (input_format == SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616)
		*fmt = PIXEL_FORMAT_FIXED16;
	else
		*fmt = PIXEL_FORMAT_FIXED;
}

static void dcn10_ipp_set_degamma_format_float(struct input_pixel_processor *ipp,
		bool is_float)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	if (is_float) {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 3);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 1);
	} else {
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_INPUT_FORMAT, 2);
		REG_UPDATE(CM_IGAM_CONTROL, CM_IGAM_LUT_MODE, 0);
	}
}


static void dcn10_ipp_cnv_setup (
		struct input_pixel_processor *ipp,
		enum surface_pixel_format input_format,
		enum expansion_mode mode,
		enum ipp_output_format cnv_out_format)
{
	uint32_t pixel_format;
	uint32_t alpha_en;
	enum pixel_format_description fmt ;
	enum dc_color_space color_space;
	enum dcn10_input_csc_select select;
	bool is_float;
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	bool force_disable_cursor = false;

	dcn10_setup_format_flags(input_format, &fmt);
	alpha_en = 1;
	pixel_format = 0;
	color_space = COLOR_SPACE_SRGB;
	select = INPUT_CSC_SELECT_BYPASS;
	is_float = false;

	switch (fmt) {
	case PIXEL_FORMAT_FIXED:
	case PIXEL_FORMAT_FIXED16:
	/*when output is float then FORMAT_CONTROL__OUTPUT_FP=1*/
		REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			FORMAT_EXPANSION_MODE, mode,
			OUTPUT_FP, 0);
		break;
	case PIXEL_FORMAT_FLOAT:
		REG_SET_3(FORMAT_CONTROL, 0,
			CNVC_BYPASS, 0,
			FORMAT_EXPANSION_MODE, mode,
			OUTPUT_FP, 1);
		is_float = true;
		break;
	default:

		break;
	}

	dcn10_ipp_set_degamma_format_float(ipp, is_float);

	switch (input_format) {
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
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		force_disable_cursor = false;
		pixel_format = 65;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		force_disable_cursor = true;
		pixel_format = 64;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		force_disable_cursor = true;
		pixel_format = 67;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		force_disable_cursor = true;
		pixel_format = 66;
		color_space = COLOR_SPACE_YCBCR709;
		select = INPUT_CSC_SELECT_ICSC;
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
	default:
		break;
	}
	REG_SET(CNVC_SURFACE_PIXEL_FORMAT, 0,
			CNVC_SURFACE_PIXEL_FORMAT, pixel_format);
	REG_UPDATE(FORMAT_CONTROL, ALPHA_EN, alpha_en);

	dcn10_program_input_csc(ipp, color_space, select);

	if (force_disable_cursor) {
		REG_UPDATE(CURSOR_CONTROL,
				CURSOR_ENABLE, 0);
		REG_UPDATE(CURSOR0_CONTROL,
				CUR0_ENABLE, 0);
	}
}


static bool dcn10_degamma_ram_inuse(struct input_pixel_processor *ipp,
							bool *ram_a_inuse)
{
	bool ret = false;
	uint32_t status_reg = 0;
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	status_reg = (REG_READ(CM_IGAM_LUT_RW_CONTROL) & 0x0F00) >>16;
	if (status_reg == 9) {
		*ram_a_inuse = true;
		ret = true;
	} else if (status_reg == 10) {
		*ram_a_inuse = false;
		ret = true;
	}
	return ret;
}

static void dcn10_degamma_ram_select(struct input_pixel_processor *ipp,
							bool use_ram_a)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);

	if (use_ram_a)
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 3);
	else
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 4);

}

static void dcn10_ipp_set_degamma_pwl(struct input_pixel_processor *ipp,
								 const struct pwl_params *params)
{
	bool is_ram_a = true;

	ipp_power_on_degamma_lut(ipp, true);
	dcn10_ipp_enable_cm_block(ipp);
	dcn10_degamma_ram_inuse(ipp, &is_ram_a);
	if (is_ram_a == true)
		dcn10_ipp_program_degamma_lutb_settings(ipp, params);
	else
		dcn10_ipp_program_degamma_luta_settings(ipp, params);

	ipp_program_degamma_lut(ipp, params->rgb_resulted,
							params->hw_points_num, !is_ram_a);
	dcn10_degamma_ram_select(ipp, !is_ram_a);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static void dcn10_ipp_destroy(struct input_pixel_processor **ipp)
{
	dm_free(TO_DCN10_IPP(*ipp));
	*ipp = NULL;
}

static const struct ipp_funcs dcn10_ipp_funcs = {
	.ipp_cursor_set_attributes	= dcn10_cursor_set_attributes,
	.ipp_cursor_set_position	= dcn10_cursor_set_position,
	.ipp_set_degamma		= dcn10_ipp_set_degamma,
	.ipp_full_bypass		= dcn10_ipp_full_bypass,
	.ipp_setup			= dcn10_ipp_cnv_setup,
	.ipp_program_degamma_pwl	= dcn10_ipp_set_degamma_pwl,
	.ipp_destroy			= dcn10_ipp_destroy
};

void dcn10_ipp_construct(
	struct dcn10_ipp *ippn10,
	struct dc_context *ctx,
	int inst,
	const struct dcn10_ipp_registers *regs,
	const struct dcn10_ipp_shift *ipp_shift,
	const struct dcn10_ipp_mask *ipp_mask)
{
	ippn10->base.ctx = ctx;
	ippn10->base.inst = inst;
	ippn10->base.funcs = &dcn10_ipp_funcs;

	ippn10->regs = regs;
	ippn10->ipp_shift = ipp_shift;
	ippn10->ipp_mask = ipp_mask;
}
