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
	struct fixed31_32 region_size = dc_fixpt_from_int(128);
	int32_t segment;
	uint32_t seg_offset;
	uint32_t index;
	struct fixed31_32 increment;

	coordinates_x[MAX_HW_POINTS].x = region_size;
	coordinates_x[MAX_HW_POINTS + 1].x = region_size;

	for (segment = 6; segment > (6 - NUM_REGIONS); segment--) {
		region_size = dc_fixpt_div_int(region_size, 2);
		increment = dc_fixpt_div_int(region_size,
						NUM_PTS_IN_REGION);
		seg_offset = (segment + (NUM_REGIONS - 7)) * NUM_PTS_IN_REGION;
		coordinates_x[seg_offset].x = region_size;

		for (index = seg_offset + 1;
				index < seg_offset + NUM_PTS_IN_REGION;
				index++) {
			coordinates_x[index].x = dc_fixpt_add
					(coordinates_x[index-1].x, increment);
		}
	}
}

static void compute_pq(struct fixed31_32 in_x, struct fixed31_32 *out_y)
{
	/* consts for PQ gamma formula. */
	const struct fixed31_32 m1 =
		dc_fixpt_from_fraction(159301758, 1000000000);
	const struct fixed31_32 m2 =
		dc_fixpt_from_fraction(7884375, 100000);
	const struct fixed31_32 c1 =
		dc_fixpt_from_fraction(8359375, 10000000);
	const struct fixed31_32 c2 =
		dc_fixpt_from_fraction(188515625, 10000000);
	const struct fixed31_32 c3 =
		dc_fixpt_from_fraction(186875, 10000);

	struct fixed31_32 l_pow_m1;
	struct fixed31_32 base;

	if (dc_fixpt_lt(in_x, dc_fixpt_zero))
		in_x = dc_fixpt_zero;

	l_pow_m1 = dc_fixpt_pow(in_x, m1);
	base = dc_fixpt_div(
			dc_fixpt_add(c1,
					(dc_fixpt_mul(c2, l_pow_m1))),
			dc_fixpt_add(dc_fixpt_one,
					(dc_fixpt_mul(c3, l_pow_m1))));
	*out_y = dc_fixpt_pow(base, m2);
}

static void compute_de_pq(struct fixed31_32 in_x, struct fixed31_32 *out_y)
{
	/* consts for dePQ gamma formula. */
	const struct fixed31_32 m1 =
		dc_fixpt_from_fraction(159301758, 1000000000);
	const struct fixed31_32 m2 =
		dc_fixpt_from_fraction(7884375, 100000);
	const struct fixed31_32 c1 =
		dc_fixpt_from_fraction(8359375, 10000000);
	const struct fixed31_32 c2 =
		dc_fixpt_from_fraction(188515625, 10000000);
	const struct fixed31_32 c3 =
		dc_fixpt_from_fraction(186875, 10000);

	struct fixed31_32 l_pow_m1;
	struct fixed31_32 base, div;


	if (dc_fixpt_lt(in_x, dc_fixpt_zero))
		in_x = dc_fixpt_zero;

	l_pow_m1 = dc_fixpt_pow(in_x,
			dc_fixpt_div(dc_fixpt_one, m2));
	base = dc_fixpt_sub(l_pow_m1, c1);

	if (dc_fixpt_lt(base, dc_fixpt_zero))
		base = dc_fixpt_zero;

	div = dc_fixpt_sub(c2, dc_fixpt_mul(c3, l_pow_m1));

	*out_y = dc_fixpt_pow(dc_fixpt_div(base, div),
			dc_fixpt_div(dc_fixpt_one, m1));

}

/*de gamma, none linear to linear*/
static void compute_hlg_oetf(struct fixed31_32 in_x, bool is_light0_12, struct fixed31_32 *out_y)
{
	struct fixed31_32 a;
	struct fixed31_32 b;
	struct fixed31_32 c;
	struct fixed31_32 threshold;
	struct fixed31_32 reference_white_level;

	a = dc_fixpt_from_fraction(17883277, 100000000);
	if (is_light0_12) {
		/*light 0-12*/
		b = dc_fixpt_from_fraction(28466892, 100000000);
		c = dc_fixpt_from_fraction(55991073, 100000000);
		threshold = dc_fixpt_one;
		reference_white_level = dc_fixpt_half;
	} else {
		/*light 0-1*/
		b = dc_fixpt_from_fraction(2372241, 100000000);
		c = dc_fixpt_add(dc_fixpt_one, dc_fixpt_from_fraction(429347, 100000000));
		threshold = dc_fixpt_from_fraction(1, 12);
		reference_white_level = dc_fixpt_pow(dc_fixpt_from_fraction(3, 1), dc_fixpt_half);
	}
	if (dc_fixpt_lt(threshold, in_x))
		*out_y = dc_fixpt_add(c, dc_fixpt_mul(a, dc_fixpt_log(dc_fixpt_sub(in_x, b))));
	else
		*out_y = dc_fixpt_mul(dc_fixpt_pow(in_x, dc_fixpt_half), reference_white_level);
}

