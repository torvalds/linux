/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "gamma_calcs.h"

struct curve_config {
	uint32_t offset;
	int8_t segments[16];
	int8_t begin;
};

static bool build_custom_float(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	bool *negative,
	uint32_t *mantissa,
	uint32_t *exponenta)
{
	uint32_t exp_offset = (1 << (format->exponenta_bits - 1)) - 1;

	const struct fixed31_32 mantissa_constant_plus_max_fraction =
		dal_fixed31_32_from_fraction(
			(1LL << (format->mantissa_bits + 1)) - 1,
			1LL << format->mantissa_bits);

	struct fixed31_32 mantiss;

	if (dal_fixed31_32_eq(
		value,
		dal_fixed31_32_zero)) {
		*negative = false;
		*mantissa = 0;
		*exponenta = 0;
		return true;
	}

	if (dal_fixed31_32_lt(
		value,
		dal_fixed31_32_zero)) {
		*negative = format->sign;
		value = dal_fixed31_32_neg(value);
	} else {
		*negative = false;
	}

	if (dal_fixed31_32_lt(
		value,
		dal_fixed31_32_one)) {
		uint32_t i = 1;

		do {
			value = dal_fixed31_32_shl(value, 1);
			++i;
		} while (dal_fixed31_32_lt(
			value,
			dal_fixed31_32_one));

		--i;

		if (exp_offset <= i) {
			*mantissa = 0;
			*exponenta = 0;
			return true;
		}

		*exponenta = exp_offset - i;
	} else if (dal_fixed31_32_le(
		mantissa_constant_plus_max_fraction,
		value)) {
		uint32_t i = 1;

		do {
			value = dal_fixed31_32_shr(value, 1);
			++i;
		} while (dal_fixed31_32_lt(
			mantissa_constant_plus_max_fraction,
			value));

		*exponenta = exp_offset + i - 1;
	} else {
		*exponenta = exp_offset;
	}

	mantiss = dal_fixed31_32_sub(
		value,
		dal_fixed31_32_one);

	if (dal_fixed31_32_lt(
			mantiss,
			dal_fixed31_32_zero) ||
		dal_fixed31_32_lt(
			dal_fixed31_32_one,
			mantiss))
		mantiss = dal_fixed31_32_zero;
	else
		mantiss = dal_fixed31_32_shl(
			mantiss,
			format->mantissa_bits);

	*mantissa = dal_fixed31_32_floor(mantiss);

	return true;
}

static bool setup_custom_float(
	const struct custom_float_format *format,
	bool negative,
	uint32_t mantissa,
	uint32_t exponenta,
	uint32_t *result)
{
	uint32_t i = 0;
	uint32_t j = 0;

	uint32_t value = 0;

	/* verification code:
	 * once calculation is ok we can remove it
	 */

	const uint32_t mantissa_mask =
		(1 << (format->mantissa_bits + 1)) - 1;

	const uint32_t exponenta_mask =
		(1 << (format->exponenta_bits + 1)) - 1;

	if (mantissa & ~mantissa_mask) {
		BREAK_TO_DEBUGGER();
		mantissa = mantissa_mask;
	}

	if (exponenta & ~exponenta_mask) {
		BREAK_TO_DEBUGGER();
		exponenta = exponenta_mask;
	}

	/* end of verification code */

	while (i < format->mantissa_bits) {
		uint32_t mask = 1 << i;

		if (mantissa & mask)
			value |= mask;

		++i;
	}

	while (j < format->exponenta_bits) {
		uint32_t mask = 1 << j;

		if (exponenta & mask)
			value |= mask << i;

		++j;
	}

	if (negative && format->sign)
		value |= 1 << (i + j);

	*result = value;

	return true;
}

static bool convert_to_custom_float_format_ex(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	struct custom_float_value *result)
{
	return build_custom_float(
		value, format,
		&result->negative, &result->mantissa, &result->exponenta) &&
	setup_custom_float(
		format, result->negative, result->mantissa, result->exponenta,
		&result->value);
}

static bool round_custom_float_6_12(
	struct hw_x_point *x)
{
	struct custom_float_format fmt;

