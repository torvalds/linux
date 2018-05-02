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
#include "opp.h"
#include "color_gamma.h"


#define NUM_PTS_IN_REGION 16
#define NUM_REGIONS 32
#define MAX_HW_POINTS (NUM_PTS_IN_REGION*NUM_REGIONS)

static struct hw_x_point coordinates_x[MAX_HW_POINTS + 2];

static struct fixed31_32 pq_table[MAX_HW_POINTS + 2];
static struct fixed31_32 de_pq_table[MAX_HW_POINTS + 2];

static bool pq_initialized; /* = false; */
static bool de_pq_initialized; /* = false; */

/* one-time setup of X points */
void setup_x_points_distribution(void)
{
	struct fixed31_32 region_size = dal_fixed31_32_from_int(128);
	int32_t segment;
	uint32_t seg_offset;
	uint32_t index;
	struct fixed31_32 increment;

	coordinates_x[MAX_HW_POINTS].x = region_size;
	coordinates_x[MAX_HW_POINTS + 1].x = region_size;

	for (segment = 6; segment > (6 - NUM_REGIONS); segment--) {
		region_size = dal_fixed31_32_div_int(region_size, 2);
		increment = dal_fixed31_32_div_int(region_size,
						NUM_PTS_IN_REGION);
		seg_offset = (segment + (NUM_REGIONS - 7)) * NUM_PTS_IN_REGION;
		coordinates_x[seg_offset].x = region_size;

		for (index = seg_offset + 1;
				index < seg_offset + NUM_PTS_IN_REGION;
				index++) {
			coordinates_x[index].x = dal_fixed31_32_add
					(coordinates_x[index-1].x, increment);
		}
	}
}

static void compute_pq(struct fixed31_32 in_x, struct fixed31_32 *out_y)
{
	/* consts for PQ gamma formula. */
	const struct fixed31_32 m1 =
		dal_fixed31_32_from_fraction(159301758, 1000000000);
	const struct fixed31_32 m2 =
		dal_fixed31_32_from_fraction(7884375, 100000);
	const struct fixed31_32 c1 =
		dal_fixed31_32_from_fraction(8359375, 10000000);
	const struct fixed31_32 c2 =
		dal_fixed31_32_from_fraction(188515625, 10000000);
	const struct fixed31_32 c3 =
		dal_fixed31_32_from_fraction(186875, 10000);

	struct fixed31_32 l_pow_m1;
	struct fixed31_32 base;

	if (dal_fixed31_32_lt(in_x, dal_fixed31_32_zero))
		in_x = dal_fixed31_32_zero;

	l_pow_m1 = dal_fixed31_32_pow(in_x, m1);
	base = dal_fixed31_32_div(
			dal_fixed31_32_add(c1,
					(dal_fixed31_32_mul(c2, l_pow_m1))),
			dal_fixed31_32_add(dal_fixed31_32_one,
					(dal_fixed31_32_mul(c3, l_pow_m1))));
	*out_y = dal_fixed31_32_pow(base, m2);
}

static void compute_de_pq(struct fixed31_32 in_x, struct fixed31_32 *out_y)
{
	/* consts for dePQ gamma formula. */
	const struct fixed31_32 m1 =
		dal_fixed31_32_from_fraction(159301758, 1000000000);
	const struct fixed31_32 m2 =
		dal_fixed31_32_from_fraction(7884375, 100000);
	const struct fixed31_32 c1 =
		dal_fixed31_32_from_fraction(8359375, 10000000);
	const struct fixed31_32 c2 =
		dal_fixed31_32_from_fraction(188515625, 10000000);
	const struct fixed31_32 c3 =
		dal_fixed31_32_from_fraction(186875, 10000);

	struct fixed31_32 l_pow_m1;
	struct fixed31_32 base, div;


	if (dal_fixed31_32_lt(in_x, dal_fixed31_32_zero))
		in_x = dal_fixed31_32_zero;

	l_pow_m1 = dal_fixed31_32_pow(in_x,
			dal_fixed31_32_div(dal_fixed31_32_one, m2));
	base = dal_fixed31_32_sub(l_pow_m1, c1);

	if (dal_fixed31_32_lt(base, dal_fixed31_32_zero))
		base = dal_fixed31_32_zero;

	div = dal_fixed31_32_sub(c2, dal_fixed31_32_mul(c3, l_pow_m1));

	*out_y = dal_fixed31_32_pow(dal_fixed31_32_div(base, div),
			dal_fixed31_32_div(dal_fixed31_32_one, m1));

}
/* one-time pre-compute PQ values - only for sdr_white_level 80 */
void precompute_pq(void)
{
	int i;
	struct fixed31_32 x;
	const struct hw_x_point *coord_x = coordinates_x + 32;
	struct fixed31_32 scaling_factor =
			dal_fixed31_32_from_fraction(80, 10000);

	/* pow function has problems with arguments too small */
	for (i = 0; i < 32; i++)
		pq_table[i] = dal_fixed31_32_zero;

	for (i = 32; i <= MAX_HW_POINTS; i++) {
		x = dal_fixed31_32_mul(coord_x->x, scaling_factor);
		compute_pq(x, &pq_table[i]);
		++coord_x;
	}
}