/*re gamma, linear to none linear*/
static void compute_hlg_eotf(struct fixed31_32 in_x, bool is_light0_12, struct fixed31_32 *out_y)
{
	struct fixed31_32 a;
	struct fixed31_32 b;
	struct fixed31_32 c;
	struct fixed31_32 reference_white_level;

	a = dc_fixpt_from_fraction(17883277, 100000000);
	if (is_light0_12) {
		/*light 0-12*/
		b = dc_fixpt_from_fraction(28466892, 100000000);
		c = dc_fixpt_from_fraction(55991073, 100000000);
		reference_white_level = dc_fixpt_from_fraction(4, 1);
	} else {
		/*light 0-1*/
		b = dc_fixpt_from_fraction(2372241, 100000000);
		c = dc_fixpt_add(dc_fixpt_one, dc_fixpt_from_fraction(429347, 100000000));
		reference_white_level = dc_fixpt_from_fraction(1, 3);
	}
	if (dc_fixpt_lt(dc_fixpt_half, in_x))
		*out_y = dc_fixpt_add(dc_fixpt_exp(dc_fixpt_div(dc_fixpt_sub(in_x, c), a)), b);
	else
		*out_y = dc_fixpt_mul(dc_fixpt_pow(in_x, dc_fixpt_from_fraction(2, 1)), reference_white_level);
}


