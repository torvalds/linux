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

#include "dcn10/dcn10_cm_common.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define IND_REG(index) \
	(index)

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name


static void dpp2_enable_cm_block(
		struct dpp *dpp_base)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	unsigned int cm_bypass_mode = 0;
	//Temp, put CM in bypass mode
	if (dpp_base->ctx->dc->debug.cm_in_bypass)
		cm_bypass_mode = 1;

	REG_UPDATE(CM_CONTROL, CM_BYPASS, cm_bypass_mode);
}


static bool dpp2_degamma_ram_inuse(
		struct dpp *dpp_base,
		bool *ram_a_inuse)
{
	bool ret = false;
	uint32_t status_reg = 0;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET(CM_DGAM_LUT_WRITE_EN_MASK, CM_DGAM_CONFIG_STATUS,
			&status_reg);

	if (status_reg == 3) {
		*ram_a_inuse = true;
		ret = true;
	} else if (status_reg == 4) {
		*ram_a_inuse = false;
		ret = true;
	}
	return ret;
}

static void dpp2_program_degamma_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num,
		bool is_ram_a)
{
	uint32_t i;

	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
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

void dpp2_set_degamma_pwl(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	bool is_ram_a = true;

	dpp1_power_on_degamma_lut(dpp_base, true);
	dpp2_enable_cm_block(dpp_base);
	dpp2_degamma_ram_inuse(dpp_base, &is_ram_a);
	if (is_ram_a == true)
		dpp1_program_degamma_lutb_settings(dpp_base, params);
	else
		dpp1_program_degamma_luta_settings(dpp_base, params);

	dpp2_program_degamma_lut(dpp_base, params->rgb_resulted, params->hw_points_num, !is_ram_a);
	dpp1_degamma_ram_select(dpp_base, !is_ram_a);
}

void dpp2_set_degamma(
		struct dpp *dpp_base,
		enum ipp_degamma_mode mode)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	dpp2_enable_cm_block(dpp_base);

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
	case IPP_DEGAMMA_MODE_USER_PWL:
		REG_UPDATE(CM_DGAM_CONTROL, CM_DGAM_LUT_MODE, 3);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void program_gamut_remap(
		struct dcn20_dpp *dpp,
		const uint16_t *regval,
		enum dcn20_gamut_remap_select select)
{
	uint32_t cur_select = 0;
	struct color_matrices_reg gam_regs;

	if (regval == NULL || select == DCN2_GAMUT_REMAP_BYPASS) {
		REG_SET(CM_GAMUT_REMAP_CONTROL, 0,
				CM_GAMUT_REMAP_MODE, 0);
		return;
	}

	/* determine which gamut_remap coefficients (A or B) we are using
	 * currently. select the alternate set to double buffer
	 * the update so gamut_remap is updated on frame boundary
	 */
	IX_REG_GET(CM_TEST_DEBUG_INDEX, CM_TEST_DEBUG_DATA,
					CM_TEST_DEBUG_DATA_STATUS_IDX,
					CM_TEST_DEBUG_DATA_GAMUT_REMAP_MODE, &cur_select);

	/* value stored in dbg reg will be 1 greater than mode we want */
	if (cur_select != DCN2_GAMUT_REMAP_COEF_A)
		select = DCN2_GAMUT_REMAP_COEF_A;
	else
		select = DCN2_GAMUT_REMAP_COEF_B;

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_GAMUT_REMAP_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_GAMUT_REMAP_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_GAMUT_REMAP_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_GAMUT_REMAP_C12;

	if (select == DCN2_GAMUT_REMAP_COEF_A) {
		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_C33_C34);
	} else {
		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_B_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_B_C33_C34);
	}

	cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);

	REG_SET(
			CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, select);

}

void dpp2_cm_set_gamut_remap(
	struct dpp *dpp_base,
	const struct dpp_grph_csc_adjustment *adjust)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	int i = 0;

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(dpp, NULL, DCN2_GAMUT_REMAP_BYPASS);
	else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		for (i = 0; i < 12; i++)
			arr_matrix[i] = adjust->temperature_matrix[i];

		convert_float_matrix(
			arr_reg_val, arr_matrix, 12);

		program_gamut_remap(dpp, arr_reg_val, DCN2_GAMUT_REMAP_COEF_A);
	}
}

