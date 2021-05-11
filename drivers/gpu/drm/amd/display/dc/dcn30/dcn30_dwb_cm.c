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

#include "reg_helper.h"
#include "fixed31_32.h"
#include "resource.h"
#include "basics/conversion.h"
#include "dwb.h"
#include "dcn30_dwb.h"
#include "dcn30_cm_common.h"
#include "dcn10/dcn10_cm_common.h"


#define REG(reg)\
	dwbc30->dwbc_regs->reg

#define CTX \
	dwbc30->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dwbc30->dwbc_shift->field_name, dwbc30->dwbc_mask->field_name

#define TO_DCN30_DWBC(dwbc_base) \
	container_of(dwbc_base, struct dcn30_dwbc, base)

static void dwb3_get_reg_field_ogam(struct dcn30_dwbc *dwbc30,
	struct dcn3_xfer_func_reg *reg)
{
	reg->shifts.exp_region0_lut_offset = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_region_linear_slope = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_offset = dwbc30->dwbc_mask->DWB_OGAM_RAMA_OFFSET_B;
	reg->shifts.field_offset = dwbc30->dwbc_shift->DWB_OGAM_RAMA_OFFSET_B;
	reg->shifts.exp_region_start = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = dwbc30->dwbc_shift->DWB_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = dwbc30->dwbc_mask->DWB_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
}