/* one-time pre-compute PQ values - only for sdr_white_level 80 */
void precompute_pq(void)
{
	int i;
	struct fixed31_32 x;
	const struct hw_x_point *coord_x = coordinates_x + 32;
	struct fixed31_32 scaling_factor =
			dc_fixpt_from_fraction(80, 10000);

	/* pow function has problems with arguments too small */
	for (i = 0; i < 32; i++)
		pq_table[i] = dc_fixpt_zero;

	for (i = 32; i <= MAX_HW_POINTS; i++) {
		x = dc_fixpt_mul(coord_x->x, scaling_factor);
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

	struct fixed31_32 scaling_factor = dc_fixpt_from_int(125);

	/* X points is 2^-25 to 2^7
	 * De-gamma X is 2^-12 to 2^0 – we are skipping first -12-(-25) = 13 regions
	 */
	begin_index = 13 * NUM_PTS_IN_REGION;
	end_index = begin_index + 12 * NUM_PTS_IN_REGION;

	for (i = 0; i <= begin_index; i++)
		de_pq_table[i] = dc_fixpt_zero;

	for (; i <= end_index; i++) {
		compute_de_pq(coordinates_x[i].x, &y);
		de_pq_table[i] = dc_fixpt_mul(y, scaling_factor);
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
		coefficients->a0[i] = dc_fixpt_from_fraction(
			numerator01[index], 10000000);
		coefficients->a1[i] = dc_fixpt_from_fraction(
			numerator02[index], 1000);
		coefficients->a2[i] = dc_fixpt_from_fraction(
			numerator03[index], 1000);
		coefficients->a3[i] = dc_fixpt_from_fraction(
			numerator04[index], 1000);
		coefficients->user_gamma[i] = dc_fixpt_from_fraction(
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
	const struct fixed31_32 one = dc_fixpt_from_int(1);

	if (dc_fixpt_lt(one, arg))
		return one;

	if (dc_fixpt_le(arg, dc_fixpt_neg(a0)))
		return dc_fixpt_sub(
			a2,
			dc_fixpt_mul(
				dc_fixpt_add(
					one,
					a3),
				dc_fixpt_pow(
					dc_fixpt_neg(arg),
					dc_fixpt_recip(gamma))));
	else if (dc_fixpt_le(a0, arg))
		return dc_fixpt_sub(
			dc_fixpt_mul(
				dc_fixpt_add(
					one,
					a3),
				dc_fixpt_pow(
					arg,
					dc_fixpt_recip(gamma))),
			a2);
	else
		return dc_fixpt_mul(
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

	a0 = dc_fixpt_mul(a0, a1);
	if (dc_fixpt_le(arg, dc_fixpt_neg(a0)))

		linear = dc_fixpt_neg(
				 dc_fixpt_pow(
				 dc_fixpt_div(
				 dc_fixpt_sub(a2, arg),
				 dc_fixpt_add(
				 dc_fixpt_one, a3)), gamma));

	else if (dc_fixpt_le(dc_fixpt_neg(a0), arg) &&
			 dc_fixpt_le(arg, a0))
		linear = dc_fixpt_div(arg, a1);
	else
		linear =  dc_fixpt_pow(
					dc_fixpt_div(
					dc_fixpt_add(a2, arg),
					dc_fixpt_add(
					dc_fixpt_one, a3)), gamma);

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

		if (dc_fixpt_le(left, hw_point) &&
			dc_fixpt_le(hw_point, right)) {
			*index_to_start = i;
			*index_left = i;

			if (i < max_number - 1)
				*index_right = i + 1;
			else
				*index_right = max_number - 1;

			*pos = HW_POINT_POSITION_MIDDLE;

			return true;
		} else if ((i == *index_to_start) &&
			dc_fixpt_le(hw_point, left)) {
			*index_to_start = i;
			*index_left = i;
			*index_right = i;

			*pos = HW_POINT_POSITION_LEFT;

			return true;
		} else if ((i == max_number - 1) &&
			dc_fixpt_le(right, hw_point)) {
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
			point->coeff = dc_fixpt_div(
				dc_fixpt_sub(
					coord_x,
					left_pos),
				dc_fixpt_sub(
					right_pos,
					left_pos));
		else if (hw_pos == HW_POINT_POSITION_LEFT)
			point->coeff = dc_fixpt_zero;
		else if (hw_pos == HW_POINT_POSITION_RIGHT)
			point->coeff = dc_fixpt_from_int(2);
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
		return dc_fixpt_zero;
	}

	if ((point->right_index < 0) || (point->right_index > max_index)) {
		BREAK_TO_DEBUGGER();
		return dc_fixpt_zero;
	}

	if (point->pos == HW_POINT_POSITION_MIDDLE)
		if (channel == CHANNEL_NAME_RED)
			result = dc_fixpt_add(
				dc_fixpt_mul(
					point->coeff,
					dc_fixpt_sub(
						rgb[point->right_index].r,
						rgb[point->left_index].r)),
				rgb[point->left_index].r);
		else if (channel == CHANNEL_NAME_GREEN)
			result = dc_fixpt_add(
				dc_fixpt_mul(
					point->coeff,
					dc_fixpt_sub(
						rgb[point->right_index].g,
						rgb[point->left_index].g)),
				rgb[point->left_index].g);
		else
			result = dc_fixpt_add(
				dc_fixpt_mul(
					point->coeff,
					dc_fixpt_sub(
						rgb[point->right_index].b,
						rgb[point->left_index].b)),
				rgb[point->left_index].b);
	else if (point->pos == HW_POINT_POSITION_LEFT) {
		BREAK_TO_DEBUGGER();
		result = dc_fixpt_zero;
	} else {
		BREAK_TO_DEBUGGER();
		result = dc_fixpt_one;
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
			dc_fixpt_from_fraction(sdr_white_level, 10000);

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
			x = dc_fixpt_mul(coord_x->x, scaling_factor);
			compute_pq(x, &output);
		}

		/* should really not happen? */
		if (dc_fixpt_lt(output, dc_fixpt_zero))
			output = dc_fixpt_zero;
		else if (dc_fixpt_lt(dc_fixpt_one, output))
			output = dc_fixpt_one;

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

	struct fixed31_32 scaling_factor = dc_fixpt_from_int(125);

	if (!de_pq_initialized) {
		precompute_de_pq();
		de_pq_initialized = true;
	}


	for (i = 0; i <= hw_points_num; i++) {
		output = de_pq_table[i];
		/* should really not happen? */
		if (dc_fixpt_lt(output, dc_fixpt_zero))
			output = dc_fixpt_zero;
		else if (dc_fixpt_lt(scaling_factor, output))
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
		curve[i].r = dc_fixpt_zero;
		curve[i].g = dc_fixpt_zero;
		curve[i].b = dc_fixpt_zero;
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
		curve[i].r = dc_fixpt_one;
		curve[i].g = dc_fixpt_one;
		curve[i].b = dc_fixpt_one;
		i++;
	}
}

static void build_hlg_degamma(struct pwl_float_data_ex *degamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x, bool is_light0_12)
{
	uint32_t i;

	struct pwl_float_data_ex *rgb = degamma;
	const struct hw_x_point *coord_x = coordinate_x;

	i = 0;

	while (i != hw_points_num + 1) {
		compute_hlg_oetf(coord_x->x, is_light0_12, &rgb->r);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
		++i;
	}
}

static void build_hlg_regamma(struct pwl_float_data_ex *regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x, bool is_light0_12)
{
	uint32_t i;

	struct pwl_float_data_ex *rgb = regamma;
	const struct hw_x_point *coord_x = coordinate_x;

	i = 0;

	while (i != hw_points_num + 1) {
		compute_hlg_eotf(coord_x->x, is_light0_12, &rgb->r);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
		++i;
	}
}

static void scale_gamma(struct pwl_float_data *pwl_rgb,
		const struct dc_gamma *ramp,
		struct dividers dividers)
{
	const struct fixed31_32 max_driver = dc_fixpt_from_int(0xFFFF);
	const struct fixed31_32 max_os = dc_fixpt_from_int(0xFF00);
	struct fixed31_32 scaler = max_os;
	uint32_t i;
	struct pwl_float_data *rgb = pwl_rgb;
	struct pwl_float_data *rgb_last = rgb + ramp->num_entries - 1;

	i = 0;

	do {
		if (dc_fixpt_lt(max_os, ramp->entries.red[i]) ||
			dc_fixpt_lt(max_os, ramp->entries.green[i]) ||
			dc_fixpt_lt(max_os, ramp->entries.blue[i])) {
			scaler = max_driver;
			break;
		}
		++i;
	} while (i != ramp->num_entries);

	i = 0;

	do {
		rgb->r = dc_fixpt_div(
			ramp->entries.red[i], scaler);
		rgb->g = dc_fixpt_div(
			ramp->entries.green[i], scaler);
		rgb->b = dc_fixpt_div(
			ramp->entries.blue[i], scaler);

		++rgb;
		++i;
	} while (i != ramp->num_entries);

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider1);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider1);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider1);

	++rgb;

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider2);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider2);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider2);

	++rgb;

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider3);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider3);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider3);
}

