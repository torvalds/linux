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

#include "reg_helper.h"
#include "dcn10_dpp.h"

#include "dcn10_cm_common.h"

#define REG(reg) reg

#define CTX \
	ctx

#undef FN
#define FN(reg_name, field_name) \
	reg->shifts.field_name, reg->masks.field_name

void cm_helper_program_color_matrices(
		struct dc_context *ctx,
		const uint16_t *regval,
		const struct color_matrices_reg *reg)
{
	uint32_t cur_csc_reg;
	unsigned int i = 0;

	for (cur_csc_reg = reg->csc_c11_c12;
			cur_csc_reg <= reg->csc_c33_c34;
			cur_csc_reg++) {

		const uint16_t *regval0 = &(regval[2 * i]);
		const uint16_t *regval1 = &(regval[(2 * i) + 1]);

		REG_SET_2(cur_csc_reg, 0,
				csc_c11, *regval0,
				csc_c12, *regval1);

		i++;
	}

}

void cm_helper_program_xfer_func(
		struct dc_context *ctx,
		const struct pwl_params *params,
		const struct xfer_func_reg *reg)
{
	uint32_t reg_region_cur;
	unsigned int i = 0;

	REG_SET_2(reg->start_cntl_b, 0,
			exp_region_start, params->arr_points[0].custom_float_x,
			exp_resion_start_segment, 0);
	REG_SET_2(reg->start_cntl_g, 0,
			exp_region_start, params->arr_points[0].custom_float_x,
			exp_resion_start_segment, 0);
	REG_SET_2(reg->start_cntl_r, 0,
			exp_region_start, params->arr_points[0].custom_float_x,
			exp_resion_start_segment, 0);

	REG_SET(reg->start_slope_cntl_b, 0,
			field_region_linear_slope, params->arr_points[0].custom_float_slope);
	REG_SET(reg->start_slope_cntl_g, 0,
			field_region_linear_slope, params->arr_points[0].custom_float_slope);
	REG_SET(reg->start_slope_cntl_r, 0,
			field_region_linear_slope, params->arr_points[0].custom_float_slope);

	REG_SET(reg->start_end_cntl1_b, 0,
			field_region_end, params->arr_points[1].custom_float_x);
	REG_SET_2(reg->start_end_cntl2_b, 0,
			field_region_end_slope, params->arr_points[1].custom_float_slope,
			field_region_end_base, params->arr_points[1].custom_float_y);

	REG_SET(reg->start_end_cntl1_g, 0,
			field_region_end, params->arr_points[1].custom_float_x);
	REG_SET_2(reg->start_end_cntl2_g, 0,
			field_region_end_slope, params->arr_points[1].custom_float_slope,
		field_region_end_base, params->arr_points[1].custom_float_y);

	REG_SET(reg->start_end_cntl1_r, 0,
			field_region_end, params->arr_points[1].custom_float_x);
	REG_SET_2(reg->start_end_cntl2_r, 0,
			field_region_end_slope, params->arr_points[1].custom_float_slope,
		field_region_end_base, params->arr_points[1].custom_float_y);

	for (reg_region_cur = reg->region_start;
			reg_region_cur <= reg->region_end;
			reg_region_cur++) {

		const struct gamma_curve *curve0 = &(params->arr_curve_points[2 * i]);
		const struct gamma_curve *curve1 = &(params->arr_curve_points[(2 * i) + 1]);

		REG_SET_4(reg_region_cur, 0,
				exp_region0_lut_offset, curve0->offset,
				exp_region0_num_segments, curve0->segments_num,
				exp_region1_lut_offset, curve1->offset,
				exp_region1_num_segments, curve1->segments_num);

		i++;
	}

}