void dpp2_program_input_csc(
		struct dpp *dpp_base,
		enum dc_color_space color_space,
		enum dcn20_input_csc_select input_select,
		const struct out_csc_color_matrix *tbl_entry)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	int i;
	int arr_size = sizeof(dpp_input_csc_matrix)/sizeof(struct dpp_input_csc_matrix);
	const uint16_t *regval = NULL;
	uint32_t cur_select = 0;
	enum dcn20_input_csc_select select;
	struct color_matrices_reg icsc_regs;

	if (input_select == DCN2_ICSC_SELECT_BYPASS) {
		REG_SET(CM_ICSC_CONTROL, 0, CM_ICSC_MODE, 0);
		return;
	}

	if (tbl_entry == NULL) {
		for (i = 0; i < arr_size; i++)
			if (dpp_input_csc_matrix[i].color_space == color_space) {
				regval = dpp_input_csc_matrix[i].regval;
				break;
			}

		if (regval == NULL) {
			BREAK_TO_DEBUGGER();
			return;
		}
	} else {
		regval = tbl_entry->regval;
	}

	/* determine which CSC coefficients (A or B) we are using
	 * currently.  select the alternate set to double buffer
	 * the CSC update so CSC is updated on frame boundary
	 */
	IX_REG_GET(CM_TEST_DEBUG_INDEX, CM_TEST_DEBUG_DATA,
					CM_TEST_DEBUG_DATA_STATUS_IDX,
					CM_TEST_DEBUG_DATA_ICSC_MODE, &cur_select);

	if (cur_select != DCN2_ICSC_SELECT_ICSC_A)
		select = DCN2_ICSC_SELECT_ICSC_A;
	else
		select = DCN2_ICSC_SELECT_ICSC_B;

	icsc_regs.shifts.csc_c11 = dpp->tf_shift->CM_ICSC_C11;
	icsc_regs.masks.csc_c11  = dpp->tf_mask->CM_ICSC_C11;
	icsc_regs.shifts.csc_c12 = dpp->tf_shift->CM_ICSC_C12;
	icsc_regs.masks.csc_c12 = dpp->tf_mask->CM_ICSC_C12;

	if (select == DCN2_ICSC_SELECT_ICSC_A) {

		icsc_regs.csc_c11_c12 = REG(CM_ICSC_C11_C12);
		icsc_regs.csc_c33_c34 = REG(CM_ICSC_C33_C34);

	} else {

		icsc_regs.csc_c11_c12 = REG(CM_ICSC_B_C11_C12);
		icsc_regs.csc_c33_c34 = REG(CM_ICSC_B_C33_C34);

	}

	cm_helper_program_color_matrices(
			dpp->base.ctx,
			regval,
			&icsc_regs);

	REG_SET(CM_ICSC_CONTROL, 0,
				CM_ICSC_MODE, select);
}

static void dpp20_power_on_blnd_lut(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_SET(CM_MEM_PWR_CTRL, 0,
			BLNDGAM_MEM_PWR_FORCE, power_on == true ? 0:1);

}

static void dpp20_configure_blnd_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(CM_BLNDGAM_LUT_WRITE_EN_MASK,
			CM_BLNDGAM_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_BLNDGAM_LUT_WRITE_EN_MASK,
			CM_BLNDGAM_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_BLNDGAM_LUT_INDEX, 0, CM_BLNDGAM_LUT_INDEX, 0);
}

static void dpp20_program_blnd_pwl(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	for (i = 0 ; i < num; i++) {
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0, CM_BLNDGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(CM_BLNDGAM_LUT_DATA, 0,
				CM_BLNDGAM_LUT_DATA, rgb[i].delta_red_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0,
				CM_BLNDGAM_LUT_DATA, rgb[i].delta_green_reg);
		REG_SET(CM_BLNDGAM_LUT_DATA, 0,
				CM_BLNDGAM_LUT_DATA, rgb[i].delta_blue_reg);

	}

}