static void scale_gamma_dx(struct pwl_float_data *pwl_rgb,
		const struct dc_gamma *ramp,
		struct dividers dividers)
{
	uint32_t i;
	struct fixed31_32 min = dc_fixpt_zero;
	struct fixed31_32 max = dc_fixpt_one;

	struct fixed31_32 delta = dc_fixpt_zero;
	struct fixed31_32 offset = dc_fixpt_zero;

	for (i = 0 ; i < ramp->num_entries; i++) {
		if (dc_fixpt_lt(ramp->entries.red[i], min))
			min = ramp->entries.red[i];

		if (dc_fixpt_lt(ramp->entries.green[i], min))
			min = ramp->entries.green[i];

		if (dc_fixpt_lt(ramp->entries.blue[i], min))
			min = ramp->entries.blue[i];

		if (dc_fixpt_lt(max, ramp->entries.red[i]))
			max = ramp->entries.red[i];

		if (dc_fixpt_lt(max, ramp->entries.green[i]))
			max = ramp->entries.green[i];

		if (dc_fixpt_lt(max, ramp->entries.blue[i]))
			max = ramp->entries.blue[i];
	}

	if (dc_fixpt_lt(min, dc_fixpt_zero))
		delta = dc_fixpt_neg(min);

	offset = dc_fixpt_add(min, max);

	for (i = 0 ; i < ramp->num_entries; i++) {
		pwl_rgb[i].r = dc_fixpt_div(
			dc_fixpt_add(
				ramp->entries.red[i], delta), offset);
		pwl_rgb[i].g = dc_fixpt_div(
			dc_fixpt_add(
				ramp->entries.green[i], delta), offset);
		pwl_rgb[i].b = dc_fixpt_div(
			dc_fixpt_add(
				ramp->entries.blue[i], delta), offset);

	}

	pwl_rgb[i].r =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].r, 2), pwl_rgb[i-2].r);
	pwl_rgb[i].g =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].g, 2), pwl_rgb[i-2].g);
	pwl_rgb[i].b =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].b, 2), pwl_rgb[i-2].b);
	++i;
	pwl_rgb[i].r =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].r, 2), pwl_rgb[i-2].r);
	pwl_rgb[i].g =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].g, 2), pwl_rgb[i-2].g);
	pwl_rgb[i].b =  dc_fixpt_sub(dc_fixpt_mul_int(
				pwl_rgb[i-1].b, 2), pwl_rgb[i-2].b);
}