/* one-time pre-compute dePQ values - only for max pixel value 125 FP16 */
void precompute_de_pq(void)
{
	int i;
	struct fixed31_32  y;
	uint32_t begin_index, end_index;

	struct fixed31_32 scaling_factor = dal_fixed31_32_from_int(125);

	/* X points is 2^-25 to 2^7
	 * De-gamma X is 2^-12 to 2^0 – we are skipping first -12-(-25) = 13 regions
	 */
	begin_index = 13 * NUM_PTS_IN_REGION;
	end_index = begin_index + 12 * NUM_PTS_IN_REGION;

	for (i = 0; i <= begin_index; i++)
		de_pq_table[i] = dal_fixed31_32_zero;

	for (; i <= end_index; i++) {
		compute_de_pq(coordinates_x[i].x, &y);
		de_pq_table[i] = dal_fixed31_32_mul(y, scaling_factor);
	}

	for (; i <= MAX_HW_POINTS; i++)
		de_pq_table[i] = de_pq_table[i-1];
}
struct dividers {
	struct fixed31_32 divider1;
	struct fixed31_32 divider2;
	struct fixed31_32 divider3;
};

static void build_coefficients(struct gamma_coefficients *coefficients, bool is_2_4)
{
		static const int32_t numerator01[] = { 31308, 180000};
		static const int32_t numerator02[] = { 12920, 4500};
		static const int32_t numerator03[] = { 55, 99};
		static const int32_t numerator04[] = { 55, 99};
		static const int32_t numerator05[] = { 2400, 2200};

		uint32_t i = 0;
		uint32_t index = is_2_4 == true ? 0:1;

	do {
		coefficients->a0[i] = dal_fixed31_32_from_fraction(
			numerator01[index], 10000000);
		coefficients->a1[i] = dal_fixed31_32_from_fraction(
			numerator02[index], 1000);
		coefficients->a2[i] = dal_fixed31_32_from_fraction(
			numerator03[index], 1000);
		coefficients->a3[i] = dal_fixed31_32_from_fraction(
			numerator04[index], 1000);
		coefficients->user_gamma[i] = dal_fixed31_32_from_fraction(
			numerator05[index], 1000);

		++i;
	} while (i != ARRAY_SIZE(coefficients->a0));
}

static struct fixed31_32 translate_from_linear_space(
	struct fixed31_32 arg,
	struct fixed31_32 a0,
	struct fixed31_32 a1,
	struct fixed31_32 a2,
	struct fixed31_32 a3,
	struct fixed31_32 gamma)
{
	const struct fixed31_32 one = dal_fixed31_32_from_int(1);

	if (dal_fixed31_32_lt(one, arg))
		return one;

	if (dal_fixed31_32_le(arg, dal_fixed31_32_neg(a0)))
		return dal_fixed31_32_sub(
			a2,
			dal_fixed31_32_mul(
				dal_fixed31_32_add(
					one,
					a3),
				dal_fixed31_32_pow(
					dal_fixed31_32_neg(arg),
					dal_fixed31_32_recip(gamma))));
	else if (dal_fixed31_32_le(a0, arg))
		return dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				dal_fixed31_32_add(
					one,
					a3),
				dal_fixed31_32_pow(
					arg,
					dal_fixed31_32_recip(gamma))),
			a2);
	else
		return dal_fixed31_32_mul(
			arg,
			a1);
}

static struct fixed31_32 translate_to_linear_space(
	struct fixed31_32 arg,
	struct fixed31_32 a0,
	struct fixed31_32 a1,
	struct fixed31_32 a2,
	struct fixed31_32 a3,
	struct fixed31_32 gamma)
{
	struct fixed31_32 linear;

	a0 = dal_fixed31_32_mul(a0, a1);
	if (dal_fixed31_32_le(arg, dal_fixed31_32_neg(a0)))

		linear = dal_fixed31_32_neg(
				 dal_fixed31_32_pow(
				 dal_fixed31_32_div(
				 dal_fixed31_32_sub(a2, arg),
				 dal_fixed31_32_add(
				 dal_fixed31_32_one, a3)), gamma));

	else if (dal_fixed31_32_le(dal_fixed31_32_neg(a0), arg) &&
			 dal_fixed31_32_le(arg, a0))
		linear = dal_fixed31_32_div(arg, a1);
	else
		linear =  dal_fixed31_32_pow(
					dal_fixed31_32_div(
					dal_fixed31_32_add(a2, arg),
					dal_fixed31_32_add(
					dal_fixed31_32_one, a3)), gamma);

	return linear;
}

static inline struct fixed31_32 translate_from_linear_space_ex(
	struct fixed31_32 arg,
	struct gamma_coefficients *coeff,
	uint32_t color_index)
{
	return translate_from_linear_space(
		arg,
		coeff->a0[color_index],
		coeff->a1[color_index],
		coeff->a2[color_index],
		coeff->a3[color_index],
		coeff->user_gamma[color_index]);
}


static inline struct fixed31_32 translate_to_linear_space_ex(
	struct fixed31_32 arg,
	struct gamma_coefficients *coeff,
	uint32_t color_index)
{
	return translate_to_linear_space(
		arg,
		coeff->a0[color_index],
		coeff->a1[color_index],
		coeff->a2[color_index],
		coeff->a3[color_index],
		coeff->user_gamma[color_index]);
}


