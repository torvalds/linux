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
#include "dc.h"
#include "reg_helper.h"
#include "dcn10_dpp.h"

#include "dcn10_cm_common.h"
#include "custom_float.h"

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



bool cm_helper_convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num,
		bool fixpoint)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = false;

	if (!convert_to_custom_float_format(arr_points[0].x, &fmt,
					    &arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[0].offset, &fmt,
					    &arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[0].slope, &fmt,
					    &arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(arr_points[1].x, &fmt,
					    &arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (fixpoint == true)
		arr_points[1].custom_float_y = dal_fixed31_32_clamp_u0d14(arr_points[1].y);
	else if (!convert_to_custom_float_format(arr_points[1].y, &fmt,
		&arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(arr_points[1].slope, &fmt,
					    &arr_points[1].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (hw_points_num == 0 || rgb_resulted == NULL || fixpoint == true)
		return true;

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(rgb->red, &fmt,
						    &rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->green, &fmt,
						    &rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->blue, &fmt,
						    &rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_red, &fmt,
						    &rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_green, &fmt,
						    &rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(rgb->delta_blue, &fmt,
						    &rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}


#define MAX_REGIONS_NUMBER 34
#define MAX_LOW_POINT      25
#define NUMBER_SEGMENTS    32

bool cm_helper_translate_curve_to_hw_format(
				const struct dc_transfer_func *output_tf,
				struct pwl_params *lut_params, bool fixpoint)
{
	struct curve_points *arr_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;
	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;
	struct fixed31_32 y1_min;
	struct fixed31_32 y3_max;

	int32_t segment_start, segment_end;
	int32_t i;
	uint32_t j, k, seg_distr[MAX_REGIONS_NUMBER], increment, start_index, hw_points;

	if (output_tf == NULL || lut_params == NULL || output_tf->type == TF_TYPE_BYPASS)
		return false;

	PERF_TRACE();

	arr_points = lut_params->arr_points;
	rgb_resulted = lut_params->rgb_resulted;
	hw_points = 0;

	memset(lut_params, 0, sizeof(struct pwl_params));
	memset(seg_distr, 0, sizeof(seg_distr));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* 32 segments
		 * segments are from 2^-25 to 2^7
		 */
		for (i = 0; i < 32 ; i++)
			seg_distr[i] = 3;

		segment_start = -25;
		segment_end   = 7;
	} else {
		/* 10 segments
		 * segment is from 2^-10 to 2^0
		 * There are less than 256 points, for optimization
		 */
		seg_distr[0] = 3;
		seg_distr[1] = 4;
		seg_distr[2] = 4;
		seg_distr[3] = 4;
		seg_distr[4] = 4;
		seg_distr[5] = 4;
		seg_distr[6] = 4;
		seg_distr[7] = 4;
		seg_distr[8] = 5;
		seg_distr[9] = 5;

		segment_start = -10;
		segment_end = 0;
	}

	for (i = segment_end - segment_start; i < MAX_REGIONS_NUMBER ; i++)
		seg_distr[i] = -1;

	for (k = 0; k < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (segment_end - segment_start); k++) {
		increment = NUMBER_SEGMENTS / (1 << seg_distr[k]);
		start_index = (segment_start + k + MAX_LOW_POINT) * NUMBER_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SEGMENTS; i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (segment_end + MAX_LOW_POINT) * NUMBER_SEGMENTS;
	rgb_resulted[hw_points - 1].red = output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green = output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue = output_tf->tf_pts.blue[start_index];

	arr_points[0].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
					     dal_fixed31_32_from_int(segment_start));
	arr_points[1].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
					     dal_fixed31_32_from_int(segment_end));

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	arr_points[0].y = y1_min;
	arr_points[0].slope = dal_fixed31_32_div(arr_points[0].y, arr_points[0].x);
	y_r = rgb_resulted[hw_points - 1].red;
	y_g = rgb_resulted[hw_points - 1].green;
	y_b = rgb_resulted[hw_points - 1].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[1].y = y3_max;

	arr_points[1].slope = dal_fixed31_32_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value =
				dal_fixed31_32_from_int(125);

		arr_points[1].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
	}

	lut_params->hw_points_num = hw_points;

	i = 1;
	for (k = 0; k < MAX_REGIONS_NUMBER && i < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1) {
			lut_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			lut_params->arr_curve_points[i].offset =
					lut_params->arr_curve_points[k].offset + (1 << seg_distr[k]);
		}
		i++;
	}

	if (seg_distr[k] != -1)
		lut_params->arr_curve_points[k].segments_num = seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;
	while (i != hw_points + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red   = dal_fixed31_32_sub(rgb_plus_1->red,   rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(rgb_plus_1->green, rgb->green);
		rgb->delta_blue  = dal_fixed31_32_sub(rgb_plus_1->blue,  rgb->blue);

		if (fixpoint == true) {
			rgb->delta_red_reg   = dal_fixed31_32_clamp_u0d10(rgb->delta_red);
			rgb->delta_green_reg = dal_fixed31_32_clamp_u0d10(rgb->delta_green);
			rgb->delta_blue_reg  = dal_fixed31_32_clamp_u0d10(rgb->delta_blue);
			rgb->red_reg         = dal_fixed31_32_clamp_u0d14(rgb->red);
			rgb->green_reg       = dal_fixed31_32_clamp_u0d14(rgb->green);
			rgb->blue_reg        = dal_fixed31_32_clamp_u0d14(rgb->blue);
		}

		++rgb_plus_1;
		++rgb;
		++i;
	}
	cm_helper_convert_to_custom_float(rgb_resulted,
						lut_params->arr_points,
						hw_points, fixpoint);

	return true;
}
