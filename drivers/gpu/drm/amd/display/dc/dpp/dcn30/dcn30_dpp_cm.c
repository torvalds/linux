/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dcn30/dcn30_dpp.h"
#include "basics/conversion.h"
#include "dcn30/dcn30_cm_common.h"

#define REG(reg)\
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

static void dpp3_enable_cm_block(
		struct dpp *dpp_base)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	unsigned int cm_bypass_mode = 0;

	// debug option: put CM in bypass mode
	if (dpp_base->ctx->dc->debug.cm_in_bypass)
		cm_bypass_mode = 1;

	REG_UPDATE(CM_CONTROL, CM_BYPASS, cm_bypass_mode);
}

static enum dc_lut_mode dpp30_get_gamcor_current(struct dpp *dpp_base)
{
	enum dc_lut_mode mode = LUT_BYPASS;
	uint32_t state_mode;
	uint32_t lut_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_GET(CM_GAMCOR_CONTROL, CM_GAMCOR_MODE_CURRENT, &state_mode);

	if (state_mode == 2) {//Programmable RAM LUT
		REG_GET(CM_GAMCOR_CONTROL, CM_GAMCOR_SELECT_CURRENT, &lut_mode);
		if (lut_mode == 0)
			mode = LUT_RAM_A;
		else
			mode = LUT_RAM_B;
	}

	return mode;
}

static void dpp3_program_gammcor_lut(
		struct dpp *dpp_base,
		const struct pwl_result_data *rgb,
		uint32_t num,
		bool is_ram_a)
{
	uint32_t i;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	uint32_t last_base_value_red = rgb[num-1].red_reg + rgb[num-1].delta_red_reg;
	uint32_t last_base_value_green = rgb[num-1].green_reg + rgb[num-1].delta_green_reg;
	uint32_t last_base_value_blue = rgb[num-1].blue_reg + rgb[num-1].delta_blue_reg;

	/*fill in the LUT with all base values to be used by pwl module
	 * HW auto increments the LUT index: back-to-back write
	 */
	if (is_rgb_equal(rgb,  num)) {
		for (i = 0 ; i < num; i++)
			REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, rgb[i].red_reg);

		REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, last_base_value_red);

	} else {
		REG_UPDATE(CM_GAMCOR_LUT_CONTROL,
				CM_GAMCOR_LUT_WRITE_COLOR_MASK, 4);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, rgb[i].red_reg);

		REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, last_base_value_red);

		REG_SET(CM_GAMCOR_LUT_INDEX, 0, CM_GAMCOR_LUT_INDEX, 0);

		REG_UPDATE(CM_GAMCOR_LUT_CONTROL,
				CM_GAMCOR_LUT_WRITE_COLOR_MASK, 2);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, rgb[i].green_reg);

		REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, last_base_value_green);

		REG_SET(CM_GAMCOR_LUT_INDEX, 0, CM_GAMCOR_LUT_INDEX, 0);

		REG_UPDATE(CM_GAMCOR_LUT_CONTROL,
				CM_GAMCOR_LUT_WRITE_COLOR_MASK, 1);
		for (i = 0 ; i < num; i++)
			REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, rgb[i].blue_reg);

		REG_SET(CM_GAMCOR_LUT_DATA, 0, CM_GAMCOR_LUT_DATA, last_base_value_blue);
	}
}

static void dpp3_power_on_gamcor_lut(
		struct dpp *dpp_base,
	bool power_on)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm) {
		if (power_on) {
			REG_UPDATE(CM_MEM_PWR_CTRL, GAMCOR_MEM_PWR_FORCE, 0);
			REG_WAIT(CM_MEM_PWR_STATUS, GAMCOR_MEM_PWR_STATE, 0, 1, 5);
		} else {
			dpp_base->ctx->dc->optimized_required = true;
			dpp_base->deferred_reg_writes.bits.disable_gamcor = true;
		}
	} else
		REG_SET(CM_MEM_PWR_CTRL, 0,
				GAMCOR_MEM_PWR_DIS, power_on == true ? 0:1);
}

void dpp3_program_cm_dealpha(
		struct dpp *dpp_base,
	uint32_t enable, uint32_t additive_blending)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_SET_2(CM_DEALPHA, 0,
			CM_DEALPHA_EN, enable,
			CM_DEALPHA_ABLND, additive_blending);
}

void dpp3_program_cm_bias(
	struct dpp *dpp_base,
	struct CM_bias_params *bias_params)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_SET(CM_BIAS_CR_R, 0, CM_BIAS_CR_R, bias_params->cm_bias_cr_r);
	REG_SET_2(CM_BIAS_Y_G_CB_B, 0,
			CM_BIAS_Y_G, bias_params->cm_bias_y_g,
			CM_BIAS_CB_B, bias_params->cm_bias_cb_b);
}

static void dpp3_gamcor_reg_field(
		struct dcn3_dpp *dpp,
		struct dcn3_xfer_func_reg *reg)
{