static bool find_software_points(
	const struct dc_gamma *ramp,
	const struct gamma_pixel *axis_x,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number = ramp->num_entries + 3;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = axis_x[i].r;

			if (i < max_number - 1)
				right = axis_x[i + 1].r;
			else
				right = axis_x[max_number - 1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = axis_x[i].g;

			if (i < max_number - 1)
				right = axis_x[i + 1].g;
			else
				right = axis_x[max_number - 1].g;
		} else {
			left = axis_x[i].b;

			if (i < max_number - 1)
				right = axis_x[i + 1].b;
			else
				right = axis_x[max_number - 1].b;
		}

		if (dal_fixed31_32_le(left, hw_point) &&
			dal_fixed31_32_le(hw_point, right)) {
			*index_to_start = i;
			*index_left = i;

			if (i < max_number - 1)
				*index_right = i + 1;
			else
				*index_right = max_number - 1;

			*pos = HW_POINT_POSITION_MIDDLE;

			return true;
		} else if ((i == *index_to_start) &&
			dal_fixed31_32_le(hw_point, left)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_LEFT;

			return true;
		} else if ((i == max_number - 1) &&
			dal_fixed31_32_le(right, hw_point)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_RIGHT;

			return true;
		}

		++i;
	}

	return false;
}

static bool build_custom_gamma_mapping_coefficients_worker(
	const struct dc_gamma *ramp,
	struct pixel_gamma_point *coeff,
	const struct hw_x_point *coordinates_x,
	const struct gamma_pixel *axis_x,
	enum channel_name channel,
	uint32_t number_of_points)
{
	uint32_t i = 0;

	while (i <= number_of_points) {
		struct fixed31_32 coord_x;

		uint32_t index_to_start = 0;
		uint32_t index_left = 0;
		uint32_t index_right = 0;

		enum hw_point_position hw_pos;

		struct gamma_point *point;

		struct fixed31_32 left_pos;
		struct fixed31_32 right_pos;

		if (channel == CHANNEL_NAME_RED)
			coord_x = coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = coordinates_x[i].regamma_y_green;
		else
			coord_x = coordinates_x[i].regamma_y_blue;

		if (!find_software_points(
			ramp, axis_x, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= ramp->num_entries + 3) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= ramp->num_entries + 3) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &coeff[i].r;

			left_pos = axis_x[index_left].r;
			right_pos = axis_x[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &coeff[i].g;

			left_pos = axis_x[index_left].g;
			right_pos = axis_x[index_right].g;
		} else {
			point = &coeff[i].b;

			left_pos = axis_x[index_left].b;
			right_pos = axis_x[index_right].b;
		}

		if (hw_pos == HW_POINT_POSITION_MIDDLE)
			point->coeff = dal_fixed31_32_div(
				dal_fixed31_32_sub(
					coord_x,
					left_pos),
				dal_fixed31_32_sub(
					right_pos,
					left_pos));
		else if (hw_pos == HW_POINT_POSITION_LEFT)
			point->coeff = dal_fixed31_32_zero;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = dal_fixed31_32_from_int(2);
		else {
			BREAK_TO_DEBUGGER();
			return false;
		}

		point->left_index = index_left;
		point->right_index = index_right;
		point->pos = hw_pos;

		++i;
	}

	return true;
}

static struct fixed31_32 calculate_mapped_value(
	struct pwl_float_data *rgb,
	const struct pixel_gamma_point *coeff,
	enum channel_name channel,
	uint32_t max_index)
{
	const struct gamma_point *point;

	struct fixed31_32 result;

	if (channel == CHANNEL_NAME_RED)
		point = &coeff->r;
	else if (channel == CHANNEL_NAME_GREEN)
		point = &coeff->g;
	else
		point = &coeff->b;

	if ((point->left_index < 0) || (point->left_index > max_index)) {
		BREAK_TO_DEBUGGER();
		return dal_fixed31_32_zero;
	}

	if ((point->right_index < 0) || (point->right_index > max_index)) {
		BREAK_TO_DEBUGGER();
		return dal_fixed31_32_zero;
	}

	if (point->pos == HW_POINT_POSITION_MIDDLE)
		if (channel == CHANNEL_NAME_RED)
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].r,
						rgb[point->left_index].r)),
				rgb[point->left_index].r);
		else if (channel == CHANNEL_NAME_GREEN)
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].g,
						rgb[point->left_index].g)),
				rgb[point->left_index].g);
		else
			result = dal_fixed31_32_add(
				dal_fixed31_32_mul(
					point->coeff,
					dal_fixed31_32_sub(
						rgb[point->right_index].b,
						rgb[point->left_index].b)),
				rgb[point->left_index].b);
	else if (point->pos == HW_POINT_POSITION_LEFT) {
		BREAK_TO_DEBUGGER();
		result = dal_fixed31_32_zero;
	} else {
		BREAK_TO_DEBUGGER();
		result = dal_fixed31_32_one;
	}

	return result;
}

static void build_pq(struct pwl_float_data_ex *rgb_regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		uint32_t sdr_white_level)
{
	uint32_t i, start_index;

	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;
	struct fixed31_32 x;
	struct fixed31_32 output;
	struct fixed31_32 scaling_factor =
			dal_fixed31_32_from_fraction(sdr_white_level, 10000);

	if (!pq_initialized && sdr_white_level == 80) {
		precompute_pq();
		pq_initialized = true;
	}

	/* TODO: start index is from segment 2^-24, skipping first segment
	 * due to x values too small for power calculations
	 */
	start_index = 32;
	rgb += start_index;
	coord_x += start_index;

	for (i = start_index; i <= hw_points_num; i++) {
		/* Multiply 0.008 as regamma is 0-1 and FP16 input is 0-125.
		 * FP 1.0 = 80nits
		 */
		if (sdr_white_level == 80) {
			output = pq_table[i];
		} else {
			x = dal_fixed31_32_mul(coord_x->x, scaling_factor);
			compute_pq(x, &output);
		}

		/* should really not happen? */
		if (dal_fixed31_32_lt(output, dal_fixed31_32_zero))
			output = dal_fixed31_32_zero;
		else if (dal_fixed31_32_lt(dal_fixed31_32_one, output))
			output = dal_fixed31_32_one;

		rgb->r = output;
		rgb->g = output;
		rgb->b = output;

		++coord_x;
		++rgb;
	}
}