static void dcn20_dpp_cm_get_reg_field(
		struct dcn20_dpp *dpp,
		struct xfer_func_reg *reg)
{
	reg->shifts.exp_region0_lut_offset = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B;
	reg->masks.field_region_linear_slope = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B;
	reg->shifts.exp_region_start = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = dpp->tf_shift->CM_BLNDGAM_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = dpp->tf_mask->CM_BLNDGAM_RAMA_EXP_REGION_START_SEGMENT_B;
}

/*program blnd lut RAM A*/
static void dpp20_program_blnd_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dcn20_dpp_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_BLNDGAM_RAMA_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_BLNDGAM_RAMA_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_BLNDGAM_RAMA_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_BLNDGAM_RAMA_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_BLNDGAM_RAMA_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_BLNDGAM_RAMA_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_BLNDGAM_RAMA_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_BLNDGAM_RAMA_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_BLNDGAM_RAMA_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_BLNDGAM_RAMA_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_BLNDGAM_RAMA_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_BLNDGAM_RAMA_END_CNTL2_R);
	gam_regs.region_start = REG(CM_BLNDGAM_RAMA_REGION_0_1);
	gam_regs.region_end = REG(CM_BLNDGAM_RAMA_REGION_32_33);

	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);
}

/*program blnd lut RAM B*/
static void dpp20_program_blnd_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);
	struct xfer_func_reg gam_regs;

	dcn20_dpp_cm_get_reg_field(dpp, &gam_regs);

	gam_regs.start_cntl_b = REG(CM_BLNDGAM_RAMB_START_CNTL_B);
	gam_regs.start_cntl_g = REG(CM_BLNDGAM_RAMB_START_CNTL_G);
	gam_regs.start_cntl_r = REG(CM_BLNDGAM_RAMB_START_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(CM_BLNDGAM_RAMB_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(CM_BLNDGAM_RAMB_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(CM_BLNDGAM_RAMB_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(CM_BLNDGAM_RAMB_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(CM_BLNDGAM_RAMB_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(CM_BLNDGAM_RAMB_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(CM_BLNDGAM_RAMB_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(CM_BLNDGAM_RAMB_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(CM_BLNDGAM_RAMB_END_CNTL2_R);
	gam_regs.region_start = REG(CM_BLNDGAM_RAMB_REGION_0_1);
	gam_regs.region_end = REG(CM_BLNDGAM_RAMB_REGION_32_33);

	cm_helper_program_xfer_func(dpp->base.ctx, params, &gam_regs);
}

static enum dc_lut_mode dpp20_get_blndgam_current(struct dpp *dpp_base)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET(CM_BLNDGAM_LUT_WRITE_EN_MASK, CM_BLNDGAM_CONFIG_STATUS, &state_mode);

	switch (state_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 1:
		mode = LUT_RAM_A;
		break;
	case 2:
		mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}

	return mode;
}

bool dpp20_program_blnd_lut(
	struct dpp *dpp_base, const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	if (params == NULL) {
		REG_SET(CM_BLNDGAM_CONTROL, 0, CM_BLNDGAM_LUT_MODE, 0);
		return false;
	}
	current_mode = dpp20_get_blndgam_current(dpp_base);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dpp20_power_on_blnd_lut(dpp_base, true);
	dpp20_configure_blnd_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		dpp20_program_blnd_luta_settings(dpp_base, params);
	else
		dpp20_program_blnd_lutb_settings(dpp_base, params);

	dpp20_program_blnd_pwl(
			dpp_base, params->rgb_resulted, params->hw_points_num);

	REG_SET(CM_BLNDGAM_CONTROL, 0, CM_BLNDGAM_LUT_MODE,
			next_mode == LUT_RAM_A ? 1:2);

	return true;
}


static void dpp20_program_shaper_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i, red, green, blue;
	uint32_t  red_delta, green_delta, blue_delta;
	uint32_t  red_value, green_value, blue_value;

	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	for (i = 0 ; i < num; i++) {

		red   = rgb[i].red_reg;
		green = rgb[i].green_reg;
		blue  = rgb[i].blue_reg;

		red_delta   = rgb[i].delta_red_reg;
		green_delta = rgb[i].delta_green_reg;
		blue_delta  = rgb[i].delta_blue_reg;

		red_value   = ((red_delta   & 0x3ff) << 14) | (red   & 0x3fff);
		green_value = ((green_delta & 0x3ff) << 14) | (green & 0x3fff);
		blue_value  = ((blue_delta  & 0x3ff) << 14) | (blue  & 0x3fff);

		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, red_value);
		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, green_value);
		REG_SET(CM_SHAPER_LUT_DATA, 0, CM_SHAPER_LUT_DATA, blue_value);
	}

}

static enum dc_lut_mode dpp20_get_shaper_current(struct dpp *dpp_base)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET(CM_SHAPER_LUT_WRITE_EN_MASK, CM_SHAPER_CONFIG_STATUS, &state_mode);

	switch (state_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 1:
		mode = LUT_RAM_A;
		break;
	case 2:
		mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}

	return mode;
}

static void dpp20_configure_shaper_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(CM_SHAPER_LUT_WRITE_EN_MASK,
			CM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_SHAPER_LUT_WRITE_EN_MASK,
			CM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_SHAPER_LUT_INDEX, 0, CM_SHAPER_LUT_INDEX, 0);
}

/*program shaper RAM A*/

static void dpp20_program_shaper_luta_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_B, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_G, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_G, params->corner_points[0].green.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_SHAPER_RAMA_START_CNTL_R, 0,
		CM_SHAPER_RAMA_EXP_REGION_START_R, params->corner_points[0].red.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_B, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_G, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_G, params->corner_points[1].green.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_G, params->corner_points[1].green.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMA_END_CNTL_R, 0,
		CM_SHAPER_RAMA_EXP_REGION_END_R, params->corner_points[1].red.custom_float_x,
		CM_SHAPER_RAMA_EXP_REGION_END_BASE_R, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_SHAPER_RAMA_REGION_0_1, 0,
		CM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_2_3, 0,
		CM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_4_5, 0,
		CM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_6_7, 0,
		CM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_8_9, 0,
		CM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_10_11, 0,
		CM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_12_13, 0,
		CM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_14_15, 0,
		CM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_16_17, 0,
		CM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_18_19, 0,
		CM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_20_21, 0,
		CM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_22_23, 0,
		CM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_24_25, 0,
		CM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_26_27, 0,
		CM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_28_29, 0,
		CM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_30_31, 0,
		CM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMA_REGION_32_33, 0,
		CM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);
}

