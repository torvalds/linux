/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#define DIVIDER 10000

/* S2D13 value in [-3.00...0.9999] */
#define S2D13_MIN (-3 * DIVIDER)
#define S2D13_MAX (3 * DIVIDER)

uint16_t fixed_point_to_int_frac(
	struct fixed31_32 arg,
	uint8_t integer_bits,
	uint8_t fractional_bits)
{
	int32_t numerator;
	int32_t divisor = 1 << fractional_bits;

	uint16_t result;

	uint16_t d = (uint16_t)dal_fixed31_32_floor(
		dal_fixed31_32_abs(
			arg));

	if (d <= (uint16_t)(1 << integer_bits) - (1 / (uint16_t)divisor))
		numerator = (uint16_t)dal_fixed31_32_floor(
			dal_fixed31_32_mul_int(
				arg,
				divisor));
	else {
		numerator = dal_fixed31_32_floor(
			dal_fixed31_32_sub(
				dal_fixed31_32_from_int(
					1LL << integer_bits),
				dal_fixed31_32_recip(
					dal_fixed31_32_from_int(
						divisor))));
	}

	if (numerator >= 0)
		result = (uint16_t)numerator;
	else
		result = (uint16_t)(
		(1 << (integer_bits + fractional_bits + 1)) + numerator);

	if ((result != 0) && dal_fixed31_32_lt(
		arg, dal_fixed31_32_zero))
		result |= 1 << (integer_bits + fractional_bits);

	return result;
}
/**
* convert_float_matrix
* This converts a double into HW register spec defined format S2D13.
* @param :
* @return None
*/
void convert_float_matrix(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size)
{
	const struct fixed31_32 min_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MIN, DIVIDER);
	const struct fixed31_32 max_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MAX, DIVIDER);
	uint32_t i;

	for (i = 0; i < buffer_size; ++i) {
		uint32_t reg_value =
				fixed_point_to_int_frac(
					dal_fixed31_32_clamp(
						flt[i],
						min_2_13,
						max_2_13),
						2,
						13);

		matrix[i] = (uint16_t)reg_value;
	}
}

static void calculate_adjustments_common(
	const struct fixed31_32 *ideal_matrix,
	const struct dc_csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	const struct fixed31_32 sin_hue =
		dal_fixed31_32_sin(adjustments->hue);
	const struct fixed31_32 cos_hue =
		dal_fixed31_32_cos(adjustments->hue);

	const struct fixed31_32 multiplier =
		dal_fixed31_32_mul(
			adjustments->contrast,
			adjustments->saturation);

	matrix[0] = dal_fixed31_32_mul(
		ideal_matrix[0],
		adjustments->contrast);

	matrix[1] = dal_fixed31_32_mul(
		ideal_matrix[1],
		adjustments->contrast);

	matrix[2] = dal_fixed31_32_mul(
		ideal_matrix[2],
		adjustments->contrast);

	matrix[4] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[8],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[4],
				cos_hue)));

	matrix[5] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[9],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[5],
				cos_hue)));

	matrix[6] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_add(
			dal_fixed31_32_mul(
				ideal_matrix[10],
				sin_hue),
			dal_fixed31_32_mul(
				ideal_matrix[6],
				cos_hue)));

	matrix[7] = ideal_matrix[7];

	matrix[8] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[8],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[4],
				sin_hue)));

	matrix[9] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[9],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[5],
				sin_hue)));

	matrix[10] = dal_fixed31_32_mul(
		multiplier,
		dal_fixed31_32_sub(
			dal_fixed31_32_mul(
				ideal_matrix[10],
				cos_hue),
			dal_fixed31_32_mul(
				ideal_matrix[6],
				sin_hue)));

	matrix[11] = ideal_matrix[11];
}

void calculate_adjustments(
	const struct fixed31_32 *ideal_matrix,
	const struct dc_csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	calculate_adjustments_common(ideal_matrix, adjustments, matrix);

	matrix[3] = dal_fixed31_32_add(
		ideal_matrix[3],
		dal_fixed31_32_mul(
			adjustments->brightness,
			dal_fixed31_32_from_fraction(86, 100)));
}

void calculate_adjustments_y_only(
	const struct fixed31_32 *ideal_matrix,
	const struct dc_csc_adjustments *adjustments,
	struct fixed31_32 *matrix)
{
	calculate_adjustments_common(ideal_matrix, adjustments, matrix);

	matrix[3] = dal_fixed31_32_add(
		ideal_matrix[3],
		adjustments->brightness);
}