static void build_de_pq(struct pwl_float_data_ex *de_pq,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x)
{
	uint32_t i;
	struct fixed31_32 output;

	struct fixed31_32 scaling_factor = dal_fixed31_32_from_int(125);

	if (!de_pq_initialized) {
		precompute_de_pq();
		de_pq_initialized = true;
	}


	for (i = 0; i <= hw_points_num; i++) {
		output = de_pq_table[i];
		/* should really not happen? */
		if (dal_fixed31_32_lt(output, dal_fixed31_32_zero))
			output = dal_fixed31_32_zero;
		else if (dal_fixed31_32_lt(scaling_factor, output))
			output = scaling_factor;
		de_pq[i].r = output;
		de_pq[i].g = output;
		de_pq[i].b = output;
	}
}

static void build_regamma(struct pwl_float_data_ex *rgb_regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x, bool is_2_4)
{
	uint32_t i;

	struct gamma_coefficients coeff;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;

	build_coefficients(&coeff, is_2_4);

	i = 0;

	while (i != hw_points_num + 1) {
		/*TODO use y vs r,g,b*/
		rgb->r = translate_from_linear_space_ex(
			coord_x->x, &coeff, 0);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
		++i;
	}
}

static void build_degamma(struct pwl_float_data_ex *curve,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x, bool is_2_4)
{
	uint32_t i;
	struct gamma_coefficients coeff;
	uint32_t begin_index, end_index;

	build_coefficients(&coeff, is_2_4);
	i = 0;

	/* X points is 2^-25 to 2^7
	 * De-gamma X is 2^-12 to 2^0 – we are skipping first -12-(-25) = 13 regions
	 */
	begin_index = 13 * NUM_PTS_IN_REGION;
	end_index = begin_index + 12 * NUM_PTS_IN_REGION;

	while (i != begin_index) {
		curve[i].r = dal_fixed31_32_zero;
		curve[i].g = dal_fixed31_32_zero;
		curve[i].b = dal_fixed31_32_zero;
		i++;
	}

	while (i != end_index) {
		curve[i].r = translate_to_linear_space_ex(
				coordinate_x[i].x, &coeff, 0);
		curve[i].g = curve[i].r;
		curve[i].b = curve[i].r;
		i++;
	}
	while (i != hw_points_num + 1) {
		curve[i].r = dal_fixed31_32_one;
		curve[i].g = dal_fixed31_32_one;
		curve[i].b = dal_fixed31_32_one;
		i++;
	}
}

static bool scale_gamma(struct pwl_float_data *pwl_rgb,
		const struct dc_gamma *ramp,
		struct dividers dividers)
{
	const struct fixed31_32 max_driver = dal_fixed31_32_from_int(0xFFFF);
	const struct fixed31_32 max_os = dal_fixed31_32_from_int(0xFF00);
	struct fixed31_32 scaler = max_os;
	uint32_t i;
	struct pwl_float_data *rgb = pwl_rgb;
	struct pwl_float_data *rgb_last = rgb + ramp->num_entries - 1;

	i = 0;

	do {
		if (dal_fixed31_32_lt(max_os, ramp->entries.red[i]) ||
			dal_fixed31_32_lt(max_os, ramp->entries.green[i]) ||
			dal_fixed31_32_lt(max_os, ramp->entries.blue[i])) {
			scaler = max_driver;
			break;
		}
		++i;
	} while (i != ramp->num_entries);

	i = 0;

	do {
		rgb->r = dal_fixed31_32_div(
			ramp->entries.red[i], scaler);
		rgb->g = dal_fixed31_32_div(
			ramp->entries.green[i], scaler);
		rgb->b = dal_fixed31_32_div(
			ramp->entries.blue[i], scaler);

		++rgb;
		++i;
	} while (i != ramp->num_entries);

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			dividers.divider1);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			dividers.divider1);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			dividers.divider1);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			dividers.divider2);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			dividers.divider2);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			dividers.divider2);

	++rgb;

	rgb->r = dal_fixed31_32_mul(rgb_last->r,
			dividers.divider3);
	rgb->g = dal_fixed31_32_mul(rgb_last->g,
			dividers.divider3);
	rgb->b = dal_fixed31_32_mul(rgb_last->b,
			dividers.divider3);

	return true;
}

static bool scale_gamma_dx(struct pwl_float_data *pwl_rgb,
		const struct dc_gamma *ramp,
		struct dividers dividers)
{
	uint32_t i;
	struct fixed31_32 min = dal_fixed31_32_zero;
	struct fixed31_32 max = dal_fixed31_32_one;

	struct fixed31_32 delta = dal_fixed31_32_zero;
	struct fixed31_32 offset = dal_fixed31_32_zero;

