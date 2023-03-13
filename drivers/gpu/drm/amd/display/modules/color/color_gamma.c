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

#include <linux/mm.h>
#include <linux/slab.h>

#include "dc.h"
#include "opp.h"
#include "color_gamma.h"

static struct hw_x_point coordinates_x[MAX_HW_POINTS + 2];

// these are helpers for calculations to reduce stack usage
// do not depend on these being preserved across calls

/* Helper to optimize gamma calculation, only use in translate_from_linear, in
 * particular the dc_fixpt_pow function which is very expensive
 * The idea is that our regions for X points are exponential and currently they all use
 * the same number of points (NUM_PTS_IN_REGION) and in each region every point
 * is exactly 2x the one at the same index in the previous region. In other words
 * X[i] = 2 * X[i-NUM_PTS_IN_REGION] for i>=16
 * The other fact is that (2x)^gamma = 2^gamma * x^gamma
 * So we compute and save x^gamma for the first 16 regions, and for every next region
 * just multiply with 2^gamma which can be computed once, and save the result so we
 * recursively compute all the values.
 */
										/*sRGB	 709 2.2 2.4 P3*/
static const int32_t gamma_numerator01[] = { 31308,	180000,	0,	0,	0};
static const int32_t gamma_numerator02[] = { 12920,	4500,	0,	0,	0};
static const int32_t gamma_numerator03[] = { 55,	99,		0,	0,	0};
static const int32_t gamma_numerator04[] = { 55,	99,		0,	0,	0};
static const int32_t gamma_numerator05[] = { 2400,	2200,	2200, 2400, 2600};

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