/*program dwb ogam RAM A*/
static void dwb3_program_ogam_luta_settings(
	struct dcn30_dwbc *dwbc30,
	const struct pwl_params *params)
{
	struct dcn3_xfer_func_reg gam_regs;

	dwb3_get_reg_field_ogam(dwbc30, &gam_regs);

	gam_regs.start_cntl_b = REG(DWB_OGAM_RAMA_START_CNTL_B);
	gam_regs.start_cntl_g = REG(DWB_OGAM_RAMA_START_CNTL_G);
	gam_regs.start_cntl_r = REG(DWB_OGAM_RAMA_START_CNTL_R);
	gam_regs.start_base_cntl_b = REG(DWB_OGAM_RAMA_START_BASE_CNTL_B);
	gam_regs.start_base_cntl_g = REG(DWB_OGAM_RAMA_START_BASE_CNTL_G);
	gam_regs.start_base_cntl_r = REG(DWB_OGAM_RAMA_START_BASE_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(DWB_OGAM_RAMA_START_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(DWB_OGAM_RAMA_START_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(DWB_OGAM_RAMA_START_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(DWB_OGAM_RAMA_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(DWB_OGAM_RAMA_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(DWB_OGAM_RAMA_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(DWB_OGAM_RAMA_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(DWB_OGAM_RAMA_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(DWB_OGAM_RAMA_END_CNTL2_R);
	gam_regs.offset_b = REG(DWB_OGAM_RAMA_OFFSET_B);
	gam_regs.offset_g = REG(DWB_OGAM_RAMA_OFFSET_G);
	gam_regs.offset_r = REG(DWB_OGAM_RAMA_OFFSET_R);
	gam_regs.region_start = REG(DWB_OGAM_RAMA_REGION_0_1);
	gam_regs.region_end = REG(DWB_OGAM_RAMA_REGION_32_33);
	/*todo*/
	cm_helper_program_gamcor_xfer_func(dwbc30->base.ctx, params, &gam_regs);
}

/*program dwb ogam RAM B*/
static void dwb3_program_ogam_lutb_settings(
	struct dcn30_dwbc *dwbc30,
	const struct pwl_params *params)
{
	struct dcn3_xfer_func_reg gam_regs;

	dwb3_get_reg_field_ogam(dwbc30, &gam_regs);

	gam_regs.start_cntl_b = REG(DWB_OGAM_RAMB_START_CNTL_B);
	gam_regs.start_cntl_g = REG(DWB_OGAM_RAMB_START_CNTL_G);
	gam_regs.start_cntl_r = REG(DWB_OGAM_RAMB_START_CNTL_R);
	gam_regs.start_base_cntl_b = REG(DWB_OGAM_RAMB_START_BASE_CNTL_B);
	gam_regs.start_base_cntl_g = REG(DWB_OGAM_RAMB_START_BASE_CNTL_G);
	gam_regs.start_base_cntl_r = REG(DWB_OGAM_RAMB_START_BASE_CNTL_R);
	gam_regs.start_slope_cntl_b = REG(DWB_OGAM_RAMB_START_SLOPE_CNTL_B);
	gam_regs.start_slope_cntl_g = REG(DWB_OGAM_RAMB_START_SLOPE_CNTL_G);
	gam_regs.start_slope_cntl_r = REG(DWB_OGAM_RAMB_START_SLOPE_CNTL_R);
	gam_regs.start_end_cntl1_b = REG(DWB_OGAM_RAMB_END_CNTL1_B);
	gam_regs.start_end_cntl2_b = REG(DWB_OGAM_RAMB_END_CNTL2_B);
	gam_regs.start_end_cntl1_g = REG(DWB_OGAM_RAMB_END_CNTL1_G);
	gam_regs.start_end_cntl2_g = REG(DWB_OGAM_RAMB_END_CNTL2_G);
	gam_regs.start_end_cntl1_r = REG(DWB_OGAM_RAMB_END_CNTL1_R);
	gam_regs.start_end_cntl2_r = REG(DWB_OGAM_RAMB_END_CNTL2_R);
	gam_regs.offset_b = REG(DWB_OGAM_RAMB_OFFSET_B);
	gam_regs.offset_g = REG(DWB_OGAM_RAMB_OFFSET_G);
	gam_regs.offset_r = REG(DWB_OGAM_RAMB_OFFSET_R);
	gam_regs.region_start = REG(DWB_OGAM_RAMB_REGION_0_1);
	gam_regs.region_end = REG(DWB_OGAM_RAMB_REGION_32_33);

	cm_helper_program_gamcor_xfer_func(dwbc30->base.ctx, params, &gam_regs);
}

static enum dc_lut_mode dwb3_get_ogam_current(
	struct dcn30_dwbc *dwbc30)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	uint32_t ram_select;

	REG_GET(DWB_OGAM_CONTROL,
		DWB_OGAM_MODE, &state_mode);
	REG_GET(DWB_OGAM_CONTROL,
		DWB_OGAM_SELECT, &ram_select);

	if (state_mode == 0) {
		mode = LUT_BYPASS;
	} else if (state_mode == 2) {
		if (ram_select == 0)
			mode = LUT_RAM_A;
		else
			mode = LUT_RAM_B;
	} else {
		// Reserved value
		mode = LUT_BYPASS;
		BREAK_TO_DEBUGGER();
		return mode;
	}
	return mode;
}

static void dwb3_configure_ogam_lut(
	struct dcn30_dwbc *dwbc30,
	bool is_ram_a)
{
	REG_UPDATE(DWB_OGAM_LUT_CONTROL,
		DWB_OGAM_LUT_READ_COLOR_SEL, 7);
	REG_UPDATE(DWB_OGAM_CONTROL,
		DWB_OGAM_SELECT, is_ram_a == true ? 0 : 1);
	REG_SET(DWB_OGAM_LUT_INDEX, 0, DWB_OGAM_LUT_INDEX, 0);
}

static void dwb3_program_ogam_pwl(struct dcn30_dwbc *dwbc30,
	const struct pwl_result_data *rgb,
	uint32_t num)
{
	uint32_t i;

    // triple base implementation
	for (i = 0; i < num/2; i++) {
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+0].red_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+0].green_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+0].blue_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+1].red_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+1].green_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+1].blue_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+2].red_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+2].green_reg);
		REG_SET(DWB_OGAM_LUT_DATA, 0, DWB_OGAM_LUT_DATA, rgb[2*i+2].blue_reg);
	}
}