	for (i = 0 ; i < ramp->num_entries; i++) {
		if (dal_fixed31_32_lt(ramp->entries.red[i], min))
			min = ramp->entries.red[i];

		if (dal_fixed31_32_lt(ramp->entries.green[i], min))
			min = ramp->entries.green[i];

		if (dal_fixed31_32_lt(ramp->entries.blue[i], min))
			min = ramp->entries.blue[i];

		if (dal_fixed31_32_lt(max, ramp->entries.red[i]))
			max = ramp->entries.red[i];

		if (dal_fixed31_32_lt(max, ramp->entries.green[i]))
			max = ramp->entries.green[i];

		if (dal_fixed31_32_lt(max, ramp->entries.blue[i]))
			max = ramp->entries.blue[i];
	}

	if (dal_fixed31_32_lt(min, dal_fixed31_32_zero))
		delta = dal_fixed31_32_neg(min);

	offset = dal_fixed31_32_add(min, max);

	for (i = 0 ; i < ramp->num_entries; i++) {
		pwl_rgb[i].r = dal_fixed31_32_div(
			dal_fixed31_32_add(
				ramp->entries.red[i], delta), offset);
		pwl_rgb[i].g = dal_fixed31_32_div(
			dal_fixed31_32_add(
				ramp->entries.green[i], delta), offset);
		pwl_rgb[i].b = dal_fixed31_32_div(
			dal_fixed31_32_add(
				ramp->entries.blue[i], delta), offset);

	}

	pwl_rgb[i].r =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].r, 2), pwl_rgb[i-2].r);
	pwl_rgb[i].g =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].g, 2), pwl_rgb[i-2].g);
	pwl_rgb[i].b =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].b, 2), pwl_rgb[i-2].b);
	++i;
	pwl_rgb[i].r =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].r, 2), pwl_rgb[i-2].r);
	pwl_rgb[i].g =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].g, 2), pwl_rgb[i-2].g);
	pwl_rgb[i].b =  dal_fixed31_32_sub(dal_fixed31_32_mul_int(
				pwl_rgb[i-1].b, 2), pwl_rgb[i-2].b);

	return true;
}

/*
 * RS3+ color transform DDI - 1D LUT adjustment is composed with regamma here
 * Input is evenly distributed in the output color space as specified in
 * SetTimings
 *
 * Interpolation details:
 * 1D LUT has 4096 values which give curve correction in 0-1 float range
 * for evenly spaced points in 0-1 range. lut1D[index] gives correction
 * for index/4095.
 * First we find index for which:
 *	index/4095 < regamma_y < (index+1)/4095 =>
 *	index < 4095*regamma_y < index + 1
 * norm_y = 4095*regamma_y, and index is just truncating to nearest integer
 * lut1 = lut1D[index], lut2 = lut1D[index+1]
 *
 *adjustedY is then linearly interpolating regamma Y between lut1 and lut2
 */
static void apply_lut_1d(
		const struct dc_gamma *ramp,
		uint32_t num_hw_points,
		struct dc_transfer_func_distributed_points *tf_pts)
{
	int i = 0;
	int color = 0;
	struct fixed31_32 *regamma_y;
	struct fixed31_32 norm_y;
	struct fixed31_32 lut1;
	struct fixed31_32 lut2;
	const int max_lut_index = 4095;
	const struct fixed31_32 max_lut_index_f =
			dal_fixed31_32_from_int_nonconst(max_lut_index);
	int32_t index = 0, index_next = 0;
	struct fixed31_32 index_f;
	struct fixed31_32 delta_lut;
	struct fixed31_32 delta_index;

	if (ramp->type != GAMMA_CS_TFM_1D)
		return; // this is not expected

	for (i = 0; i < num_hw_points; i++) {
		for (color = 0; color < 3; color++) {
			if (color == 0)
				regamma_y = &tf_pts->red[i];
			else if (color == 1)
				regamma_y = &tf_pts->green[i];
			else
				regamma_y = &tf_pts->blue[i];

			norm_y = dal_fixed31_32_mul(max_lut_index_f,
						   *regamma_y);
			index = dal_fixed31_32_floor(norm_y);
			index_f = dal_fixed31_32_from_int_nonconst(index);

			if (index < 0 || index > max_lut_index)
				continue;

			index_next = (index == max_lut_index) ? index : index+1;

			if (color == 0) {
				lut1 = ramp->entries.red[index];
				lut2 = ramp->entries.red[index_next];
			} else if (color == 1) {
				lut1 = ramp->entries.green[index];
				lut2 = ramp->entries.green[index_next];
			} else {
				lut1 = ramp->entries.blue[index];
				lut2 = ramp->entries.blue[index_next];
			}

			// we have everything now, so interpolate
			delta_lut = dal_fixed31_32_sub(lut2, lut1);
			delta_index = dal_fixed31_32_sub(norm_y, index_f);

			*regamma_y = dal_fixed31_32_add(lut1,
				dal_fixed31_32_mul(delta_index, delta_lut));
		}
	}
}

static void build_evenly_distributed_points(
	struct gamma_pixel *points,
	uint32_t numberof_points,
	struct dividers dividers)
{
	struct gamma_pixel *p = points;
	struct gamma_pixel *p_last = p + numberof_points - 1;

	uint32_t i = 0;

	do {
		struct fixed31_32 value = dal_fixed31_32_from_fraction(i,
			numberof_points - 1);

		p->r = value;
		p->g = value;
		p->b = value;

		++p;
		++i;
	} while (i != numberof_points);