/*program shaper RAM B*/
static void dpp20_program_shaper_lutb_settings(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_B, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_G, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_G, params->corner_points[0].green.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_SHAPER_RAMB_START_CNTL_R, 0,
		CM_SHAPER_RAMB_EXP_REGION_START_R, params->corner_points[0].red.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_B, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_G, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_G, params->corner_points[1].green.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_G, params->corner_points[1].green.custom_float_y);

	REG_SET_2(CM_SHAPER_RAMB_END_CNTL_R, 0,
		CM_SHAPER_RAMB_EXP_REGION_END_R, params->corner_points[1].red.custom_float_x,
		CM_SHAPER_RAMB_EXP_REGION_END_BASE_R, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_SHAPER_RAMB_REGION_0_1, 0,
		CM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_2_3, 0,
		CM_SHAPER_RAMB_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_4_5, 0,
		CM_SHAPER_RAMB_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_6_7, 0,
		CM_SHAPER_RAMB_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_8_9, 0,
		CM_SHAPER_RAMB_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_10_11, 0,
		CM_SHAPER_RAMB_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_12_13, 0,
		CM_SHAPER_RAMB_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_14_15, 0,
		CM_SHAPER_RAMB_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_16_17, 0,
		CM_SHAPER_RAMB_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_18_19, 0,
		CM_SHAPER_RAMB_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_20_21, 0,
		CM_SHAPER_RAMB_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_22_23, 0,
		CM_SHAPER_RAMB_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_24_25, 0,
		CM_SHAPER_RAMB_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_26_27, 0,
		CM_SHAPER_RAMB_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_28_29, 0,
		CM_SHAPER_RAMB_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_30_31, 0,
		CM_SHAPER_RAMB_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_SHAPER_RAMB_REGION_32_33, 0,
		CM_SHAPER_RAMB_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_SHAPER_RAMB_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_SHAPER_RAMB_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_SHAPER_RAMB_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);

}