	struct custom_float_value value;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format_ex(
		x->x, &fmt, &value))
		return false;

	x->adjusted_x = x->x;

	if (value.mantissa) {
		BREAK_TO_DEBUGGER();

		return false;
	}

	return true;
}

static bool build_hw_curve_configuration(
	const struct curve_config *curve_config,
	struct gamma_curve *gamma_curve,
	struct curve_points *curve_points,
	struct hw_x_point *points,
	uint32_t *number_of_points)
{
	const int8_t max_regions_number = ARRAY_SIZE(curve_config->segments);

	int8_t i;

	uint8_t segments_calculation[8] = { 0 };

	struct fixed31_32 region1 = dal_fixed31_32_zero;
	struct fixed31_32 region2;
	struct fixed31_32 increment;

	uint32_t index = 0;
	uint32_t segments = 0;
	uint32_t max_number;

	bool result = false;

	if (!number_of_points) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	max_number = *number_of_points;

	i = 0;

	while (i != max_regions_number) {
		gamma_curve[i].offset = 0;
		gamma_curve[i].segments_num = 0;

		++i;
	}

	i = 0;

	while (i != max_regions_number) {
		/* number should go in uninterruptible sequence */
		if (curve_config->segments[i] == -1)
			break;

		ASSERT(curve_config->segments[i] >= 0);

		segments += (1 << curve_config->segments[i]);

		++i;
	}

	if (segments > max_number) {
		BREAK_TO_DEBUGGER();
	} else {
		int32_t divisor;
		uint32_t offset = 0;
		int8_t begin = curve_config->begin;
		int32_t region_number = 0;

		i = begin;

		while ((index < max_number) &&
			(region_number < max_regions_number) &&
			(i <= 1)) {
			int32_t j = 0;

			segments = curve_config->segments[region_number];
			divisor = 1 << segments;

			if (segments == -1) {
				if (i > 0) {
					region1 = dal_fixed31_32_shl(
						dal_fixed31_32_one,
						i - 1);
					region2 = dal_fixed31_32_shl(
						dal_fixed31_32_one,
						i);
				} else {
					region1 = dal_fixed31_32_shr(
						dal_fixed31_32_one,
						-(i - 1));
					region2 = dal_fixed31_32_shr(
						dal_fixed31_32_one,
						-i);
				}

				break;
			}

			if (i > -1) {
				region1 = dal_fixed31_32_shl(
					dal_fixed31_32_one,
					i);
				region2 = dal_fixed31_32_shl(
					dal_fixed31_32_one,
					i + 1);
			} else {
				region1 = dal_fixed31_32_shr(
					dal_fixed31_32_one,
					-i);
				region2 = dal_fixed31_32_shr(
					dal_fixed31_32_one,
					-(i + 1));
			}

			gamma_curve[region_number].offset = offset;
			gamma_curve[region_number].segments_num = segments;

			offset += divisor;

			++segments_calculation[segments];

			increment = dal_fixed31_32_div_int(
				dal_fixed31_32_sub(
					region2,
					region1),
				divisor);

			points[index].x = region1;

			round_custom_float_6_12(points + index);

			++index;
			++region_number;

			while ((index < max_number) && (j < divisor - 1)) {
				region1 = dal_fixed31_32_add(
					region1,
					increment);

				points[index].x = region1;
				points[index].adjusted_x = region1;

				++index;
				++j;
			}

			++i;
		}

		points[index].x = region1;

		round_custom_float_6_12(points + index);

		*number_of_points = index;

		result = true;
	}

	curve_points[0].x = points[0].adjusted_x;
	curve_points[0].offset = dal_fixed31_32_zero;

	curve_points[1].x = points[index - 1].adjusted_x;
	curve_points[1].offset = dal_fixed31_32_zero;

	curve_points[2].x = points[index].adjusted_x;
	curve_points[2].offset = dal_fixed31_32_zero;

	return result;
}

static bool setup_distribution_points_pq(
		struct gamma_curve *arr_curve_points,
		struct curve_points *arr_points,
		uint32_t *hw_points_num,
		struct hw_x_point *coordinates_x,
		enum surface_pixel_format format)
{
	struct curve_config cfg;