	p->r = dal_fixed31_32_div(p_last->r, dividers.divider1);
	p->g = dal_fixed31_32_div(p_last->g, dividers.divider1);
	p->b = dal_fixed31_32_div(p_last->b, dividers.divider1);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, dividers.divider2);
	p->g = dal_fixed31_32_div(p_last->g, dividers.divider2);
	p->b = dal_fixed31_32_div(p_last->b, dividers.divider2);

	++p;

	p->r = dal_fixed31_32_div(p_last->r, dividers.divider3);
	p->g = dal_fixed31_32_div(p_last->g, dividers.divider3);
	p->b = dal_fixed31_32_div(p_last->b, dividers.divider3);
}

static inline void copy_rgb_regamma_to_coordinates_x(
		struct hw_x_point *coordinates_x,
		uint32_t hw_points_num,
		const struct pwl_float_data_ex *rgb_ex)
{
	struct hw_x_point *coords = coordinates_x;
	uint32_t i = 0;
	const struct pwl_float_data_ex *rgb_regamma = rgb_ex;

	while (i <= hw_points_num) {
		coords->regamma_y_red = rgb_regamma->r;
		coords->regamma_y_green = rgb_regamma->g;
		coords->regamma_y_blue = rgb_regamma->b;

		++coords;
		++rgb_regamma;
		++i;
	}
}

static bool calculate_interpolated_hardware_curve(
	const struct dc_gamma *ramp,
	struct pixel_gamma_point *coeff128,
	struct pwl_float_data *rgb_user,
	const struct hw_x_point *coordinates_x,
	const struct gamma_pixel *axis_x,
	uint32_t number_of_points,
	struct dc_transfer_func_distributed_points *tf_pts)
{

	const struct pixel_gamma_point *coeff = coeff128;
	uint32_t max_entries = 3 - 1;

	uint32_t i = 0;

	for (i = 0; i < 3; i++) {
		if (!build_custom_gamma_mapping_coefficients_worker(
				ramp, coeff128, coordinates_x, axis_x, i,
				number_of_points))
			return false;
	}

	i = 0;
	max_entries += ramp->num_entries;

	/* TODO: float point case */

	while (i <= number_of_points) {
		tf_pts->red[i] = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_RED, max_entries);
		tf_pts->green[i] = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_GREEN, max_entries);
		tf_pts->blue[i] = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_BLUE, max_entries);

		++coeff;
		++i;
	}

	return true;
}

static void build_new_custom_resulted_curve(
	uint32_t hw_points_num,
	struct dc_transfer_func_distributed_points *tf_pts)
{
	uint32_t i;

	i = 0;

	while (i != hw_points_num + 1) {
		tf_pts->red[i] = dal_fixed31_32_clamp(
			tf_pts->red[i], dal_fixed31_32_zero,
			dal_fixed31_32_one);
		tf_pts->green[i] = dal_fixed31_32_clamp(
			tf_pts->green[i], dal_fixed31_32_zero,
			dal_fixed31_32_one);
		tf_pts->blue[i] = dal_fixed31_32_clamp(
			tf_pts->blue[i], dal_fixed31_32_zero,
			dal_fixed31_32_one);

		++i;
	}
}

static bool map_regamma_hw_to_x_user(
	const struct dc_gamma *ramp,
	struct pixel_gamma_point *coeff128,
	struct pwl_float_data *rgb_user,
	struct hw_x_point *coords_x,
	const struct gamma_pixel *axis_x,
	const struct pwl_float_data_ex *rgb_regamma,
	uint32_t hw_points_num,
	struct dc_transfer_func_distributed_points *tf_pts,
	bool mapUserRamp)
{
	/* setup to spare calculated ideal regamma values */

	int i = 0;
	struct hw_x_point *coords = coords_x;
	const struct pwl_float_data_ex *regamma = rgb_regamma;

	if (mapUserRamp) {
		copy_rgb_regamma_to_coordinates_x(coords,
				hw_points_num,
				rgb_regamma);

		calculate_interpolated_hardware_curve(
			ramp, coeff128, rgb_user, coords, axis_x,
			hw_points_num, tf_pts);
	} else {
		/* just copy current rgb_regamma into  tf_pts */
		while (i <= hw_points_num) {
			tf_pts->red[i] = regamma->r;
			tf_pts->green[i] = regamma->g;
			tf_pts->blue[i] = regamma->b;

			++regamma;
			++i;
		}
	}

	build_new_custom_resulted_curve(hw_points_num, tf_pts);

	return true;
}

#define _EXTRA_POINTS 3