/* todo: all these scale_gamma functions are inherently the same but
 *  take different structures as params or different format for ramp
 *  values. We could probably implement it in a more generic fashion
 */
static void scale_user_regamma_ramp(struct pwl_float_data *pwl_rgb,
		const struct regamma_ramp *ramp,
		struct dividers dividers)
{
	unsigned short max_driver = 0xFFFF;
	unsigned short max_os = 0xFF00;
	unsigned short scaler = max_os;
	uint32_t i;
	struct pwl_float_data *rgb = pwl_rgb;
	struct pwl_float_data *rgb_last = rgb + GAMMA_RGB_256_ENTRIES - 1;

	i = 0;
	do {
		if (ramp->gamma[i] > max_os ||
				ramp->gamma[i + 256] > max_os ||
				ramp->gamma[i + 512] > max_os) {
			scaler = max_driver;
			break;
		}
		i++;
	} while (i != GAMMA_RGB_256_ENTRIES);

	i = 0;
	do {
		rgb->r = dc_fixpt_from_fraction(
				ramp->gamma[i], scaler);
		rgb->g = dc_fixpt_from_fraction(
				ramp->gamma[i + 256], scaler);
		rgb->b = dc_fixpt_from_fraction(
				ramp->gamma[i + 512], scaler);

		++rgb;
		++i;
	} while (i != GAMMA_RGB_256_ENTRIES);

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider1);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider1);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider1);

	++rgb;

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider2);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider2);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider2);

	++rgb;

	rgb->r = dc_fixpt_mul(rgb_last->r,
			dividers.divider3);
	rgb->g = dc_fixpt_mul(rgb_last->g,
			dividers.divider3);
	rgb->b = dc_fixpt_mul(rgb_last->b,
			dividers.divider3);
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
 * adjustedY is then linearly interpolating regamma Y between lut1 and lut2
 *
 * Custom degamma on Linux uses the same interpolation math, so is handled here
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
			dc_fixpt_from_int(max_lut_index);
	int32_t index = 0, index_next = 0;
	struct fixed31_32 index_f;
	struct fixed31_32 delta_lut;
	struct fixed31_32 delta_index;

	if (ramp->type != GAMMA_CS_TFM_1D && ramp->type != GAMMA_CUSTOM)
		return; // this is not expected

	for (i = 0; i < num_hw_points; i++) {
		for (color = 0; color < 3; color++) {
			if (color == 0)
				regamma_y = &tf_pts->red[i];
			else if (color == 1)
				regamma_y = &tf_pts->green[i];
			else
				regamma_y = &tf_pts->blue[i];

			norm_y = dc_fixpt_mul(max_lut_index_f,
						   *regamma_y);
			index = dc_fixpt_floor(norm_y);
			index_f = dc_fixpt_from_int(index);

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
			delta_lut = dc_fixpt_sub(lut2, lut1);
			delta_index = dc_fixpt_sub(norm_y, index_f);

			*regamma_y = dc_fixpt_add(lut1,
				dc_fixpt_mul(delta_index, delta_lut));
		}
	}
}

static void build_evenly_distributed_points(
	struct gamma_pixel *points,
	uint32_t numberof_points,
	struct dividers dividers)
{
	struct gamma_pixel *p = points;
	struct gamma_pixel *p_last;

	uint32_t i = 0;

	// This function should not gets called with 0 as a parameter
	ASSERT(numberof_points > 0);
	p_last = p + numberof_points - 1;

	do {
		struct fixed31_32 value = dc_fixpt_from_fraction(i,
			numberof_points - 1);

		p->r = value;
		p->g = value;
		p->b = value;

		++p;
		++i;
	} while (i < numberof_points);

	p->r = dc_fixpt_div(p_last->r, dividers.divider1);
	p->g = dc_fixpt_div(p_last->g, dividers.divider1);
	p->b = dc_fixpt_div(p_last->b, dividers.divider1);

	++p;

	p->r = dc_fixpt_div(p_last->r, dividers.divider2);
	p->g = dc_fixpt_div(p_last->g, dividers.divider2);
	p->b = dc_fixpt_div(p_last->b, dividers.divider2);

	++p;

	p->r = dc_fixpt_div(p_last->r, dividers.divider3);
	p->g = dc_fixpt_div(p_last->g, dividers.divider3);
	p->b = dc_fixpt_div(p_last->b, dividers.divider3);
}

