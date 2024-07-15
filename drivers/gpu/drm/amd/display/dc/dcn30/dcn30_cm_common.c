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
#include "dcn30_cm_common.h"
#include "custom_float.h"

#define REG(reg) reg

#define CTX \
	ctx //dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	reg->shifts.field_name, reg->masks.field_name

void cm_helper_program_gamcor_xfer_func(
		struct dc_context *ctx,
		const struct pwl_params *params,
		const struct dcn3_xfer_func_reg *reg)
{
	uint32_t reg_region_cur;
	unsigned int i = 0;

	REG_SET_2(reg->start_cntl_b, 0,
		exp_region_start, params->corner_points[0].blue.custom_float_x,
		exp_resion_start_segment, 0);
	REG_SET_2(reg->start_cntl_g, 0,
		exp_region_start, params->corner_points[0].green.custom_float_x,
		exp_resion_start_segment, 0);
	REG_SET_2(reg->start_cntl_r, 0,
		exp_region_start, params->corner_points[0].red.custom_float_x,
		exp_resion_start_segment, 0);

	REG_SET(reg->start_slope_cntl_b, 0, //linear slope at start of curve
		field_region_linear_slope, params->corner_points[0].blue.custom_float_slope);
	REG_SET(reg->start_slope_cntl_g, 0,
		field_region_linear_slope, params->corner_points[0].green.custom_float_slope);
	REG_SET(reg->start_slope_cntl_r, 0,
		field_region_linear_slope, params->corner_points[0].red.custom_float_slope);

	REG_SET(reg->start_end_cntl1_b, 0,
		field_region_end_base, params->corner_points[1].blue.custom_float_y);
	REG_SET(reg->start_end_cntl1_g, 0,
		field_region_end_base, params->corner_points[1].green.custom_float_y);
	REG_SET(reg->start_end_cntl1_r, 0,
		field_region_end_base, params->corner_points[1].red.custom_float_y);

	REG_SET_2(reg->start_end_cntl2_b, 0,
		field_region_end_slope, params->corner_points[1].blue.custom_float_slope,
		field_region_end, params->corner_points[1].blue.custom_float_x);
	REG_SET_2(reg->start_end_cntl2_g, 0,
		field_region_end_slope, params->corner_points[1].green.custom_float_slope,
		field_region_end, params->corner_points[1].green.custom_float_x);
	REG_SET_2(reg->start_end_cntl2_r, 0,
		field_region_end_slope, params->corner_points[1].red.custom_float_slope,
		field_region_end, params->corner_points[1].red.custom_float_x);

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

/* driver uses 32 regions or less, but DCN HW has 34, extra 2 are set to 0 */
#define MAX_REGIONS_NUMBER 34
#define MAX_LOW_POINT      25
#define NUMBER_REGIONS     32
#define NUMBER_SW_SEGMENTS 16

bool cm3_helper_translate_curve_to_hw_format(
				const struct dc_transfer_func *output_tf,
				struct pwl_params *lut_params, bool fixpoint)
{
	struct curve_points3 *corner_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;
	struct pwl_result_data *rgb_minus_1;

	int32_t region_start, region_end;
	int32_t i;
	uint32_t j, k, seg_distr[MAX_REGIONS_NUMBER], increment, start_index, hw_points;

	if (output_tf == NULL || lut_params == NULL || output_tf->type == TF_TYPE_BYPASS)
		return false;

	corner_points = lut_params->corner_points;
	rgb_resulted = lut_params->rgb_resulted;
	hw_points = 0;

	memset(lut_params, 0, sizeof(struct pwl_params));
	memset(seg_distr, 0, sizeof(seg_distr));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ || output_tf->tf == TRANSFER_FUNCTION_GAMMA22 ||
		output_tf->tf == TRANSFER_FUNCTION_HLG) {
		/* 32 segments
		 * segments are from 2^-25 to 2^7
		 */
		for (i = 0; i < NUMBER_REGIONS ; i++)
			seg_distr[i] = 3;

		region_start = -MAX_LOW_POINT;
		region_end   = NUMBER_REGIONS - MAX_LOW_POINT;
	} else {
		/* 11 segments
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
		seg_distr[8] = 4;
		seg_distr[9] = 4;
		seg_distr[10] = 1;

		region_start = -10;
		region_end = 1;
	}

	for (i = region_end - region_start; i < MAX_REGIONS_NUMBER ; i++)
		seg_distr[i] = -1;

	for (k = 0; k < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (region_end - region_start); k++) {
		increment = NUMBER_SW_SEGMENTS / (1 << seg_distr[k]);
		start_index = (region_start + k + MAX_LOW_POINT) *
				NUMBER_SW_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SW_SEGMENTS;
				i += increment) {
			if (j == hw_points)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (region_end + MAX_LOW_POINT) * NUMBER_SW_SEGMENTS;
	rgb_resulted[hw_points].red = output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points].green = output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points].blue = output_tf->tf_pts.blue[start_index];

	rgb_resulted[hw_points+1].red = rgb_resulted[hw_points].red;
	rgb_resulted[hw_points+1].green = rgb_resulted[hw_points].green;
	rgb_resulted[hw_points+1].blue = rgb_resulted[hw_points].blue;

	// All 3 color channels have same x
	corner_points[0].red.x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_start));
	corner_points[0].green.x = corner_points[0].red.x;
	corner_points[0].blue.x = corner_points[0].red.x;

	corner_points[1].red.x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_end));
	corner_points[1].green.x = corner_points[1].red.x;
	corner_points[1].blue.x = corner_points[1].red.x;

	corner_points[0].red.y = rgb_resulted[0].red;
	corner_points[0].green.y = rgb_resulted[0].green;
	corner_points[0].blue.y = rgb_resulted[0].blue;

	corner_points[0].red.slope = dc_fixpt_div(corner_points[0].red.y,
			corner_points[0].red.x);
	corner_points[0].green.slope = dc_fixpt_div(corner_points[0].green.y,
			corner_points[0].green.x);
	corner_points[0].blue.slope = dc_fixpt_div(corner_points[0].blue.y,
			corner_points[0].blue.x);

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	corner_points[1].red.y = rgb_resulted[hw_points].red;
	corner_points[1].green.y = rgb_resulted[hw_points].green;
	corner_points[1].blue.y = rgb_resulted[hw_points].blue;
	corner_points[1].red.slope = dc_fixpt_zero;
	corner_points[1].green.slope = dc_fixpt_zero;
	corner_points[1].blue.slope = dc_fixpt_zero;

	// DCN3+ have 257 pts in lieu of no separate slope registers
	// Prior HW had 256 base+slope pairs
	lut_params->hw_points_num = hw_points + 1;

	k = 0;
	for (i = 1; i < MAX_REGIONS_NUMBER; i++) {
		if (seg_distr[k] != -1) {
			lut_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			lut_params->arr_curve_points[i].offset =
					lut_params->arr_curve_points[k].offset + (1 << seg_distr[k]);
		}
		k++;
	}

	if (seg_distr[k] != -1)
		lut_params->arr_curve_points[k].segments_num = seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;
	rgb_minus_1 = rgb;

	if (fixpoint == true) {
		i = 1;
		while (i != hw_points + 2) {
			if (i >= hw_points) {
				if (dc_fixpt_lt(rgb_plus_1->red, rgb->red))
					rgb_plus_1->red = dc_fixpt_add(rgb->red,
							rgb_minus_1->delta_red);
				if (dc_fixpt_lt(rgb_plus_1->green, rgb->green))
					rgb_plus_1->green = dc_fixpt_add(rgb->green,
							rgb_minus_1->delta_green);
				if (dc_fixpt_lt(rgb_plus_1->blue, rgb->blue))
					rgb_plus_1->blue = dc_fixpt_add(rgb->blue,
							rgb_minus_1->delta_blue);
			}

			rgb->delta_red_reg   = dc_fixpt_clamp_u0d10(rgb->delta_red);
			rgb->delta_green_reg = dc_fixpt_clamp_u0d10(rgb->delta_green);
			rgb->delta_blue_reg  = dc_fixpt_clamp_u0d10(rgb->delta_blue);
			rgb->red_reg         = dc_fixpt_clamp_u0d14(rgb->red);
			rgb->green_reg       = dc_fixpt_clamp_u0d14(rgb->green);
			rgb->blue_reg        = dc_fixpt_clamp_u0d14(rgb->blue);

			++rgb_plus_1;
			rgb_minus_1 = rgb;
			++rgb;
			++i;
		}
	}
	cm3_helper_convert_to_custom_float(rgb_resulted,
						lut_params->corner_points,
						hw_points+1, fixpoint);

	return true;
}

#define NUM_DEGAMMA_REGIONS    12


bool cm3_helper_translate_curve_to_degamma_hw_format(
				const struct dc_transfer_func *output_tf,
				struct pwl_params *lut_params)
{
	struct curve_points3 *corner_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;

	int32_t region_start, region_end;
	int32_t i;
	uint32_t j, k, seg_distr[MAX_REGIONS_NUMBER], increment, start_index, hw_points;

	if (output_tf == NULL || lut_params == NULL || output_tf->type == TF_TYPE_BYPASS)
		return false;

	corner_points = lut_params->corner_points;
	rgb_resulted = lut_params->rgb_resulted;
	hw_points = 0;

	memset(lut_params, 0, sizeof(struct pwl_params));
	memset(seg_distr, 0, sizeof(seg_distr));

	region_start = -NUM_DEGAMMA_REGIONS;
	region_end   = 0;


	for (i = region_end - region_start; i < MAX_REGIONS_NUMBER ; i++)
		seg_distr[i] = -1;
	/* 12 segments
	 * segments are from 2^-12 to 0
	 */
	for (i = 0; i < NUM_DEGAMMA_REGIONS ; i++)
		seg_distr[i] = 4;

	for (k = 0; k < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (region_end - region_start); k++) {
		increment = NUMBER_SW_SEGMENTS / (1 << seg_distr[k]);
		start_index = (region_start + k + MAX_LOW_POINT) *
				NUMBER_SW_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SW_SEGMENTS;
				i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (region_end + MAX_LOW_POINT) * NUMBER_SW_SEGMENTS;
	rgb_resulted[hw_points - 1].red = output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green = output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue = output_tf->tf_pts.blue[start_index];

	corner_points[0].red.x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_start));
	corner_points[0].green.x = corner_points[0].red.x;
	corner_points[0].blue.x = corner_points[0].red.x;
	corner_points[1].red.x = dc_fixpt_pow(dc_fixpt_from_int(2),
					     dc_fixpt_from_int(region_end));
	corner_points[1].green.x = corner_points[1].red.x;
	corner_points[1].blue.x = corner_points[1].red.x;

	corner_points[0].red.y = rgb_resulted[0].red;
	corner_points[0].green.y = rgb_resulted[0].green;
	corner_points[0].blue.y = rgb_resulted[0].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	corner_points[1].red.y = rgb_resulted[hw_points - 1].red;
	corner_points[1].green.y = rgb_resulted[hw_points - 1].green;
	corner_points[1].blue.y = rgb_resulted[hw_points - 1].blue;
	corner_points[1].red.slope = dc_fixpt_zero;
	corner_points[1].green.slope = dc_fixpt_zero;
	corner_points[1].blue.slope = dc_fixpt_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value =
				dc_fixpt_from_int(125);

		corner_points[1].red.slope = dc_fixpt_div(
			dc_fixpt_sub(dc_fixpt_one, corner_points[1].red.y),
			dc_fixpt_sub(end_value, corner_points[1].red.x));
		corner_points[1].green.slope = dc_fixpt_div(
			dc_fixpt_sub(dc_fixpt_one, corner_points[1].green.y),
			dc_fixpt_sub(end_value, corner_points[1].green.x));
		corner_points[1].blue.slope = dc_fixpt_div(
			dc_fixpt_sub(dc_fixpt_one, corner_points[1].blue.y),
			dc_fixpt_sub(end_value, corner_points[1].blue.x));
	}

	lut_params->hw_points_num = hw_points;

	k = 0;
	for (i = 1; i < MAX_REGIONS_NUMBER; i++) {
		if (seg_distr[k] != -1) {
			lut_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			lut_params->arr_curve_points[i].offset =
					lut_params->arr_curve_points[k].offset + (1 << seg_distr[k]);
		}
		k++;
	}

	if (seg_distr[k] != -1)
		lut_params->arr_curve_points[k].segments_num = seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;
	while (i != hw_points + 1) {
		if (dc_fixpt_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dc_fixpt_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dc_fixpt_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red   = dc_fixpt_sub(rgb_plus_1->red,   rgb->red);
		rgb->delta_green = dc_fixpt_sub(rgb_plus_1->green, rgb->green);
		rgb->delta_blue  = dc_fixpt_sub(rgb_plus_1->blue,  rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}
	cm3_helper_convert_to_custom_float(rgb_resulted,
						lut_params->corner_points,
						hw_points, false);

	return true;
}

bool cm3_helper_convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points3 *corner_points,
		uint32_t hw_points_num,
		bool fixpoint)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = false;

	/* corner_points[0] - beginning base, slope offset for R,G,B
	 * corner_points[1] - end base, slope offset for R,G,B
	 */
	if (!convert_to_custom_float_format(corner_points[0].red.x, &fmt,
				&corner_points[0].red.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].green.x, &fmt,
				&corner_points[0].green.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].blue.x, &fmt,
				&corner_points[0].blue.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(corner_points[0].red.offset, &fmt,
				&corner_points[0].red.custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].green.offset, &fmt,
				&corner_points[0].green.custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].blue.offset, &fmt,
				&corner_points[0].blue.custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(corner_points[0].red.slope, &fmt,
				&corner_points[0].red.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].green.slope, &fmt,
				&corner_points[0].green.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[0].blue.slope, &fmt,
				&corner_points[0].blue.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (fixpoint == true) {
		corner_points[1].red.custom_float_y =
				dc_fixpt_clamp_u0d14(corner_points[1].red.y);
		corner_points[1].green.custom_float_y =
				dc_fixpt_clamp_u0d14(corner_points[1].green.y);
		corner_points[1].blue.custom_float_y =
				dc_fixpt_clamp_u0d14(corner_points[1].blue.y);
	} else {
		if (!convert_to_custom_float_format(corner_points[1].red.y,
				&fmt, &corner_points[1].red.custom_float_y)) {
			BREAK_TO_DEBUGGER();
			return false;
		}
		if (!convert_to_custom_float_format(corner_points[1].green.y,
				&fmt, &corner_points[1].green.custom_float_y)) {
			BREAK_TO_DEBUGGER();
			return false;
		}
		if (!convert_to_custom_float_format(corner_points[1].blue.y,
				&fmt, &corner_points[1].blue.custom_float_y)) {
			BREAK_TO_DEBUGGER();
			return false;
		}
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(corner_points[1].red.x, &fmt,
				&corner_points[1].red.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[1].green.x, &fmt,
				&corner_points[1].green.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[1].blue.x, &fmt,
				&corner_points[1].blue.custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(corner_points[1].red.slope, &fmt,
				&corner_points[1].red.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[1].green.slope, &fmt,
				&corner_points[1].green.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	if (!convert_to_custom_float_format(corner_points[1].blue.slope, &fmt,
				&corner_points[1].blue.custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (hw_points_num == 0 || rgb_resulted == NULL || fixpoint == true)
		return true;

	fmt.mantissa_bits = 12;

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

		++rgb;
		++i;
	}

	return true;
}

bool is_rgb_equal(const struct pwl_result_data *rgb, uint32_t num)
{
	uint32_t i;
	bool ret = true;

	for (i = 0 ; i < num; i++) {
		if (rgb[i].red_reg != rgb[i].green_reg ||
		rgb[i].blue_reg != rgb[i].red_reg  ||
		rgb[i].blue_reg != rgb[i].green_reg) {
			ret = false;
			break;
		}
	}
	return ret;
}