bool dpp20_program_shaper(
		struct dpp *dpp_base,
		const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;

	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	if (params == NULL) {
		REG_SET(CM_SHAPER_CONTROL, 0, CM_SHAPER_LUT_MODE, 0);
		return false;
	}
	current_mode = dpp20_get_shaper_current(dpp_base);

	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dpp20_configure_shaper_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		dpp20_program_shaper_luta_settings(dpp_base, params);
	else
		dpp20_program_shaper_lutb_settings(dpp_base, params);

	dpp20_program_shaper_lut(
			dpp_base, params->rgb_resulted, params->hw_points_num);

	REG_SET(CM_SHAPER_CONTROL, 0, CM_SHAPER_LUT_MODE, next_mode == LUT_RAM_A ? 1:2);

	return true;

}

static enum dc_lut_mode get3dlut_config(
			struct dpp *dpp_base,
			bool *is_17x17x17,
			bool *is_12bits_color_channel)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_GET_2(CM_3DLUT_READ_WRITE_CONTROL,
			CM_3DLUT_CONFIG_STATUS, &i_mode,
			CM_3DLUT_30BIT_EN, &i_enable_10bits);

	switch (i_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 1:
		mode = LUT_RAM_A;
		break;
	case 2:
		mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}
	if (i_enable_10bits > 0)
		*is_12bits_color_channel = false;
	else
		*is_12bits_color_channel = true;

	REG_GET(CM_3DLUT_MODE, CM_3DLUT_SIZE, &lut_size);

	if (lut_size == 0)
		*is_17x17x17 = true;
	else
		*is_17x17x17 = false;

	return mode;
}
/*
 * select ramA or ramB, or bypass
 * select color channel size 10 or 12 bits
 * select 3dlut size 17x17x17 or 9x9x9
 */
static void dpp20_set_3dlut_mode(
		struct dpp *dpp_base,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		bool is_lut_size17x17x17)
{
	uint32_t lut_mode;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	if (mode == LUT_BYPASS)
		lut_mode = 0;
	else if (mode == LUT_RAM_A)
		lut_mode = 1;
	else
		lut_mode = 2;

	REG_UPDATE_2(CM_3DLUT_MODE,
			CM_3DLUT_MODE, lut_mode,
			CM_3DLUT_SIZE, is_lut_size17x17x17 == true ? 0 : 1);
}

static void dpp20_select_3dlut_ram(
		struct dpp *dpp_base,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE_2(CM_3DLUT_READ_WRITE_CONTROL,
			CM_3DLUT_RAM_SEL, mode == LUT_RAM_A ? 0 : 1,
			CM_3DLUT_30BIT_EN,
			is_color_channel_12bits == true ? 0:1);
}



static void dpp20_set3dlut_ram12(
		struct dpp *dpp_base,
		const struct dc_rgb *lut,
		uint32_t entries)
{
	uint32_t i, red, green, blue, red1, green1, blue1;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	for (i = 0 ; i < entries; i += 2) {
		red   = lut[i].red<<4;
		green = lut[i].green<<4;
		blue  = lut[i].blue<<4;
		red1   = lut[i+1].red<<4;
		green1 = lut[i+1].green<<4;
		blue1  = lut[i+1].blue<<4;

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, red,
				CM_3DLUT_DATA1, red1);

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, green,
				CM_3DLUT_DATA1, green1);

		REG_SET_2(CM_3DLUT_DATA, 0,
				CM_3DLUT_DATA0, blue,
				CM_3DLUT_DATA1, blue1);

	}
}

/*
 * load selected lut with 10 bits color channels
 */