	cfg.offset = 0;
	cfg.segments[0] = 2;
	cfg.segments[1] = 2;
	cfg.segments[2] = 2;
	cfg.segments[3] = 2;
	cfg.segments[4] = 2;
	cfg.segments[5] = 2;
	cfg.segments[6] = 3;
	cfg.segments[7] = 4;
	cfg.segments[8] = 4;
	cfg.segments[9] = 4;
	cfg.segments[10] = 4;
	cfg.segments[11] = 5;
	cfg.segments[12] = 5;
	cfg.segments[13] = 5;
	cfg.segments[14] = 5;
	cfg.segments[15] = 5;

	if (format == SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F ||
			format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F)
		cfg.begin = -11;
	else
		cfg.begin = -16;

	if (!build_hw_curve_configuration(
		&cfg, arr_curve_points,
		arr_points,
		coordinates_x, hw_points_num)) {
		ASSERT_CRITICAL(false);
		return false;
	}
	return true;
}

static bool setup_distribution_points(
		struct gamma_curve *arr_curve_points,
		struct curve_points *arr_points,
		uint32_t *hw_points_num,
		struct hw_x_point *coordinates_x)
{
	struct curve_config cfg;

	cfg.offset = 0;
	cfg.segments[0] = 3;
	cfg.segments[1] = 4;
	cfg.segments[2] = 4;
	cfg.segments[3] = 4;
	cfg.segments[4] = 4;
	cfg.segments[5] = 4;
	cfg.segments[6] = 4;
	cfg.segments[7] = 4;
	cfg.segments[8] = 5;
	cfg.segments[9] = 5;
	cfg.segments[10] = 0;
	cfg.segments[11] = -1;
	cfg.segments[12] = -1;
	cfg.segments[13] = -1;
	cfg.segments[14] = -1;
	cfg.segments[15] = -1;

	cfg.begin = -10;

	if (!build_hw_curve_configuration(
		&cfg, arr_curve_points,
		arr_points,
		coordinates_x, hw_points_num)) {
		ASSERT_CRITICAL(false);
		return false;
	}
	return true;
}

struct dividers {
	struct fixed31_32 divider1;
	struct fixed31_32 divider2;
	struct fixed31_32 divider3;
};