static inline void copy_rgb_regamma_to_coordinates_x(
		struct hw_x_point *coordinates_x,
		uint32_t hw_points_num,
		const struct pwl_float_data_ex *rgb_ex)
{
	struct hw_x_point *coords = coordinates_x;
	uint32_t i = 0;
	const struct pwl_float_data_ex *rgb_regamma = rgb_ex;

	while (i <= hw_points_num + 1) {
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

/* The "old" interpolation uses a complicated scheme to build an array of
 * coefficients while also using an array of 0-255 normalized to 0-1
 * Then there's another loop using both of the above + new scaled user ramp
 * and we concatenate them. It also searches for points of interpolation and
 * uses enums for positions.
 *
 * This function uses a different approach:
 * user ramp is always applied on X with 0/255, 1/255, 2/255, ..., 255/255
 * To find index for hwX , we notice the following:
 * i/255 <= hwX < (i+1)/255  <=> i <= 255*hwX < i+1
 * See apply_lut_1d which is the same principle, but on 4K entry 1D LUT
 *
 * Once the index is known, combined Y is simply:
 * user_ramp(index) + (hwX-index/255)*(user_ramp(index+1) - user_ramp(index)
 *
 * We should switch to this method in all cases, it's simpler and faster
 * ToDo one day - for now this only applies to ADL regamma to avoid regression
 * for regular use cases (sRGB and PQ)
 */
static void interpolate_user_regamma(uint32_t hw_points_num,
		struct pwl_float_data *rgb_user,
		bool apply_degamma,
		struct dc_transfer_func_distributed_points *tf_pts)
{
	uint32_t i;
	uint32_t color = 0;
	int32_t index;
	int32_t index_next;
	struct fixed31_32 *tf_point;
	struct fixed31_32 hw_x;
	struct fixed31_32 norm_factor =
			dc_fixpt_from_int(255);
	struct fixed31_32 norm_x;
	struct fixed31_32 index_f;
	struct fixed31_32 lut1;
	struct fixed31_32 lut2;
	struct fixed31_32 delta_lut;
	struct fixed31_32 delta_index;

	i = 0;
	/* fixed_pt library has problems handling too small values */
	while (i != 32) {
		tf_pts->red[i] = dc_fixpt_zero;
		tf_pts->green[i] = dc_fixpt_zero;
		tf_pts->blue[i] = dc_fixpt_zero;
		++i;
	}
	while (i <= hw_points_num + 1) {
		for (color = 0; color < 3; color++) {
			if (color == 0)
				tf_point = &tf_pts->red[i];
			else if (color == 1)
				tf_point = &tf_pts->green[i];
			else
				tf_point = &tf_pts->blue[i];

			if (apply_degamma) {
				if (color == 0)
					hw_x = coordinates_x[i].regamma_y_red;
				else if (color == 1)
					hw_x = coordinates_x[i].regamma_y_green;
				else
					hw_x = coordinates_x[i].regamma_y_blue;
			} else
				hw_x = coordinates_x[i].x;

			norm_x = dc_fixpt_mul(norm_factor, hw_x);
			index = dc_fixpt_floor(norm_x);
			if (index < 0 || index > 255)
				continue;

			index_f = dc_fixpt_from_int(index);
			index_next = (index == 255) ? index : index + 1;

			if (color == 0) {
				lut1 = rgb_user[index].r;
				lut2 = rgb_user[index_next].r;
			} else if (color == 1) {
				lut1 = rgb_user[index].g;
				lut2 = rgb_user[index_next].g;
			} else {
				lut1 = rgb_user[index].b;
				lut2 = rgb_user[index_next].b;
			}

			// we have everything now, so interpolate
			delta_lut = dc_fixpt_sub(lut2, lut1);
			delta_index = dc_fixpt_sub(norm_x, index_f);

			*tf_point = dc_fixpt_add(lut1,
				dc_fixpt_mul(delta_index, delta_lut));
		}
		++i;
	}
}

static void build_new_custom_resulted_curve(
	uint32_t hw_points_num,
	struct dc_transfer_func_distributed_points *tf_pts)
{
	uint32_t i;

	i = 0;

	while (i != hw_points_num + 1) {
		tf_pts->red[i] = dc_fixpt_clamp(
			tf_pts->red[i], dc_fixpt_zero,
			dc_fixpt_one);
		tf_pts->green[i] = dc_fixpt_clamp(
			tf_pts->green[i], dc_fixpt_zero,
			dc_fixpt_one);
		tf_pts->blue[i] = dc_fixpt_clamp(
			tf_pts->blue[i], dc_fixpt_zero,
			dc_fixpt_one);

		++i;
	}
}

static void apply_degamma_for_user_regamma(struct pwl_float_data_ex *rgb_regamma,
		uint32_t hw_points_num)
{
	uint32_t i;

	struct gamma_coefficients coeff;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinates_x;

	build_coefficients(&coeff, true);

	i = 0;
	while (i != hw_points_num + 1) {
		rgb->r = translate_from_linear_space_ex(
				coord_x->x, &coeff, 0);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
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

	/* this should be named differently, all it does is clamp to 0-1 */
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

	rgb_user = kvcalloc(ramp->num_entries + _EXTRA_POINTS,
			    sizeof(*rgb_user),
			    GFP_KERNEL);
	if (!rgb_user)
		goto rgb_user_alloc_fail;
	rgb_regamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
			       sizeof(*rgb_regamma),
			       GFP_KERNEL);
	if (!rgb_regamma)
		goto rgb_regamma_alloc_fail;
	axix_x = kvcalloc(ramp->num_entries + 3, sizeof(*axix_x),
			  GFP_KERNEL);
	if (!axix_x)
		goto axix_x_alloc_fail;
	coeff = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*coeff),
			 GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	dividers.divider1 = dc_fixpt_from_fraction(3, 2);
	dividers.divider2 = dc_fixpt_from_int(2);
	dividers.divider3 = dc_fixpt_from_fraction(5, 2);

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

	kvfree(coeff);
coeff_alloc_fail:
	kvfree(axix_x);
axix_x_alloc_fail:
	kvfree(rgb_regamma);
rgb_regamma_alloc_fail:
	kvfree(rgb_user);
rgb_user_alloc_fail:
	return ret;
}

bool calculate_user_regamma_coeff(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma)
{
	struct gamma_coefficients coeff;
	const struct hw_x_point *coord_x = coordinates_x;
	uint32_t i = 0;

	do {
		coeff.a0[i] = dc_fixpt_from_fraction(
				regamma->coeff.A0[i], 10000000);
		coeff.a1[i] = dc_fixpt_from_fraction(
				regamma->coeff.A1[i], 1000);
		coeff.a2[i] = dc_fixpt_from_fraction(
				regamma->coeff.A2[i], 1000);
		coeff.a3[i] = dc_fixpt_from_fraction(
				regamma->coeff.A3[i], 1000);
		coeff.user_gamma[i] = dc_fixpt_from_fraction(
				regamma->coeff.gamma[i], 1000);

		++i;
	} while (i != 3);

	i = 0;
	/* fixed_pt library has problems handling too small values */
	while (i != 32) {
		output_tf->tf_pts.red[i] = dc_fixpt_zero;
		output_tf->tf_pts.green[i] = dc_fixpt_zero;
		output_tf->tf_pts.blue[i] = dc_fixpt_zero;
		++coord_x;
		++i;
	}
	while (i != MAX_HW_POINTS + 1) {
		output_tf->tf_pts.red[i] = translate_from_linear_space_ex(
				coord_x->x, &coeff, 0);
		output_tf->tf_pts.green[i] = translate_from_linear_space_ex(
				coord_x->x, &coeff, 1);
		output_tf->tf_pts.blue[i] = translate_from_linear_space_ex(
				coord_x->x, &coeff, 2);
		++coord_x;
		++i;
	}

	// this function just clamps output to 0-1
	build_new_custom_resulted_curve(MAX_HW_POINTS, &output_tf->tf_pts);
	output_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	return true;
}

bool calculate_user_regamma_ramp(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma)
{
	struct dc_transfer_func_distributed_points *tf_pts = &output_tf->tf_pts;
	struct dividers dividers;

	struct pwl_float_data *rgb_user = NULL;
	struct pwl_float_data_ex *rgb_regamma = NULL;
	bool ret = false;

	if (regamma == NULL)
		return false;

	output_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	rgb_user = kcalloc(GAMMA_RGB_256_ENTRIES + _EXTRA_POINTS,
			   sizeof(*rgb_user),
			   GFP_KERNEL);
	if (!rgb_user)
		goto rgb_user_alloc_fail;

	rgb_regamma = kcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
			      sizeof(*rgb_regamma),
			      GFP_KERNEL);
	if (!rgb_regamma)
		goto rgb_regamma_alloc_fail;

	dividers.divider1 = dc_fixpt_from_fraction(3, 2);
	dividers.divider2 = dc_fixpt_from_int(2);
	dividers.divider3 = dc_fixpt_from_fraction(5, 2);

	scale_user_regamma_ramp(rgb_user, &regamma->ramp, dividers);

	if (regamma->flags.bits.applyDegamma == 1) {
		apply_degamma_for_user_regamma(rgb_regamma, MAX_HW_POINTS);
		copy_rgb_regamma_to_coordinates_x(coordinates_x,
				MAX_HW_POINTS, rgb_regamma);
	}

	interpolate_user_regamma(MAX_HW_POINTS, rgb_user,
			regamma->flags.bits.applyDegamma, tf_pts);

	// no custom HDR curves!
	tf_pts->end_exponent = 0;
	tf_pts->x_point_at_y1_red = 1;
	tf_pts->x_point_at_y1_green = 1;
	tf_pts->x_point_at_y1_blue = 1;

	// this function just clamps output to 0-1
	build_new_custom_resulted_curve(MAX_HW_POINTS, tf_pts);

	ret = true;

	kfree(rgb_regamma);
rgb_regamma_alloc_fail:
	kvfree(rgb_user);
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

	rgb_user = kvcalloc(ramp->num_entries + _EXTRA_POINTS,
			    sizeof(*rgb_user),
			    GFP_KERNEL);
	if (!rgb_user)
		goto rgb_user_alloc_fail;
	curve = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*curve),
			 GFP_KERNEL);
	if (!curve)
		goto curve_alloc_fail;
	axix_x = kvcalloc(ramp->num_entries + _EXTRA_POINTS, sizeof(*axix_x),
			  GFP_KERNEL);
	if (!axix_x)
		goto axix_x_alloc_fail;
	coeff = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*coeff),
			 GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	dividers.divider1 = dc_fixpt_from_fraction(3, 2);
	dividers.divider2 = dc_fixpt_from_int(2);
	dividers.divider3 = dc_fixpt_from_fraction(5, 2);

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
			mapUserRamp && ramp->type != GAMMA_CUSTOM);
	if (ramp->type == GAMMA_CUSTOM)
		apply_lut_1d(ramp, MAX_HW_POINTS, tf_pts);

	ret = true;

	kvfree(coeff);