	reg->shifts.field_region_start_base = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_START_BASE_B;
	reg->masks.field_region_start_base = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_START_BASE_B;
	reg->shifts.field_offset = dpp->tf_shift->CM_GAMCOR_RAMA_OFFSET_B;
	reg->masks.field_offset = dpp->tf_mask->CM_GAMCOR_RAMA_OFFSET_B;

	reg->shifts.exp_region0_lut_offset = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_region_linear_slope = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_B;
	reg->shifts.exp_region_start = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = dpp->tf_shift->CM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = dpp->tf_mask->CM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_B;
}

static void dpp3_configure_gamcor_lut(
		struct dpp *dpp_base,
		bool is_ram_a)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE(CM_GAMCOR_LUT_CONTROL,
			CM_GAMCOR_LUT_WRITE_COLOR_MASK, 7);
	REG_UPDATE(CM_GAMCOR_LUT_CONTROL,
			CM_GAMCOR_LUT_HOST_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_GAMCOR_LUT_INDEX, 0, CM_GAMCOR_LUT_INDEX, 0);
}


bool dpp3_program_gamcor_lut(
	struct dpp *dpp_base, const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	struct dcn3_xfer_func_reg gam_regs;

	dpp3_enable_cm_block(dpp_base);

	if (params == NULL) { //bypass if we have no pwl data
		REG_SET(CM_GAMCOR_CONTROL, 0, CM_GAMCOR_MODE, 0);
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
			dpp3_power_on_gamcor_lut(dpp_base, false);
		return false;
	}
	dpp3_power_on_gamcor_lut(dpp_base, true);
	REG_SET(CM_GAMCOR_CONTROL, 0, CM_GAMCOR_MODE, 2);

	current_mode = dpp30_get_gamcor_current(dpp_base);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dpp3_power_on_gamcor_lut(dpp_base, true);
	dpp3_configure_gamcor_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_B) {
		gam_regs.start_cntl_b = REG(CM_GAMCOR_RAMB_START_CNTL_B);
		gam_regs.start_cntl_g = REG(CM_GAMCOR_RAMB_START_CNTL_G);
		gam_regs.start_cntl_r = REG(CM_GAMCOR_RAMB_START_CNTL_R);
		gam_regs.start_slope_cntl_b = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_B);
		gam_regs.start_slope_cntl_g = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_G);
		gam_regs.start_slope_cntl_r = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_R);
		gam_regs.start_end_cntl1_b = REG(CM_GAMCOR_RAMB_END_CNTL1_B);
		gam_regs.start_end_cntl2_b = REG(CM_GAMCOR_RAMB_END_CNTL2_B);
		gam_regs.start_end_cntl1_g = REG(CM_GAMCOR_RAMB_END_CNTL1_G);
		gam_regs.start_end_cntl2_g = REG(CM_GAMCOR_RAMB_END_CNTL2_G);
		gam_regs.start_end_cntl1_r = REG(CM_GAMCOR_RAMB_END_CNTL1_R);
		gam_regs.start_end_cntl2_r = REG(CM_GAMCOR_RAMB_END_CNTL2_R);
		gam_regs.region_start = REG(CM_GAMCOR_RAMB_REGION_0_1);
		gam_regs.region_end = REG(CM_GAMCOR_RAMB_REGION_32_33);
		//New registers in DCN3AG/DCN GAMCOR block
		gam_regs.offset_b =  REG(CM_GAMCOR_RAMB_OFFSET_B);
		gam_regs.offset_g =  REG(CM_GAMCOR_RAMB_OFFSET_G);
		gam_regs.offset_r =  REG(CM_GAMCOR_RAMB_OFFSET_R);
		gam_regs.start_base_cntl_b = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_B);
		gam_regs.start_base_cntl_g = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_G);
		gam_regs.start_base_cntl_r = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_R);
	} else {
		gam_regs.start_cntl_b = REG(CM_GAMCOR_RAMA_START_CNTL_B);
		gam_regs.start_cntl_g = REG(CM_GAMCOR_RAMA_START_CNTL_G);
		gam_regs.start_cntl_r = REG(CM_GAMCOR_RAMA_START_CNTL_R);
		gam_regs.start_slope_cntl_b = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_B);
		gam_regs.start_slope_cntl_g = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_G);
		gam_regs.start_slope_cntl_r = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_R);
		gam_regs.start_end_cntl1_b = REG(CM_GAMCOR_RAMA_END_CNTL1_B);
		gam_regs.start_end_cntl2_b = REG(CM_GAMCOR_RAMA_END_CNTL2_B);
		gam_regs.start_end_cntl1_g = REG(CM_GAMCOR_RAMA_END_CNTL1_G);
		gam_regs.start_end_cntl2_g = REG(CM_GAMCOR_RAMA_END_CNTL2_G);
		gam_regs.start_end_cntl1_r = REG(CM_GAMCOR_RAMA_END_CNTL1_R);
		gam_regs.start_end_cntl2_r = REG(CM_GAMCOR_RAMA_END_CNTL2_R);
		gam_regs.region_start = REG(CM_GAMCOR_RAMA_REGION_0_1);
		gam_regs.region_end = REG(CM_GAMCOR_RAMA_REGION_32_33);
		//New registers in DCN3AG/DCN GAMCOR block
		gam_regs.offset_b =  REG(CM_GAMCOR_RAMA_OFFSET_B);
		gam_regs.offset_g =  REG(CM_GAMCOR_RAMA_OFFSET_G);
		gam_regs.offset_r =  REG(CM_GAMCOR_RAMA_OFFSET_R);
		gam_regs.start_base_cntl_b = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_B);
		gam_regs.start_base_cntl_g = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_G);
		gam_regs.start_base_cntl_r = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_R);
	}

	//get register fields
	dpp3_gamcor_reg_field(dpp, &gam_regs);

	//program register set for LUTA/LUTB
	cm_helper_program_gamcor_xfer_func(dpp_base->ctx, params, &gam_regs);

	dpp3_program_gammcor_lut(dpp_base, params->rgb_resulted, params->hw_points_num,
				 next_mode == LUT_RAM_A);

	//select Gamma LUT to use for next frame
	REG_UPDATE(CM_GAMCOR_CONTROL, CM_GAMCOR_SELECT, next_mode == LUT_RAM_A ? 0:1);

	return true;
}