static void build_regamma_coefficients(struct gamma_coefficients *coefficients)
{
	/* sRGB should apply 2.4 */
	static const int32_t numerator01[3] = { 31308, 31308, 31308 };
	static const int32_t numerator02[3] = { 12920, 12920, 12920 };
	static const int32_t numerator03[3] = { 55, 55, 55 };
	static const int32_t numerator04[3] = { 55, 55, 55 };
	static const int32_t numerator05[3] = { 2400, 2400, 2400 };

	const int32_t *numerator1;
	const int32_t *numerator2;
	const int32_t *numerator3;
	const int32_t *numerator4;
	const int32_t *numerator5;

	uint32_t i = 0;

	numerator1 = numerator01;
	numerator2 = numerator02;
	numerator3 = numerator03;
	numerator4 = numerator04;
	numerator5 = numerator05;

	do {
		coefficients->a0[i] = dal_fixed31_32_from_fraction(
			numerator1[i], 10000000);
		coefficients->a1[i] = dal_fixed31_32_from_fraction(
			numerator2[i], 1000);
		coefficients->a2[i] = dal_fixed31_32_from_fraction(
			numerator3[i], 1000);
		coefficients->a3[i] = dal_fixed31_32_from_fraction(
			numerator4[i], 1000);
		coefficients->user_gamma[i] = dal_fixed31_32_from_fraction(
			numerator5[i], 1000);

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

static bool find_software_points(
	const struct gamma_pixel *axis_x_256,
	struct fixed31_32 hw_point,
	enum channel_name channel,
	uint32_t *index_to_start,
	uint32_t *index_left,
	uint32_t *index_right,
	enum hw_point_position *pos)
{
	const uint32_t max_number = RGB_256X3X16 + 3;

	struct fixed31_32 left, right;

	uint32_t i = *index_to_start;

	while (i < max_number) {
		if (channel == CHANNEL_NAME_RED) {
			left = axis_x_256[i].r;

			if (i < max_number - 1)
				right = axis_x_256[i + 1].r;
			else
				right = axis_x_256[max_number - 1].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			left = axis_x_256[i].g;

			if (i < max_number - 1)
				right = axis_x_256[i + 1].g;
			else
				right = axis_x_256[max_number - 1].g;
		} else {
			left = axis_x_256[i].b;

			if (i < max_number - 1)
				right = axis_x_256[i + 1].b;
			else
				right = axis_x_256[max_number - 1].b;
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
	struct pixel_gamma_point *coeff,
	const struct hw_x_point *coordinates_x,
	const struct gamma_pixel *axis_x_256,
	enum channel_name channel,
	uint32_t number_of_points,
	enum surface_pixel_format pixel_format)
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

		/*
		 * TODO: confirm enum in surface_pixel_format
		 * if (pixel_format == PIXEL_FORMAT_FP16)
		 *coord_x = coordinates_x[i].adjusted_x;
		 *else
		 */
		if (channel == CHANNEL_NAME_RED)
			coord_x = coordinates_x[i].regamma_y_red;
		else if (channel == CHANNEL_NAME_GREEN)
			coord_x = coordinates_x[i].regamma_y_green;
		else
			coord_x = coordinates_x[i].regamma_y_blue;

		if (!find_software_points(
			axis_x_256, coord_x, channel,
			&index_to_start, &index_left, &index_right, &hw_pos)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_left >= RGB_256X3X16 + 3) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (index_right >= RGB_256X3X16 + 3) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (channel == CHANNEL_NAME_RED) {
			point = &coeff[i].r;

			left_pos = axis_x_256[index_left].r;
			right_pos = axis_x_256[index_right].r;
		} else if (channel == CHANNEL_NAME_GREEN) {
			point = &coeff[i].g;

			left_pos = axis_x_256[index_left].g;
			right_pos = axis_x_256[index_right].g;
		} else {
			point = &coeff[i].b;

			left_pos = axis_x_256[index_left].b;
			right_pos = axis_x_256[index_right].b;
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

static inline bool build_oem_custom_gamma_mapping_coefficients(
	struct pixel_gamma_point *coeff128_oem,
	const struct hw_x_point *coordinates_x,
	const struct gamma_pixel *axis_x_256,
	uint32_t number_of_points,
	enum surface_pixel_format pixel_format)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!build_custom_gamma_mapping_coefficients_worker(
				coeff128_oem, coordinates_x, axis_x_256, i,
				number_of_points, pixel_format))
			return false;
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

static inline struct fixed31_32 calculate_oem_mapped_value(
	struct pwl_float_data *rgb_oem,
	const struct pixel_gamma_point *coeff,
	uint32_t index,
	enum channel_name channel,
	uint32_t max_index)
{
	return calculate_mapped_value(
			rgb_oem,
			coeff + index,
			channel,
			max_index);
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

static void build_regamma_curve_pq(struct pwl_float_data_ex *rgb_regamma,
		struct pwl_float_data *rgb_oem,
		struct pixel_gamma_point *coeff128_oem,
		const struct core_gamma *ramp,
		const struct core_surface *surface,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		const struct gamma_pixel *axis_x,
		struct dividers dividers)
{
	uint32_t i;

	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;
	struct fixed31_32 x;
	struct fixed31_32 output;
	struct fixed31_32 scaling_factor =
			dal_fixed31_32_from_fraction(8, 1000);

	/* use coord_x to retrieve coordinates chosen base on given user curve
	 * the x values are exponentially distributed and currently it is hard
	 * coded, the user curve shape is ignored. Need to recalculate coord_x
	 * based on input curve, translation from 256/1025 to 128 PWL points.
	 */
	for (i = 0; i <= hw_points_num; i++) {
		/* Multiply 0.008 as regamma is 0-1 and FP16 input is 0-125.
		 * FP 1.0 = 80nits
		 */
		x = dal_fixed31_32_mul(coord_x->adjusted_x, scaling_factor);

		compute_pq(x, &output);

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

static void build_regamma_curve(struct pwl_float_data_ex *rgb_regamma,
		struct pwl_float_data *rgb_oem,
		struct pixel_gamma_point *coeff128_oem,
		const struct core_gamma *ramp,
		const struct core_surface *surface,
		uint32_t hw_points_num,
		const struct hw_x_point *coordinate_x,
		const struct gamma_pixel *axis_x,
		struct dividers dividers)
{
	uint32_t i;

	struct gamma_coefficients coeff;
	struct pwl_float_data_ex *rgb = rgb_regamma;
	const struct hw_x_point *coord_x = coordinate_x;

	build_regamma_coefficients(&coeff);

	/* Use opp110->regamma.coordinates_x to retrieve
	 * coordinates chosen base on given user curve (future task).
	 * The x values are exponentially distributed and currently
	 * it is hard-coded, the user curve shape is ignored.
	 * The future task is to recalculate opp110-
	 * regamma.coordinates_x based on input/user curve,
	 * translation from 256/1025 to 128 pwl points.
	 */

	i = 0;

	while (i != hw_points_num + 1) {
		rgb->r = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 0);
		rgb->g = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 1);
		rgb->b = translate_from_linear_space_ex(
			coord_x->adjusted_x, &coeff, 2);

		++coord_x;
		++rgb;
		++i;
	}
}

static bool scale_gamma(struct pwl_float_data *pwl_rgb,
		const struct core_gamma *ramp,
		struct dividers dividers)
{
	const struct dc_gamma_ramp_rgb256x3x16 *gamma;
	const uint16_t max_driver = 0xFFFF;
	const uint16_t max_os = 0xFF00;
	uint16_t scaler = max_os;
	uint32_t i;
	struct pwl_float_data *rgb = pwl_rgb;
	struct pwl_float_data *rgb_last = rgb + RGB_256X3X16 - 1;

	if (ramp->public.type == GAMMA_RAMP_RBG256X3X16)
		gamma = &ramp->public.gamma_ramp_rgb256x3x16;
	else
		return false; /* invalid option */

	i = 0;

	do {
		if ((gamma->red[i] > max_os) ||
			(gamma->green[i] > max_os) ||
			(gamma->blue[i] > max_os)) {
			scaler = max_driver;
			break;
		}
		++i;
	} while (i != RGB_256X3X16);

	i = 0;

	do {
		rgb->r = dal_fixed31_32_from_fraction(
			gamma->red[i], scaler);
		rgb->g = dal_fixed31_32_from_fraction(
			gamma->green[i], scaler);
		rgb->b = dal_fixed31_32_from_fraction(
			gamma->blue[i], scaler);

		++rgb;
		++i;
	} while (i != RGB_256X3X16);

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

static void build_evenly_distributed_points(
	struct gamma_pixel *points,
	uint32_t numberof_points,
	struct fixed31_32 max_value,
	struct dividers dividers)
{
	struct gamma_pixel *p = points;
	struct gamma_pixel *p_last = p + numberof_points - 1;

	uint32_t i = 0;

	do {
		struct fixed31_32 value = dal_fixed31_32_div_int(
			dal_fixed31_32_mul_int(max_value, i),
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
	struct pwl_result_data *rgb,
	struct pixel_gamma_point *coeff128,
	struct pwl_float_data *rgb_user,
	const struct hw_x_point *coordinates_x,
	const struct gamma_pixel *axis_x_256,
	uint32_t number_of_points,
	enum surface_pixel_format pixel_format)
{

	const struct pixel_gamma_point *coeff;
	struct pixel_gamma_point *coeff_128 = coeff128;
	uint32_t max_entries = 3 - 1;
	struct pwl_result_data *rgb_resulted = rgb;

	uint32_t i = 0;

	if (!build_oem_custom_gamma_mapping_coefficients(
			coeff_128, coordinates_x, axis_x_256,
			number_of_points,
			pixel_format))
		return false;

	coeff = coeff128;
	max_entries += RGB_256X3X16;

	/* TODO: float point case */

	while (i <= number_of_points) {
		rgb_resulted->red = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_RED, max_entries);
		rgb_resulted->green = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_GREEN, max_entries);
		rgb_resulted->blue = calculate_mapped_value(
			rgb_user, coeff, CHANNEL_NAME_BLUE, max_entries);

		++coeff;
		++rgb_resulted;
		++i;
	}

	return true;
}

static bool map_regamma_hw_to_x_user(
	struct pixel_gamma_point *coeff128,
	struct pwl_float_data *rgb_oem,
	struct pwl_result_data *rgb_resulted,
	struct pwl_float_data *rgb_user,
	struct hw_x_point *coords_x,
	const struct gamma_pixel *axis_x,
	const struct dc_gamma *gamma,
	const struct pwl_float_data_ex *rgb_regamma,
	struct dividers dividers,
	uint32_t hw_points_num,
	const struct core_surface *surface)
{
	/* setup to spare calculated ideal regamma values */

	struct pixel_gamma_point *coeff = coeff128;

	struct hw_x_point *coords = coords_x;

	copy_rgb_regamma_to_coordinates_x(coords, hw_points_num, rgb_regamma);

	return calculate_interpolated_hardware_curve(
			rgb_resulted, coeff, rgb_user, coords, axis_x,
			hw_points_num, surface->public.format);
}

static void build_new_custom_resulted_curve(
	struct pwl_result_data *rgb_resulted,
	uint32_t hw_points_num)
{
	struct pwl_result_data *rgb = rgb_resulted;
	struct pwl_result_data *rgb_plus_1 = rgb + 1;

	uint32_t i;

	i = 0;

	while (i != hw_points_num + 1) {
		rgb->red = dal_fixed31_32_clamp(
			rgb->red, dal_fixed31_32_zero,
			dal_fixed31_32_one);
		rgb->green = dal_fixed31_32_clamp(
			rgb->green, dal_fixed31_32_zero,
			dal_fixed31_32_one);
		rgb->blue = dal_fixed31_32_clamp(
			rgb->blue, dal_fixed31_32_zero,
			dal_fixed31_32_one);

		++rgb;
		++i;
	}

	rgb = rgb_resulted;

	i = 1;

	while (i != hw_points_num + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dal_fixed31_32_sub(
			rgb_plus_1->red,
			rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(
			rgb_plus_1->green,
			rgb->green);
		rgb->delta_blue = dal_fixed31_32_sub(
			rgb_plus_1->blue,
			rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}
}

static void rebuild_curve_configuration_magic(
		struct curve_points *arr_points,
		struct pwl_result_data *rgb_resulted,
		const struct hw_x_point *coordinates_x,
		uint32_t hw_points_num)
{
	const struct fixed31_32 magic_number =
		dal_fixed31_32_from_fraction(249, 1000);

	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;

	struct fixed31_32 y1_min;
	struct fixed31_32 y2_max;
	struct fixed31_32 y3_max;

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	arr_points[0].x = coordinates_x[0].adjusted_x;
	arr_points[0].y = y1_min;
	arr_points[0].slope = dal_fixed31_32_div(
					arr_points[0].y,
					arr_points[0].x);

	arr_points[1].x = dal_fixed31_32_add(
			coordinates_x[hw_points_num - 1].adjusted_x,
			magic_number);

	arr_points[2].x = arr_points[1].x;

	y_r = rgb_resulted[hw_points_num - 1].red;
	y_g = rgb_resulted[hw_points_num - 1].green;
	y_b = rgb_resulted[hw_points_num - 1].blue;

	y2_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[1].y = y2_max;

	y_r = rgb_resulted[hw_points_num].red;
	y_g = rgb_resulted[hw_points_num].green;
	y_b = rgb_resulted[hw_points_num].blue;

	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[2].y = y3_max;

	arr_points[2].slope = dal_fixed31_32_one;
}

static bool convert_to_custom_float_format(
	struct fixed31_32 value,
	const struct custom_float_format *format,
	uint32_t *result)
{
	uint32_t mantissa;
	uint32_t exponenta;
	bool negative;

	return build_custom_float(
		value, format, &negative, &mantissa, &exponenta) &&
	setup_custom_float(
		format, negative, mantissa, exponenta, result);
}

static bool convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = true;

	if (!convert_to_custom_float_format(
		arr_points[0].x,
		&fmt,
		&arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].offset,
		&fmt,
		&arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].slope,
		&fmt,
		&arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[1].x,
		&fmt,
		&arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].y,
		&fmt,
		&arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[2].slope,
		&fmt,
		&arr_points[2].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_blue,
			&fmt,
			&rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}

bool calculate_regamma_params(struct pwl_params *params,
		const struct core_gamma *ramp,
		const struct core_surface *surface,
		const struct core_stream *stream)
{
	struct gamma_curve *arr_curve_points = params->arr_curve_points;
	struct curve_points *arr_points = params->arr_points;
	struct pwl_result_data *rgb_resulted = params->rgb_resulted;
	struct dividers dividers;

	struct hw_x_point *coordinates_x = NULL;
	struct pwl_float_data *rgb_user = NULL ;
	struct pwl_float_data_ex *rgb_regamma = NULL;
	struct pwl_float_data *rgb_oem = NULL;
	struct gamma_pixel *axix_x_256 = NULL;
	struct pixel_gamma_point *coeff128_oem = NULL;
	struct pixel_gamma_point *coeff128 = NULL;

	bool ret = false;

	coordinates_x = dm_alloc(sizeof(*coordinates_x)*(256 + 3));
	if (!coordinates_x)
		goto coordinates_x_alloc_fail;
	rgb_user = dm_alloc(sizeof(*rgb_user) * (FLOAT_GAMMA_RAMP_MAX + 3));
	if (!rgb_user)
		goto rgb_user_alloc_fail;
	rgb_regamma = dm_alloc(sizeof(*rgb_regamma) * (256 + 3));
	if (!rgb_regamma)
		goto rgb_regamma_alloc_fail;
	rgb_oem = dm_alloc(sizeof(*rgb_oem) * (FLOAT_GAMMA_RAMP_MAX + 3));
	if (!rgb_oem)
		goto rgb_oem_alloc_fail;
	axix_x_256 = dm_alloc(sizeof(*axix_x_256) * (256 + 3));
	if (!axix_x_256)
		goto axix_x_256_alloc_fail;
	coeff128_oem = dm_alloc(sizeof(*coeff128_oem) * (256 + 3));
	if (!coeff128_oem)
		goto coeff128_oem_alloc_fail;
	coeff128 = dm_alloc(sizeof(*coeff128) * (256 + 3));
	if (!coeff128)
		goto coeff128_alloc_fail;

	dividers.divider1 = dal_fixed31_32_from_fraction(3, 2);
	dividers.divider2 = dal_fixed31_32_from_int(2);
	dividers.divider3 = dal_fixed31_32_from_fraction(5, 2);

	build_evenly_distributed_points(
			axix_x_256,
			256,
			dal_fixed31_32_one,
			dividers);

	scale_gamma(rgb_user, ramp, dividers);

	if (stream->public.out_transfer_func &&
		stream->public.out_transfer_func->tf == TRANSFER_FUNCTION_PQ) {
		setup_distribution_points_pq(arr_curve_points, arr_points,
				&params->hw_points_num, coordinates_x,
				surface->public.format);
		build_regamma_curve_pq(rgb_regamma, rgb_oem, coeff128_oem,
				ramp, surface, params->hw_points_num,
				coordinates_x, axix_x_256, dividers);
	} else {
		setup_distribution_points(arr_curve_points, arr_points,
				&params->hw_points_num, coordinates_x);
		build_regamma_curve(rgb_regamma, rgb_oem, coeff128_oem,
				ramp, surface, params->hw_points_num,
				coordinates_x, axix_x_256, dividers);
	}

	map_regamma_hw_to_x_user(coeff128, rgb_oem, rgb_resulted, rgb_user,
			coordinates_x, axix_x_256, &ramp->public, rgb_regamma,
			dividers, params->hw_points_num, surface);

	build_new_custom_resulted_curve(rgb_resulted, params->hw_points_num);

	rebuild_curve_configuration_magic(
			arr_points,
			rgb_resulted,
			coordinates_x,
			params->hw_points_num);

	convert_to_custom_float(rgb_resulted, arr_points,
			params->hw_points_num);

	ret = true;

	dm_free(coeff128);
coeff128_alloc_fail:
	dm_free(coeff128_oem);
coeff128_oem_alloc_fail:
	dm_free(axix_x_256);
axix_x_256_alloc_fail:
	dm_free(rgb_oem);
rgb_oem_alloc_fail:
	dm_free(rgb_regamma);
rgb_regamma_alloc_fail:
	dm_free(rgb_user);
rgb_user_alloc_fail:
	dm_free(coordinates_x);
coordinates_x_alloc_fail:
	return ret;

}