coeff_alloc_fail:
	kvfree(axix_x);
axix_x_alloc_fail:
	kvfree(curve);
curve_alloc_fail:
	kvfree(rgb_user);
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
		rgb_regamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_regamma),
				       GFP_KERNEL);
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

		kvfree(rgb_regamma);
	} else if (trans == TRANSFER_FUNCTION_SRGB ||
			  trans == TRANSFER_FUNCTION_BT709) {
		rgb_regamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_regamma),
				       GFP_KERNEL);
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

		kvfree(rgb_regamma);
	} else if (trans == TRANSFER_FUNCTION_HLG ||
		trans == TRANSFER_FUNCTION_HLG12) {
		rgb_regamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_regamma),
				       GFP_KERNEL);
		if (!rgb_regamma)
			goto rgb_regamma_alloc_fail;

		build_hlg_regamma(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				trans == TRANSFER_FUNCTION_HLG12 ? true:false);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_regamma[i].r;
			points->green[i]  = rgb_regamma[i].g;
			points->blue[i]   = rgb_regamma[i].b;
		}
		ret = true;
		kvfree(rgb_regamma);
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
		rgb_degamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_degamma),
				       GFP_KERNEL);
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

		kvfree(rgb_degamma);
	} else if (trans == TRANSFER_FUNCTION_SRGB ||
			  trans == TRANSFER_FUNCTION_BT709) {
		rgb_degamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_degamma),
				       GFP_KERNEL);
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

		kvfree(rgb_degamma);
	} else if (trans == TRANSFER_FUNCTION_HLG ||
		trans == TRANSFER_FUNCTION_HLG12) {
		rgb_degamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_degamma),
				       GFP_KERNEL);
		if (!rgb_degamma)
			goto rgb_degamma_alloc_fail;

		build_hlg_degamma(rgb_degamma,
				MAX_HW_POINTS,
				coordinates_x,
				trans == TRANSFER_FUNCTION_HLG12 ? true:false);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_degamma[i].r;
			points->green[i]  = rgb_degamma[i].g;
			points->blue[i]   = rgb_degamma[i].b;
		}
		ret = true;
		kvfree(rgb_degamma);
	}
	points->end_exponent = 0;
	points->x_point_at_y1_red = 1;
	points->x_point_at_y1_green = 1;
	points->x_point_at_y1_blue = 1;

rgb_degamma_alloc_fail:
	return ret;
}