static bool dwb3_program_ogam_lut(
	struct dcn30_dwbc *dwbc30,
	const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;

	if (params == NULL) {
		REG_SET(DWB_OGAM_CONTROL, 0, DWB_OGAM_MODE, 0);
		return false;
	}

	current_mode = dwb3_get_ogam_current(dwbc30);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dwb3_configure_ogam_lut(dwbc30, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		dwb3_program_ogam_luta_settings(dwbc30, params);
	else
		dwb3_program_ogam_lutb_settings(dwbc30, params);

	dwb3_program_ogam_pwl(
		dwbc30, params->rgb_resulted, params->hw_points_num);

	REG_SET(DWB_OGAM_CONTROL, 0, DWB_OGAM_MODE, 2);
	REG_SET(DWB_OGAM_CONTROL, 0, DWB_OGAM_SELECT, next_mode == LUT_RAM_A ? 0 : 1);

	return true;
}

bool dwb3_ogam_set_input_transfer_func(
	struct dwbc *dwbc,
	const struct dc_transfer_func *in_transfer_func_dwb_ogam)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);
	bool result = false;
	struct pwl_params *dwb_ogam_lut = NULL;

	if (in_transfer_func_dwb_ogam == NULL)
		return result;

	dwb_ogam_lut = kzalloc(sizeof(*dwb_ogam_lut), GFP_KERNEL);

	if (dwb_ogam_lut) {
		cm_helper_translate_curve_to_hw_format(
			in_transfer_func_dwb_ogam,
			dwb_ogam_lut, false);

		result = dwb3_program_ogam_lut(
			dwbc30,
			dwb_ogam_lut);
		kfree(dwb_ogam_lut);
		dwb_ogam_lut = NULL;
	}

	return result;
}

static void dwb3_program_gamut_remap(
		struct dwbc *dwbc,
		const uint16_t *regval,
		enum cm_gamut_coef_format coef_format,
		enum cm_gamut_remap_select select)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	struct color_matrices_reg gam_regs;

	REG_UPDATE(DWB_GAMUT_REMAP_COEF_FORMAT, DWB_GAMUT_REMAP_COEF_FORMAT, coef_format);

	if (regval == NULL || select == CM_GAMUT_REMAP_MODE_BYPASS) {
		REG_SET(DWB_GAMUT_REMAP_MODE, 0,
				DWB_GAMUT_REMAP_MODE, 0);
		return;
	}

	switch (select) {
	case CM_GAMUT_REMAP_MODE_RAMA_COEFF:
		gam_regs.csc_c11_c12 = REG(DWB_GAMUT_REMAPA_C11_C12);
		gam_regs.csc_c33_c34 = REG(DWB_GAMUT_REMAPA_C33_C34);

		cm_helper_program_color_matrices(
				dwbc30->base.ctx,
				regval,
				&gam_regs);
		break;
	case CM_GAMUT_REMAP_MODE_RAMB_COEFF:
		gam_regs.csc_c11_c12 = REG(DWB_GAMUT_REMAPB_C11_C12);
		gam_regs.csc_c33_c34 = REG(DWB_GAMUT_REMAPB_C33_C34);

		cm_helper_program_color_matrices(
				dwbc30->base.ctx,
				regval,
				&gam_regs);
		break;
	case CM_GAMUT_REMAP_MODE_RESERVED:
		/* should never happen, bug */
		BREAK_TO_DEBUGGER();
		return;
	default:
		break;
	}

	REG_SET(DWB_GAMUT_REMAP_MODE, 0,
			DWB_GAMUT_REMAP_MODE, select);

}

void dwb3_set_gamut_remap(
	struct dwbc *dwbc,
	const struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);
	struct cm_grph_csc_adjustment adjust = params->csc_params;
	int i = 0;

	if (adjust.gamut_adjust_type != CM_GAMUT_ADJUST_TYPE_SW) {
		/* Bypass if type is bypass or hw */
		dwb3_program_gamut_remap(dwbc, NULL, adjust.gamut_coef_format, CM_GAMUT_REMAP_MODE_BYPASS);
	} else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];
		unsigned int current_mode;

		for (i = 0; i < 12; i++)
			arr_matrix[i] = adjust.temperature_matrix[i];

		convert_float_matrix(arr_reg_val, arr_matrix, 12);

		REG_GET(DWB_GAMUT_REMAP_MODE, DWB_GAMUT_REMAP_MODE_CURRENT, &current_mode);

		if (current_mode == CM_GAMUT_REMAP_MODE_RAMA_COEFF) {
			dwb3_program_gamut_remap(dwbc, arr_reg_val,
					adjust.gamut_coef_format, CM_GAMUT_REMAP_MODE_RAMB_COEFF);
		} else {
			dwb3_program_gamut_remap(dwbc, arr_reg_val,
					adjust.gamut_coef_format, CM_GAMUT_REMAP_MODE_RAMA_COEFF);
		}
	}
}

void dwb3_program_hdr_mult(
	struct dwbc *dwbc,
	const struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	REG_UPDATE(DWB_HDR_MULT_COEF, DWB_HDR_MULT_COEF, params->hdr_mult);
}