bool mod_color_calculate_regamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp)
{
	struct dc_transfer_func_distributed_points *tf_pts = &output_tf->tf_pts;
	struct dividers dividers;

	struct pwl_float_data *rgb_user = NULL;
	struct pwl_float_data_ex *rgb_regamma = NULL;
	struct gamma_pixel *axix_x = NULL;
	struct pixel_gamma_point *coeff = NULL;
	enum dc_transfer_func_predefined tf = TRANSFER_FUNCTION_SRGB;
	bool ret = false;

	if (output_tf->type == TF_TYPE_BYPASS)
		return false;

	/* we can use hardcoded curve for plain SRGB TF */
	if (output_tf->type == TF_TYPE_PREDEFINED &&
			output_tf->tf == TRANSFER_FUNCTION_SRGB &&
			(!mapUserRamp && ramp->type == GAMMA_RGB_256))
		return true;

	output_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	rgb_user = kzalloc(sizeof(*rgb_user) * (ramp->num_entries + _EXTRA_POINTS),
			   GFP_KERNEL);
	if (!rgb_user)
		goto rgb_user_alloc_fail;
	rgb_regamma = kzalloc(sizeof(*rgb_regamma) * (MAX_HW_POINTS + _EXTRA_POINTS),
			GFP_KERNEL);
	if (!rgb_regamma)
		goto rgb_regamma_alloc_fail;
	axix_x = kzalloc(sizeof(*axix_x) * (ramp->num_entries + 3),
			 GFP_KERNEL);
	if (!axix_x)
		goto axix_x_alloc_fail;
	coeff = kzalloc(sizeof(*coeff) * (MAX_HW_POINTS + _EXTRA_POINTS), GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	dividers.divider1 = dal_fixed31_32_from_fraction(3, 2);
	dividers.divider2 = dal_fixed31_32_from_int(2);
	dividers.divider3 = dal_fixed31_32_from_fraction(5, 2);

	tf = output_tf->tf;

	build_evenly_distributed_points(
			axix_x,
			ramp->num_entries,
			dividers);

	if (ramp->type == GAMMA_RGB_256 && mapUserRamp)
		scale_gamma(rgb_user, ramp, dividers);
	else if (ramp->type == GAMMA_RGB_FLOAT_1024)
		scale_gamma_dx(rgb_user, ramp, dividers);

	if (tf == TRANSFER_FUNCTION_PQ) {
		tf_pts->end_exponent = 7;
		tf_pts->x_point_at_y1_red = 125;
		tf_pts->x_point_at_y1_green = 125;
		tf_pts->x_point_at_y1_blue = 125;

		build_pq(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				output_tf->sdr_ref_white_level);
	} else {
		tf_pts->end_exponent = 0;
		tf_pts->x_point_at_y1_red = 1;
		tf_pts->x_point_at_y1_green = 1;
		tf_pts->x_point_at_y1_blue = 1;

		build_regamma(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x, tf == TRANSFER_FUNCTION_SRGB ? true:false);
	}

	map_regamma_hw_to_x_user(ramp, coeff, rgb_user,
			coordinates_x, axix_x, rgb_regamma,
			MAX_HW_POINTS, tf_pts,
			(mapUserRamp || ramp->type != GAMMA_RGB_256) &&
			ramp->type != GAMMA_CS_TFM_1D);

	if (ramp->type == GAMMA_CS_TFM_1D)
		apply_lut_1d(ramp, MAX_HW_POINTS, tf_pts);

	ret = true;

	kfree(coeff);
coeff_alloc_fail:
	kfree(axix_x);
axix_x_alloc_fail:
	kfree(rgb_regamma);
rgb_regamma_alloc_fail:
	kfree(rgb_user);
rgb_user_alloc_fail:
	return ret;
}

bool mod_color_calculate_degamma_params(struct dc_transfer_func *input_tf,
		const struct dc_gamma *ramp, bool mapUserRamp)
{
	struct dc_transfer_func_distributed_points *tf_pts = &input_tf->tf_pts;
	struct dividers dividers;

	struct pwl_float_data *rgb_user = NULL;
	struct pwl_float_data_ex *curve = NULL;
	struct gamma_pixel *axix_x = NULL;
	struct pixel_gamma_point *coeff = NULL;
	enum dc_transfer_func_predefined tf = TRANSFER_FUNCTION_SRGB;
	bool ret = false;

	if (input_tf->type == TF_TYPE_BYPASS)
		return false;

	/* we can use hardcoded curve for plain SRGB TF */
	if (input_tf->type == TF_TYPE_PREDEFINED &&
			input_tf->tf == TRANSFER_FUNCTION_SRGB &&
			(!mapUserRamp && ramp->type == GAMMA_RGB_256))
		return true;

	input_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	rgb_user = kzalloc(sizeof(*rgb_user) * (ramp->num_entries + _EXTRA_POINTS),
			   GFP_KERNEL);
	if (!rgb_user)
		goto rgb_user_alloc_fail;
	curve = kzalloc(sizeof(*curve) * (MAX_HW_POINTS + _EXTRA_POINTS),
			GFP_KERNEL);
	if (!curve)
		goto curve_alloc_fail;
	axix_x = kzalloc(sizeof(*axix_x) * (ramp->num_entries + _EXTRA_POINTS),
			 GFP_KERNEL);
	if (!axix_x)
		goto axix_x_alloc_fail;
	coeff = kzalloc(sizeof(*coeff) * (MAX_HW_POINTS + _EXTRA_POINTS), GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	dividers.divider1 = dal_fixed31_32_from_fraction(3, 2);
	dividers.divider2 = dal_fixed31_32_from_int(2);
	dividers.divider3 = dal_fixed31_32_from_fraction(5, 2);

	tf = input_tf->tf;

	build_evenly_distributed_points(
			axix_x,
			ramp->num_entries,
			dividers);

	if (ramp->type == GAMMA_RGB_256 && mapUserRamp)
		scale_gamma(rgb_user, ramp, dividers);
	else if (ramp->type == GAMMA_RGB_FLOAT_1024)
		scale_gamma_dx(rgb_user, ramp, dividers);

	if (tf == TRANSFER_FUNCTION_PQ)
		build_de_pq(curve,
				MAX_HW_POINTS,
				coordinates_x);
	else
		build_degamma(curve,
				MAX_HW_POINTS,
				coordinates_x,
				tf == TRANSFER_FUNCTION_SRGB ? true:false);

	tf_pts->end_exponent = 0;
	tf_pts->x_point_at_y1_red = 1;
	tf_pts->x_point_at_y1_green = 1;
	tf_pts->x_point_at_y1_blue = 1;

	map_regamma_hw_to_x_user(ramp, coeff, rgb_user,
			coordinates_x, axix_x, curve,
			MAX_HW_POINTS, tf_pts,
			mapUserRamp);

	ret = true;

	kfree(coeff);
coeff_alloc_fail:
	kfree(axix_x);
axix_x_alloc_fail:
	kfree(curve);
curve_alloc_fail:
	kfree(rgb_user);
rgb_user_alloc_fail:

	return ret;

}


bool  mod_color_calculate_curve(enum dc_transfer_func_predefined trans,
				struct dc_transfer_func_distributed_points *points)
{
	uint32_t i;
	bool ret = false;
	struct pwl_float_data_ex *rgb_regamma = NULL;

	if (trans == TRANSFER_FUNCTION_UNITY ||
		trans == TRANSFER_FUNCTION_LINEAR) {
		points->end_exponent = 0;
		points->x_point_at_y1_red = 1;
		points->x_point_at_y1_green = 1;
		points->x_point_at_y1_blue = 1;

		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = coordinates_x[i].x;
			points->green[i]  = coordinates_x[i].x;
			points->blue[i]   = coordinates_x[i].x;
		}
		ret = true;
	} else if (trans == TRANSFER_FUNCTION_PQ) {
		rgb_regamma = kzalloc(sizeof(*rgb_regamma) * (MAX_HW_POINTS +
						_EXTRA_POINTS), GFP_KERNEL);
		if (!rgb_regamma)
			goto rgb_regamma_alloc_fail;
		points->end_exponent = 7;
		points->x_point_at_y1_red = 125;
		points->x_point_at_y1_green = 125;
		points->x_point_at_y1_blue = 125;


		build_pq(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				80);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_regamma[i].r;
			points->green[i]  = rgb_regamma[i].g;
			points->blue[i]   = rgb_regamma[i].b;
		}
		ret = true;

		kfree(rgb_regamma);
	} else if (trans == TRANSFER_FUNCTION_SRGB ||
			  trans == TRANSFER_FUNCTION_BT709) {
		rgb_regamma = kzalloc(sizeof(*rgb_regamma) * (MAX_HW_POINTS +
						_EXTRA_POINTS), GFP_KERNEL);
		if (!rgb_regamma)
			goto rgb_regamma_alloc_fail;
		points->end_exponent = 0;
		points->x_point_at_y1_red = 1;
		points->x_point_at_y1_green = 1;
		points->x_point_at_y1_blue = 1;

		build_regamma(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x, trans == TRANSFER_FUNCTION_SRGB ? true:false);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_regamma[i].r;
			points->green[i]  = rgb_regamma[i].g;
			points->blue[i]   = rgb_regamma[i].b;
		}
		ret = true;

		kfree(rgb_regamma);
	}