void dpp3_set_hdr_multiplier(
		struct dpp *dpp_base,
		uint32_t multiplier)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);

	REG_UPDATE(CM_HDR_MULT_COEF, CM_HDR_MULT_COEF, multiplier);
}


static void program_gamut_remap(
		struct dcn3_dpp *dpp,
		const uint16_t *regval,
		int select)
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
		/*this corresponds to GAMUT_REMAP coefficients set B
		 *we don't have common coefficient sets in dcn3ag/dcn3
		 */
	case GAMUT_REMAP_COMA_COEFF:
		selection = 2;
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

		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_B_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_B_C33_C34);

		cm_helper_program_color_matrices(
				dpp->base.ctx,
				regval,
				&gam_regs);

	}
	//select coefficient set to use
	REG_SET(
			CM_GAMUT_REMAP_CONTROL, 0,
			CM_GAMUT_REMAP_MODE, selection);
}

void dpp3_cm_set_gamut_remap(
	struct dpp *dpp_base,
	const struct dpp_grph_csc_adjustment *adjust)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	int i = 0;
	int gamut_mode;

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		/* Bypass if type is bypass or hw */
		program_gamut_remap(dpp, NULL, GAMUT_REMAP_BYPASS);
	else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		for (i = 0; i < 12; i++)
			arr_matrix[i] = adjust->temperature_matrix[i];

		convert_float_matrix(
			arr_reg_val, arr_matrix, 12);

		//current coefficient set in use
		REG_GET(CM_GAMUT_REMAP_CONTROL, CM_GAMUT_REMAP_MODE_CURRENT, &gamut_mode);

		if (gamut_mode == 0)
			gamut_mode = 1; //use coefficient set A
		else if (gamut_mode == 1)
			gamut_mode = 2;
		else
			gamut_mode = 1;

		//follow dcn2 approach for now - using only coefficient set A
		program_gamut_remap(dpp, arr_reg_val, gamut_mode);
	}
}

static void read_gamut_remap(struct dcn3_dpp *dpp,
			     uint16_t *regval,
			     int *select)
{
	struct color_matrices_reg gam_regs;
	uint32_t selection;

	//current coefficient set in use
	REG_GET(CM_GAMUT_REMAP_CONTROL, CM_GAMUT_REMAP_MODE_CURRENT, &selection);

	*select = selection;

	gam_regs.shifts.csc_c11 = dpp->tf_shift->CM_GAMUT_REMAP_C11;
	gam_regs.masks.csc_c11  = dpp->tf_mask->CM_GAMUT_REMAP_C11;
	gam_regs.shifts.csc_c12 = dpp->tf_shift->CM_GAMUT_REMAP_C12;
	gam_regs.masks.csc_c12 = dpp->tf_mask->CM_GAMUT_REMAP_C12;

	if (*select == GAMUT_REMAP_COEFF) {
		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_C33_C34);

		cm_helper_read_color_matrices(dpp->base.ctx,
					      regval,
					      &gam_regs);

	} else if (*select == GAMUT_REMAP_COMA_COEFF) {
		gam_regs.csc_c11_c12 = REG(CM_GAMUT_REMAP_B_C11_C12);
		gam_regs.csc_c33_c34 = REG(CM_GAMUT_REMAP_B_C33_C34);

		cm_helper_read_color_matrices(dpp->base.ctx,
					      regval,
					      &gam_regs);
	}
}

void dpp3_cm_get_gamut_remap(struct dpp *dpp_base,
			     struct dpp_grph_csc_adjustment *adjust)
{
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	uint16_t arr_reg_val[12] = {0};
	int select;

	read_gamut_remap(dpp, arr_reg_val, &select);

	if (select == GAMUT_REMAP_BYPASS) {
		adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
		return;
	}

	adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
	convert_hw_matrix(adjust->temperature_matrix,
			  arr_reg_val, ARRAY_SIZE(arr_reg_val));
}