void log_x_points_distribution(struct dal_logger *logger)
{
	int i = 0;

	if (logger != NULL) {
		LOG_GAMMA_WRITE("Log X Distribution\n");

		for (i = 0; i < MAX_HW_POINTS; i++)
			LOG_GAMMA_WRITE("%llu\n", coordinates_x[i].x.value);
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
	struct fixed31_32 base2;


	if (dc_fixpt_lt(in_x, dc_fixpt_zero))
		in_x = dc_fixpt_zero;

	l_pow_m1 = dc_fixpt_pow(in_x,
			dc_fixpt_div(dc_fixpt_one, m2));
	base = dc_fixpt_sub(l_pow_m1, c1);

	div = dc_fixpt_sub(c2, dc_fixpt_mul(c3, l_pow_m1));

	base2 = dc_fixpt_div(base, div);
	//avoid complex numbers
	if (dc_fixpt_lt(base2, dc_fixpt_zero))
		base2 = dc_fixpt_sub(dc_fixpt_zero, base2);


	*out_y = dc_fixpt_pow(base2, dc_fixpt_div(dc_fixpt_one, m1));

}


/*de gamma, none linear to linear*/
static void compute_hlg_eotf(struct fixed31_32 in_x,
		struct fixed31_32 *out_y,
		uint32_t sdr_white_level, uint32_t max_luminance_nits)
{
	struct fixed31_32 a;
	struct fixed31_32 b;
	struct fixed31_32 c;
	struct fixed31_32 threshold;
	struct fixed31_32 x;

	struct fixed31_32 scaling_factor =
			dc_fixpt_from_fraction(max_luminance_nits, sdr_white_level);
	a = dc_fixpt_from_fraction(17883277, 100000000);
	b = dc_fixpt_from_fraction(28466892, 100000000);
	c = dc_fixpt_from_fraction(55991073, 100000000);
	threshold = dc_fixpt_from_fraction(1, 2);

	if (dc_fixpt_lt(in_x, threshold)) {
		x = dc_fixpt_mul(in_x, in_x);
		x = dc_fixpt_div_int(x, 3);
	} else {
		x = dc_fixpt_sub(in_x, c);
		x = dc_fixpt_div(x, a);
		x = dc_fixpt_exp(x);
		x = dc_fixpt_add(x, b);
		x = dc_fixpt_div_int(x, 12);
	}
	*out_y = dc_fixpt_mul(x, scaling_factor);

}

/*re gamma, linear to none linear*/
static void compute_hlg_oetf(struct fixed31_32 in_x, struct fixed31_32 *out_y,
		uint32_t sdr_white_level, uint32_t max_luminance_nits)
{
	struct fixed31_32 a;
	struct fixed31_32 b;
	struct fixed31_32 c;
	struct fixed31_32 threshold;
	struct fixed31_32 x;

	struct fixed31_32 scaling_factor =
			dc_fixpt_from_fraction(sdr_white_level, max_luminance_nits);
	a = dc_fixpt_from_fraction(17883277, 100000000);
	b = dc_fixpt_from_fraction(28466892, 100000000);
	c = dc_fixpt_from_fraction(55991073, 100000000);
	threshold = dc_fixpt_from_fraction(1, 12);
	x = dc_fixpt_mul(in_x, scaling_factor);


	if (dc_fixpt_lt(x, threshold)) {
		x = dc_fixpt_mul(x, dc_fixpt_from_fraction(3, 1));
		*out_y = dc_fixpt_pow(x, dc_fixpt_half);
	} else {
		x = dc_fixpt_mul(x, dc_fixpt_from_fraction(12, 1));
		x = dc_fixpt_sub(x, b);
		x = dc_fixpt_log(x);
		x = dc_fixpt_mul(a, x);
		*out_y = dc_fixpt_add(x, c);
	}
}


/* one-time pre-compute PQ values - only for sdr_white_level 80 */
void precompute_pq(void)
{
	int i;
	struct fixed31_32 x;
	const struct hw_x_point *coord_x = coordinates_x + 32;
	struct fixed31_32 scaling_factor =
			dc_fixpt_from_fraction(80, 10000);

	struct fixed31_32 *pq_table = mod_color_get_table(type_pq_table);

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
	struct fixed31_32 *de_pq_table = mod_color_get_table(type_de_pq_table);
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


static bool build_coefficients(struct gamma_coefficients *coefficients, enum dc_transfer_func_predefined type)
{

	uint32_t i = 0;
	uint32_t index = 0;
	bool ret = true;

	if (type == TRANSFER_FUNCTION_SRGB)
		index = 0;
	else if (type == TRANSFER_FUNCTION_BT709)
		index = 1;
	else if (type == TRANSFER_FUNCTION_GAMMA22)
		index = 2;
	else if (type == TRANSFER_FUNCTION_GAMMA24)
		index = 3;
	else if (type == TRANSFER_FUNCTION_GAMMA26)
		index = 4;
	else {
		ret = false;
		goto release;
	}

	do {
		coefficients->a0[i] = dc_fixpt_from_fraction(
			gamma_numerator01[index], 10000000);
		coefficients->a1[i] = dc_fixpt_from_fraction(
			gamma_numerator02[index], 1000);
		coefficients->a2[i] = dc_fixpt_from_fraction(
			gamma_numerator03[index], 1000);
		coefficients->a3[i] = dc_fixpt_from_fraction(
			gamma_numerator04[index], 1000);
		coefficients->user_gamma[i] = dc_fixpt_from_fraction(
			gamma_numerator05[index], 1000);

		++i;
	} while (i != ARRAY_SIZE(coefficients->a0));
release:
	return ret;
}

static struct fixed31_32 translate_from_linear_space(
		struct translate_from_linear_space_args *args)
{
	const struct fixed31_32 one = dc_fixpt_from_int(1);

	struct fixed31_32 scratch_1, scratch_2;
	struct calculate_buffer *cal_buffer = args->cal_buffer;

	if (dc_fixpt_le(one, args->arg))
		return one;

	if (dc_fixpt_le(args->arg, dc_fixpt_neg(args->a0))) {
		scratch_1 = dc_fixpt_add(one, args->a3);
		scratch_2 = dc_fixpt_pow(
				dc_fixpt_neg(args->arg),
				dc_fixpt_recip(args->gamma));
		scratch_1 = dc_fixpt_mul(scratch_1, scratch_2);
		scratch_1 = dc_fixpt_sub(args->a2, scratch_1);

		return scratch_1;
	} else if (dc_fixpt_le(args->a0, args->arg)) {
		if (cal_buffer->buffer_index == 0) {
			cal_buffer->gamma_of_2 = dc_fixpt_pow(dc_fixpt_from_int(2),
					dc_fixpt_recip(args->gamma));
		}
		scratch_1 = dc_fixpt_add(one, args->a3);
		if (cal_buffer->buffer_index < 16)
			scratch_2 = dc_fixpt_pow(args->arg,
					dc_fixpt_recip(args->gamma));
		else
			scratch_2 = dc_fixpt_mul(cal_buffer->gamma_of_2,
					cal_buffer->buffer[cal_buffer->buffer_index%16]);

		if (cal_buffer->buffer_index != -1) {
			cal_buffer->buffer[cal_buffer->buffer_index%16] = scratch_2;
			cal_buffer->buffer_index++;
		}

		scratch_1 = dc_fixpt_mul(scratch_1, scratch_2);
		scratch_1 = dc_fixpt_sub(scratch_1, args->a2);

		return scratch_1;
	}
	else
		return dc_fixpt_mul(args->arg, args->a1);
}


static struct fixed31_32 translate_from_linear_space_long(
		struct translate_from_linear_space_args *args)
{
	const struct fixed31_32 one = dc_fixpt_from_int(1);

	if (dc_fixpt_lt(one, args->arg))
		return one;

	if (dc_fixpt_le(args->arg, dc_fixpt_neg(args->a0)))
		return dc_fixpt_sub(
			args->a2,
			dc_fixpt_mul(
				dc_fixpt_add(
					one,
					args->a3),
				dc_fixpt_pow(
					dc_fixpt_neg(args->arg),
					dc_fixpt_recip(args->gamma))));
	else if (dc_fixpt_le(args->a0, args->arg))
		return dc_fixpt_sub(
			dc_fixpt_mul(
				dc_fixpt_add(
					one,
					args->a3),
				dc_fixpt_pow(
						args->arg,
					dc_fixpt_recip(args->gamma))),
					args->a2);
	else
		return dc_fixpt_mul(
			args->arg,
			args->a1);
}

static struct fixed31_32 calculate_gamma22(struct fixed31_32 arg, bool use_eetf, struct calculate_buffer *cal_buffer)
{
	struct fixed31_32 gamma = dc_fixpt_from_fraction(22, 10);
	struct translate_from_linear_space_args scratch_gamma_args;

	scratch_gamma_args.arg = arg;
	scratch_gamma_args.a0 = dc_fixpt_zero;
	scratch_gamma_args.a1 = dc_fixpt_zero;
	scratch_gamma_args.a2 = dc_fixpt_zero;
	scratch_gamma_args.a3 = dc_fixpt_zero;
	scratch_gamma_args.cal_buffer = cal_buffer;
	scratch_gamma_args.gamma = gamma;

	if (use_eetf)
		return translate_from_linear_space_long(&scratch_gamma_args);

	return translate_from_linear_space(&scratch_gamma_args);
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

static struct fixed31_32 translate_from_linear_space_ex(
	struct fixed31_32 arg,
	struct gamma_coefficients *coeff,
	uint32_t color_index,
	struct calculate_buffer *cal_buffer)
{
	struct translate_from_linear_space_args scratch_gamma_args;

	scratch_gamma_args.arg = arg;
	scratch_gamma_args.a0 = coeff->a0[color_index];
	scratch_gamma_args.a1 = coeff->a1[color_index];
	scratch_gamma_args.a2 = coeff->a2[color_index];
	scratch_gamma_args.a3 = coeff->a3[color_index];
	scratch_gamma_args.gamma = coeff->user_gamma[color_index];
	scratch_gamma_args.cal_buffer = cal_buffer;

	return translate_from_linear_space(&scratch_gamma_args);
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
	struct fixed31_32 *pq_table = mod_color_get_table(type_pq_table);

	if (!mod_color_is_table_init(type_pq_table) && sdr_white_level == 80) {
		precompute_pq();
		mod_color_set_table_init_state(type_pq_table, true);
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
	struct fixed31_32 *de_pq_table = mod_color_get_table(type_de_pq_table);
	struct fixed31_32 scaling_factor = dc_fixpt_from_int(125);

	if (!mod_color_is_table_init(type_de_pq_table)) {
		precompute_de_pq();
		mod_color_set_table_init_state(type_de_pq_table, true);
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

static bool build_regamma(struct pwl_float_data_ex *rgb_regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		enum dc_transfer_func_predefined type,
		struct calculate_buffer *cal_buffer)
{
	uint32_t i;
	bool ret = false;

	struct gamma_coefficients *coeff;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;

	coeff = kvzalloc(sizeof(*coeff), GFP_KERNEL);
	if (!coeff)
		goto release;

	if (!build_coefficients(coeff, type))
		goto release;

	memset(cal_buffer->buffer, 0, NUM_PTS_IN_REGION * sizeof(struct fixed31_32));
	cal_buffer->buffer_index = 0; // see variable definition for more info

	i = 0;
	while (i <= hw_points_num) {
		/*TODO use y vs r,g,b*/
		rgb->r = translate_from_linear_space_ex(
			coord_x->x, coeff, 0, cal_buffer);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
		++i;
	}
	cal_buffer->buffer_index = -1;
	ret = true;
release:
	kvfree(coeff);
	return ret;
}

static void hermite_spline_eetf(struct fixed31_32 input_x,
				struct fixed31_32 max_display,
				struct fixed31_32 min_display,
				struct fixed31_32 max_content,
				struct fixed31_32 *out_x)
{
	struct fixed31_32 min_lum_pq;
	struct fixed31_32 max_lum_pq;
	struct fixed31_32 max_content_pq;
	struct fixed31_32 ks;
	struct fixed31_32 E1;
	struct fixed31_32 E2;
	struct fixed31_32 E3;
	struct fixed31_32 t;
	struct fixed31_32 t2;
	struct fixed31_32 t3;
	struct fixed31_32 two;
	struct fixed31_32 three;
	struct fixed31_32 temp1;
	struct fixed31_32 temp2;
	struct fixed31_32 a = dc_fixpt_from_fraction(15, 10);
	struct fixed31_32 b = dc_fixpt_from_fraction(5, 10);
	struct fixed31_32 epsilon = dc_fixpt_from_fraction(1, 1000000); // dc_fixpt_epsilon is a bit too small

	if (dc_fixpt_eq(max_content, dc_fixpt_zero)) {
		*out_x = dc_fixpt_zero;
		return;
	}

	compute_pq(input_x, &E1);
	compute_pq(dc_fixpt_div(min_display, max_content), &min_lum_pq);
	compute_pq(dc_fixpt_div(max_display, max_content), &max_lum_pq);
	compute_pq(dc_fixpt_one, &max_content_pq); // always 1? DAL2 code is weird
	a = dc_fixpt_div(dc_fixpt_add(dc_fixpt_one, b), max_content_pq); // (1+b)/maxContent
	ks = dc_fixpt_sub(dc_fixpt_mul(a, max_lum_pq), b); // a * max_lum_pq - b

	if (dc_fixpt_lt(E1, ks))
		E2 = E1;
	else if (dc_fixpt_le(ks, E1) && dc_fixpt_le(E1, dc_fixpt_one)) {
		if (dc_fixpt_lt(epsilon, dc_fixpt_sub(dc_fixpt_one, ks)))
			// t = (E1 - ks) / (1 - ks)
			t = dc_fixpt_div(dc_fixpt_sub(E1, ks),
					dc_fixpt_sub(dc_fixpt_one, ks));
		else
			t = dc_fixpt_zero;

		two = dc_fixpt_from_int(2);
		three = dc_fixpt_from_int(3);

		t2 = dc_fixpt_mul(t, t);
		t3 = dc_fixpt_mul(t2, t);
		temp1 = dc_fixpt_mul(two, t3);
		temp2 = dc_fixpt_mul(three, t2);

		// (2t^3 - 3t^2 + 1) * ks
		E2 = dc_fixpt_mul(ks, dc_fixpt_add(dc_fixpt_one,
				dc_fixpt_sub(temp1, temp2)));

		// (-2t^3 + 3t^2) * max_lum_pq
		E2 = dc_fixpt_add(E2, dc_fixpt_mul(max_lum_pq,
				dc_fixpt_sub(temp2, temp1)));

		temp1 = dc_fixpt_mul(two, t2);
		temp2 = dc_fixpt_sub(dc_fixpt_one, ks);

		// (t^3 - 2t^2 + t) * (1-ks)
		E2 = dc_fixpt_add(E2, dc_fixpt_mul(temp2,
				dc_fixpt_add(t, dc_fixpt_sub(t3, temp1))));
	} else
		E2 = dc_fixpt_one;

	temp1 = dc_fixpt_sub(dc_fixpt_one, E2);
	temp2 = dc_fixpt_mul(temp1, temp1);
	temp2 = dc_fixpt_mul(temp2, temp2);
	// temp2 = (1-E2)^4

	E3 =  dc_fixpt_add(E2, dc_fixpt_mul(min_lum_pq, temp2));
	compute_de_pq(E3, out_x);

	*out_x = dc_fixpt_div(*out_x, dc_fixpt_div(max_display, max_content));
}

static bool build_freesync_hdr(struct pwl_float_data_ex *rgb_regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		const struct freesync_hdr_tf_params *fs_params,
		struct calculate_buffer *cal_buffer)
{
	uint32_t i;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;
	struct fixed31_32 scaledX = dc_fixpt_zero;
	struct fixed31_32 scaledX1 = dc_fixpt_zero;
	struct fixed31_32 max_display;
	struct fixed31_32 min_display;
	struct fixed31_32 max_content;
	struct fixed31_32 clip = dc_fixpt_one;
	struct fixed31_32 output;
	bool use_eetf = false;
	bool is_clipped = false;
	struct fixed31_32 sdr_white_level;

	if (fs_params->max_content == 0 ||
			fs_params->max_display == 0)
		return false;

	max_display = dc_fixpt_from_int(fs_params->max_display);
	min_display = dc_fixpt_from_fraction(fs_params->min_display, 10000);
	max_content = dc_fixpt_from_int(fs_params->max_content);
	sdr_white_level = dc_fixpt_from_int(fs_params->sdr_white_level);

	if (fs_params->min_display > 1000) // cap at 0.1 at the bottom
		min_display = dc_fixpt_from_fraction(1, 10);
	if (fs_params->max_display < 100) // cap at 100 at the top
		max_display = dc_fixpt_from_int(100);

	// only max used, we don't adjust min luminance
	if (fs_params->max_content > fs_params->max_display)
		use_eetf = true;
	else
		max_content = max_display;

	if (!use_eetf)
		cal_buffer->buffer_index = 0; // see var definition for more info
	rgb += 32; // first 32 points have problems with fixed point, too small
	coord_x += 32;
	for (i = 32; i <= hw_points_num; i++) {
		if (!is_clipped) {
			if (use_eetf) {
				/*max content is equal 1 */
				scaledX1 = dc_fixpt_div(coord_x->x,
						dc_fixpt_div(max_content, sdr_white_level));
				hermite_spline_eetf(scaledX1, max_display, min_display,
						max_content, &scaledX);
			} else
				scaledX = dc_fixpt_div(coord_x->x,
						dc_fixpt_div(max_display, sdr_white_level));

			if (dc_fixpt_lt(scaledX, clip)) {
				if (dc_fixpt_lt(scaledX, dc_fixpt_zero))
					output = dc_fixpt_zero;
				else
					output = calculate_gamma22(scaledX, use_eetf, cal_buffer);

				rgb->r = output;
				rgb->g = output;
				rgb->b = output;
			} else {
				is_clipped = true;
				rgb->r = clip;
				rgb->g = clip;
				rgb->b = clip;
			}
		} else {
			rgb->r = clip;
			rgb->g = clip;
			rgb->b = clip;
		}

		++coord_x;
		++rgb;
	}
	cal_buffer->buffer_index = -1;

	return true;
}

static bool build_degamma(struct pwl_float_data_ex *curve,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x, enum dc_transfer_func_predefined type)
{
	uint32_t i;
	struct gamma_coefficients coeff;
	uint32_t begin_index, end_index;
	bool ret = false;

	if (!build_coefficients(&coeff, type))
		goto release;

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
	ret = true;
release:
	return ret;
}





static void build_hlg_degamma(struct pwl_float_data_ex *degamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		uint32_t sdr_white_level, uint32_t max_luminance_nits)
{
	uint32_t i;

	struct pwl_float_data_ex *rgb = degamma;
	const struct hw_x_point *coord_x = coordinate_x;

	i = 0;
	//check when i == 434
	while (i != hw_points_num + 1) {
		compute_hlg_eotf(coord_x->x, &rgb->r, sdr_white_level, max_luminance_nits);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
		++coord_x;
		++rgb;
		++i;
	}
}


static void build_hlg_regamma(struct pwl_float_data_ex *regamma,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		uint32_t sdr_white_level, uint32_t max_luminance_nits)
{
	uint32_t i;

	struct pwl_float_data_ex *rgb = regamma;
	const struct hw_x_point *coord_x = coordinate_x;

	i = 0;

	//when i == 471
	while (i != hw_points_num + 1) {
		compute_hlg_oetf(coord_x->x, &rgb->r, sdr_white_level, max_luminance_nits);
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
	const struct fixed31_32 one = dc_fixpt_from_int(1);

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

			if (dc_fixpt_le(one, hw_x))
				hw_x = one;

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
		uint32_t hw_points_num, struct calculate_buffer *cal_buffer)
{
	uint32_t i;

	struct gamma_coefficients coeff;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinates_x;

	build_coefficients(&coeff, TRANSFER_FUNCTION_SRGB);

	i = 0;
	while (i != hw_points_num + 1) {
		rgb->r = translate_from_linear_space_ex(
				coord_x->x, &coeff, 0, cal_buffer);
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

	if (ramp && mapUserRamp) {
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

bool calculate_user_regamma_coeff(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma,
		struct calculate_buffer *cal_buffer)
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
				coord_x->x, &coeff, 0, cal_buffer);
		output_tf->tf_pts.green[i] = translate_from_linear_space_ex(
				coord_x->x, &coeff, 1, cal_buffer);
		output_tf->tf_pts.blue[i] = translate_from_linear_space_ex(
				coord_x->x, &coeff, 2, cal_buffer);
		++coord_x;
		++i;
	}

	// this function just clamps output to 0-1
	build_new_custom_resulted_curve(MAX_HW_POINTS, &output_tf->tf_pts);
	output_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	return true;
}

bool calculate_user_regamma_ramp(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma,
		struct calculate_buffer *cal_buffer)
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
		apply_degamma_for_user_regamma(rgb_regamma, MAX_HW_POINTS, cal_buffer);
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
	kfree(rgb_user);
rgb_user_alloc_fail:
	return ret;
}

bool mod_color_calculate_degamma_params(struct dc_color_caps *dc_caps,
		struct dc_transfer_func *input_tf,
		const struct dc_gamma *ramp, bool mapUserRamp)
{
	struct dc_transfer_func_distributed_points *tf_pts = &input_tf->tf_pts;
	struct dividers dividers;
	struct pwl_float_data *rgb_user = NULL;
	struct pwl_float_data_ex *curve = NULL;
	struct gamma_pixel *axis_x = NULL;
	struct pixel_gamma_point *coeff = NULL;
	enum dc_transfer_func_predefined tf = TRANSFER_FUNCTION_SRGB;
	uint32_t i;
	bool ret = false;

	if (input_tf->type == TF_TYPE_BYPASS)
		return false;

	/* we can use hardcoded curve for plain SRGB TF
	 * If linear, it's bypass if on user ramp
	 */
	if (input_tf->type == TF_TYPE_PREDEFINED) {
		if ((input_tf->tf == TRANSFER_FUNCTION_SRGB ||
				input_tf->tf == TRANSFER_FUNCTION_LINEAR) &&
				!mapUserRamp)
			return true;

		if (dc_caps != NULL &&
			dc_caps->dpp.dcn_arch == 1) {

			if (input_tf->tf == TRANSFER_FUNCTION_PQ &&
					dc_caps->dpp.dgam_rom_caps.pq == 1)
				return true;

			if (input_tf->tf == TRANSFER_FUNCTION_GAMMA22 &&
					dc_caps->dpp.dgam_rom_caps.gamma2_2 == 1)
				return true;

			// HLG OOTF not accounted for
			if (input_tf->tf == TRANSFER_FUNCTION_HLG &&
					dc_caps->dpp.dgam_rom_caps.hlg == 1)
				return true;
		}
	}

	input_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	if (mapUserRamp && ramp && ramp->type == GAMMA_RGB_256) {
		rgb_user = kvcalloc(ramp->num_entries + _EXTRA_POINTS,
				sizeof(*rgb_user),
				GFP_KERNEL);
		if (!rgb_user)
			goto rgb_user_alloc_fail;

		axis_x = kvcalloc(ramp->num_entries + _EXTRA_POINTS, sizeof(*axis_x),
				GFP_KERNEL);
		if (!axis_x)
			goto axis_x_alloc_fail;

		dividers.divider1 = dc_fixpt_from_fraction(3, 2);
		dividers.divider2 = dc_fixpt_from_int(2);
		dividers.divider3 = dc_fixpt_from_fraction(5, 2);

		build_evenly_distributed_points(
				axis_x,
				ramp->num_entries,
				dividers);

		scale_gamma(rgb_user, ramp, dividers);
	}

	curve = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*curve),
			GFP_KERNEL);
	if (!curve)
		goto curve_alloc_fail;

	coeff = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*coeff),
			GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	tf = input_tf->tf;

	if (tf == TRANSFER_FUNCTION_PQ)
		build_de_pq(curve,
				MAX_HW_POINTS,
				coordinates_x);
	else if (tf == TRANSFER_FUNCTION_SRGB ||
		tf == TRANSFER_FUNCTION_BT709 ||
		tf == TRANSFER_FUNCTION_GAMMA22 ||
		tf == TRANSFER_FUNCTION_GAMMA24 ||
		tf == TRANSFER_FUNCTION_GAMMA26)
		build_degamma(curve,
				MAX_HW_POINTS,
				coordinates_x,
				tf);
	else if (tf == TRANSFER_FUNCTION_HLG)
		build_hlg_degamma(curve,
				MAX_HW_POINTS,
				coordinates_x,
				80, 1000);
	else if (tf == TRANSFER_FUNCTION_LINEAR) {
		// just copy coordinates_x into curve
		i = 0;
		while (i != MAX_HW_POINTS + 1) {
			curve[i].r = coordinates_x[i].x;
			curve[i].g = curve[i].r;
			curve[i].b = curve[i].r;
			i++;
		}
	} else
		goto invalid_tf_fail;

	tf_pts->end_exponent = 0;
	tf_pts->x_point_at_y1_red = 1;
	tf_pts->x_point_at_y1_green = 1;
	tf_pts->x_point_at_y1_blue = 1;

	if (input_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* just copy current rgb_regamma into  tf_pts */
		struct pwl_float_data_ex *curvePt = curve;
		int i = 0;

		while (i <= MAX_HW_POINTS) {
			tf_pts->red[i]   = curvePt->r;
			tf_pts->green[i] = curvePt->g;
			tf_pts->blue[i]  = curvePt->b;
			++curvePt;
			++i;
		}
	} else {
		//clamps to 0-1
		map_regamma_hw_to_x_user(ramp, coeff, rgb_user,
				coordinates_x, axis_x, curve,
				MAX_HW_POINTS, tf_pts,
				mapUserRamp && ramp && ramp->type == GAMMA_RGB_256);
	}



	if (ramp && ramp->type == GAMMA_CUSTOM)
		apply_lut_1d(ramp, MAX_HW_POINTS, tf_pts);

	ret = true;

invalid_tf_fail:
	kvfree(coeff);
coeff_alloc_fail:
	kvfree(curve);
curve_alloc_fail:
	kvfree(axis_x);
axis_x_alloc_fail:
	kvfree(rgb_user);
rgb_user_alloc_fail:

	return ret;
}

static bool calculate_curve(enum dc_transfer_func_predefined trans,
				struct dc_transfer_func_distributed_points *points,
				struct pwl_float_data_ex *rgb_regamma,
				const struct freesync_hdr_tf_params *fs_params,
				uint32_t sdr_ref_white_level,
				struct calculate_buffer *cal_buffer)
{
	uint32_t i;
	bool ret = false;

	if (trans == TRANSFER_FUNCTION_UNITY ||
		trans == TRANSFER_FUNCTION_LINEAR) {
		points->end_exponent = 0;
		points->x_point_at_y1_red = 1;
		points->x_point_at_y1_green = 1;
		points->x_point_at_y1_blue = 1;

		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			rgb_regamma[i].r = coordinates_x[i].x;
			rgb_regamma[i].g = coordinates_x[i].x;
			rgb_regamma[i].b = coordinates_x[i].x;
		}

		ret = true;
	} else if (trans == TRANSFER_FUNCTION_PQ) {
		points->end_exponent = 7;
		points->x_point_at_y1_red = 125;
		points->x_point_at_y1_green = 125;
		points->x_point_at_y1_blue = 125;

		build_pq(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				sdr_ref_white_level);

		ret = true;
	} else if (trans == TRANSFER_FUNCTION_GAMMA22 &&
			fs_params != NULL && fs_params->skip_tm == 0) {
		build_freesync_hdr(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				fs_params,
				cal_buffer);

		ret = true;
	} else if (trans == TRANSFER_FUNCTION_HLG) {
		points->end_exponent = 4;
		points->x_point_at_y1_red = 12;
		points->x_point_at_y1_green = 12;
		points->x_point_at_y1_blue = 12;

		build_hlg_regamma(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				80, 1000);

		ret = true;
	} else {
		// trans == TRANSFER_FUNCTION_SRGB
		// trans == TRANSFER_FUNCTION_BT709
		// trans == TRANSFER_FUNCTION_GAMMA22
		// trans == TRANSFER_FUNCTION_GAMMA24
		// trans == TRANSFER_FUNCTION_GAMMA26
		points->end_exponent = 0;
		points->x_point_at_y1_red = 1;
		points->x_point_at_y1_green = 1;
		points->x_point_at_y1_blue = 1;

		build_regamma(rgb_regamma,
				MAX_HW_POINTS,
				coordinates_x,
				trans,
				cal_buffer);

		ret = true;
	}

	return ret;
}

bool mod_color_calculate_regamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp, bool canRomBeUsed,
		const struct freesync_hdr_tf_params *fs_params,
		struct calculate_buffer *cal_buffer)
{
	struct dc_transfer_func_distributed_points *tf_pts = &output_tf->tf_pts;
	struct dividers dividers;

	struct pwl_float_data *rgb_user = NULL;
	struct pwl_float_data_ex *rgb_regamma = NULL;
	struct gamma_pixel *axis_x = NULL;
	struct pixel_gamma_point *coeff = NULL;
	enum dc_transfer_func_predefined tf = TRANSFER_FUNCTION_SRGB;
	bool ret = false;

	if (output_tf->type == TF_TYPE_BYPASS)
		return false;

	/* we can use hardcoded curve for plain SRGB TF */
	if (output_tf->type == TF_TYPE_PREDEFINED && canRomBeUsed == true &&
			output_tf->tf == TRANSFER_FUNCTION_SRGB) {
		if (ramp == NULL)
			return true;
		if ((ramp->is_identity && ramp->type != GAMMA_CS_TFM_1D) ||
				(!mapUserRamp && ramp->type == GAMMA_RGB_256))
			return true;
	}

	output_tf->type = TF_TYPE_DISTRIBUTED_POINTS;

	if (ramp && ramp->type != GAMMA_CS_TFM_1D &&
			(mapUserRamp || ramp->type != GAMMA_RGB_256)) {
		rgb_user = kvcalloc(ramp->num_entries + _EXTRA_POINTS,
			    sizeof(*rgb_user),
			    GFP_KERNEL);
		if (!rgb_user)
			goto rgb_user_alloc_fail;

		axis_x = kvcalloc(ramp->num_entries + 3, sizeof(*axis_x),
				GFP_KERNEL);
		if (!axis_x)
			goto axis_x_alloc_fail;

		dividers.divider1 = dc_fixpt_from_fraction(3, 2);
		dividers.divider2 = dc_fixpt_from_int(2);
		dividers.divider3 = dc_fixpt_from_fraction(5, 2);

		build_evenly_distributed_points(
				axis_x,
				ramp->num_entries,
				dividers);

		if (ramp->type == GAMMA_RGB_256 && mapUserRamp)
			scale_gamma(rgb_user, ramp, dividers);
		else if (ramp->type == GAMMA_RGB_FLOAT_1024)
			scale_gamma_dx(rgb_user, ramp, dividers);
	}

	rgb_regamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
			       sizeof(*rgb_regamma),
			       GFP_KERNEL);
	if (!rgb_regamma)
		goto rgb_regamma_alloc_fail;

	coeff = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS, sizeof(*coeff),
			 GFP_KERNEL);
	if (!coeff)
		goto coeff_alloc_fail;

	tf = output_tf->tf;

	ret = calculate_curve(tf,
			tf_pts,
			rgb_regamma,
			fs_params,
			output_tf->sdr_ref_white_level,
			cal_buffer);

	if (ret) {
		map_regamma_hw_to_x_user(ramp, coeff, rgb_user,
				coordinates_x, axis_x, rgb_regamma,
				MAX_HW_POINTS, tf_pts,
				(mapUserRamp || (ramp && ramp->type != GAMMA_RGB_256)) &&
				(ramp && ramp->type != GAMMA_CS_TFM_1D));

		if (ramp && ramp->type == GAMMA_CS_TFM_1D)
			apply_lut_1d(ramp, MAX_HW_POINTS, tf_pts);
	}

	kvfree(coeff);