rgb_regamma_alloc_fail:
	return ret;
}


bool  mod_color_calculate_degamma_curve(enum dc_transfer_func_predefined trans,
				struct dc_transfer_func_distributed_points *points)
{
	uint32_t i;
	bool ret = false;
	struct pwl_float_data_ex *rgb_degamma = NULL;

	if (trans == TRANSFER_FUNCTION_UNITY ||
		trans == TRANSFER_FUNCTION_LINEAR) {

		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = coordinates_x[i].x;
			points->green[i]  = coordinates_x[i].x;
			points->blue[i]   = coordinates_x[i].x;
		}
		ret = true;
	} else if (trans == TRANSFER_FUNCTION_PQ) {
		rgb_degamma = kzalloc(sizeof(*rgb_degamma) * (MAX_HW_POINTS +
						_EXTRA_POINTS), GFP_KERNEL);
		if (!rgb_degamma)
			goto rgb_degamma_alloc_fail;


		build_de_pq(rgb_degamma,
				MAX_HW_POINTS,
				coordinates_x);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_degamma[i].r;
			points->green[i]  = rgb_degamma[i].g;
			points->blue[i]   = rgb_degamma[i].b;
		}
		ret = true;

		kfree(rgb_degamma);
	} else if (trans == TRANSFER_FUNCTION_SRGB ||
			  trans == TRANSFER_FUNCTION_BT709) {
		rgb_degamma = kzalloc(sizeof(*rgb_degamma) * (MAX_HW_POINTS +
						_EXTRA_POINTS), GFP_KERNEL);
		if (!rgb_degamma)
			goto rgb_degamma_alloc_fail;

		build_degamma(rgb_degamma,
				MAX_HW_POINTS,
				coordinates_x, trans == TRANSFER_FUNCTION_SRGB ? true:false);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_degamma[i].r;
			points->green[i]  = rgb_degamma[i].g;
			points->blue[i]   = rgb_degamma[i].b;
		}
		ret = true;

		kfree(rgb_degamma);
	}
	points->end_exponent = 0;
	points->x_point_at_y1_red = 1;
	points->x_point_at_y1_green = 1;
	points->x_point_at_y1_blue = 1;

rgb_degamma_alloc_fail:
	return ret;
}