static void dpp20_set3dlut_ram10(
		struct dpp *dpp_base,
		const struct dc_rgb *lut,
		uint32_t entries)
{
	uint32_t i, red, green, blue, value;
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	for (i = 0; i < entries; i++) {
		red   = lut[i].red;
		green = lut[i].green;
		blue  = lut[i].blue;

		value = (red<<20) | (green<<10) | blue;

		REG_SET(CM_3DLUT_DATA_30BIT, 0, CM_3DLUT_DATA_30BIT, value);
	}

}


static void dpp20_select_3dlut_ram_mask(
		struct dpp *dpp_base,
		uint32_t ram_selection_mask)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(CM_3DLUT_READ_WRITE_CONTROL, CM_3DLUT_WRITE_EN_MASK,
			ram_selection_mask);
	REG_SET(CM_3DLUT_INDEX, 0, CM_3DLUT_INDEX, 0);
}

bool dpp20_program_3dlut(
		struct dpp *dpp_base,
		struct tetrahedral_params *params)
{
	enum dc_lut_mode mode;
	bool is_17x17x17;
	bool is_12bits_color_channel;
	struct dc_rgb *lut0;
	struct dc_rgb *lut1;
	struct dc_rgb *lut2;
	struct dc_rgb *lut3;
	int lut_size0;
	int lut_size;

	if (params == NULL) {
		dpp20_set_3dlut_mode(dpp_base, LUT_BYPASS, false, false);
		return false;
	}
	mode = get3dlut_config(dpp_base, &is_17x17x17, &is_12bits_color_channel);

	if (mode == LUT_BYPASS || mode == LUT_RAM_B)
		mode = LUT_RAM_A;
	else
		mode = LUT_RAM_B;

	is_17x17x17 = !params->use_tetrahedral_9;
	is_12bits_color_channel = params->use_12bits;
	if (is_17x17x17) {
		lut0 = params->tetrahedral_17.lut0;
		lut1 = params->tetrahedral_17.lut1;
		lut2 = params->tetrahedral_17.lut2;
		lut3 = params->tetrahedral_17.lut3;
		lut_size0 = sizeof(params->tetrahedral_17.lut0)/
					sizeof(params->tetrahedral_17.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_17.lut1)/
					sizeof(params->tetrahedral_17.lut1[0]);
	} else {
		lut0 = params->tetrahedral_9.lut0;
		lut1 = params->tetrahedral_9.lut1;
		lut2 = params->tetrahedral_9.lut2;
		lut3 = params->tetrahedral_9.lut3;
		lut_size0 = sizeof(params->tetrahedral_9.lut0)/
				sizeof(params->tetrahedral_9.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_9.lut1)/
				sizeof(params->tetrahedral_9.lut1[0]);
		}

	dpp20_select_3dlut_ram(dpp_base, mode,
				is_12bits_color_channel);
	dpp20_select_3dlut_ram_mask(dpp_base, 0x1);
	if (is_12bits_color_channel)
		dpp20_set3dlut_ram12(dpp_base, lut0, lut_size0);
	else
		dpp20_set3dlut_ram10(dpp_base, lut0, lut_size0);

	dpp20_select_3dlut_ram_mask(dpp_base, 0x2);
	if (is_12bits_color_channel)
		dpp20_set3dlut_ram12(dpp_base, lut1, lut_size);
	else
		dpp20_set3dlut_ram10(dpp_base, lut1, lut_size);

	dpp20_select_3dlut_ram_mask(dpp_base, 0x4);
	if (is_12bits_color_channel)
		dpp20_set3dlut_ram12(dpp_base, lut2, lut_size);
	else
		dpp20_set3dlut_ram10(dpp_base, lut2, lut_size);

	dpp20_select_3dlut_ram_mask(dpp_base, 0x8);
	if (is_12bits_color_channel)
		dpp20_set3dlut_ram12(dpp_base, lut3, lut_size);
	else
		dpp20_set3dlut_ram10(dpp_base, lut3, lut_size);


	dpp20_set_3dlut_mode(dpp_base, mode, is_12bits_color_channel,
					is_17x17x17);

	return true;
}

void dpp2_set_hdr_multiplier(
		struct dpp *dpp_base,
		uint32_t multiplier)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	REG_UPDATE(CM_HDR_MULT_COEF, CM_HDR_MULT_COEF, multiplier);
}