coeff_alloc_fail:
	kvfree(rgb_regamma);
rgb_regamma_alloc_fail:
	kvfree(axis_x);
axis_x_alloc_fail:
	kvfree(rgb_user);
rgb_user_alloc_fail:
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
		trans == TRANSFER_FUNCTION_BT709 ||
		trans == TRANSFER_FUNCTION_GAMMA22 ||
		trans == TRANSFER_FUNCTION_GAMMA24 ||
		trans == TRANSFER_FUNCTION_GAMMA26) {
		rgb_degamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_degamma),
				       GFP_KERNEL);
		if (!rgb_degamma)
			goto rgb_degamma_alloc_fail;

		build_degamma(rgb_degamma,
				MAX_HW_POINTS,
				coordinates_x,
				trans);
		for (i = 0; i <= MAX_HW_POINTS ; i++) {
			points->red[i]    = rgb_degamma[i].r;
			points->green[i]  = rgb_degamma[i].g;
			points->blue[i]   = rgb_degamma[i].b;
		}
		ret = true;

		kvfree(rgb_degamma);
	} else if (trans == TRANSFER_FUNCTION_HLG) {
		rgb_degamma = kvcalloc(MAX_HW_POINTS + _EXTRA_POINTS,
				       sizeof(*rgb_degamma),
				       GFP_KERNEL);
		if (!rgb_degamma)
			goto rgb_degamma_alloc_fail;

		build_hlg_degamma(rgb_degamma,
				MAX_HW_POINTS,
				coordinates_x,
				80, 1000);
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
