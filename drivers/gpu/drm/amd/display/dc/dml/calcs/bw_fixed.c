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
#include "bw_fixed.h"

#define MAX_I64 \
	((int64_t)((1ULL << 63) - 1))

#define MIN_I64 \
	(-MAX_I64 - 1)

#define FRACTIONAL_PART_MASK \
	((1ULL << BW_FIXED_BITS_PER_FRACTIONAL_PART) - 1)

#define GET_FRACTIONAL_PART(x) \
	(FRACTIONAL_PART_MASK & (x))

static uint64_t abs_i64(int64_t arg)
{
	if (arg >= 0)
		return (uint64_t)(arg);
	else
		return (uint64_t)(-arg);
}

struct bw_fixed bw_int_to_fixed_nonconst(int64_t value)
{
	struct bw_fixed res;

	ASSERT(value < BW_FIXED_MAX_I32 && value > BW_FIXED_MIN_I32);
	res.value = value << BW_FIXED_BITS_PER_FRACTIONAL_PART;
	return res;
}

struct bw_fixed bw_frc_to_fixed(int64_t numerator, int64_t denominator)
{
	struct bw_fixed res;
	bool arg1_negative = numerator < 0;
	bool arg2_negative = denominator < 0;
	uint64_t arg1_value;
	uint64_t arg2_value;
	uint64_t remainder;

	/* determine integer part */
	uint64_t res_value;

	ASSERT(denominator != 0);

	arg1_value = abs_i64(numerator);
	arg2_value = abs_i64(denominator);
	res_value = div64_u64_rem(arg1_value, arg2_value, &remainder);

	ASSERT(res_value <= BW_FIXED_MAX_I32);

	/* determine fractional part */
	{
		uint32_t i = BW_FIXED_BITS_PER_FRACTIONAL_PART;

		do {
			remainder <<= 1;

			res_value <<= 1;

			if (remainder >= arg2_value) {
				res_value |= 1;
				remainder -= arg2_value;
			}
		} while (--i != 0);
	}

	/* round up LSB */
	{
		uint64_t summand = (remainder << 1) >= arg2_value;

		ASSERT(res_value <= MAX_I64 - summand);

		res_value += summand;
	}

	res.value = (int64_t)(res_value);

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;
	return res;
}

struct bw_fixed bw_floor2(
	const struct bw_fixed arg,
	const struct bw_fixed significance)
{
	struct bw_fixed result;
	int64_t multiplicand;

	multiplicand = div64_s64(arg.value, abs_i64(significance.value));
	result.value = abs_i64(significance.value) * multiplicand;
	ASSERT(abs_i64(result.value) <= abs_i64(arg.value));
	return result;
}

struct bw_fixed bw_ceil2(
	const struct bw_fixed arg,
	const struct bw_fixed significance)
{
	struct bw_fixed result;
	int64_t multiplicand;

	multiplicand = div64_s64(arg.value, abs_i64(significance.value));
	result.value = abs_i64(significance.value) * multiplicand;
	if (abs_i64(result.value) < abs_i64(arg.value)) {
		if (arg.value < 0)
			result.value -= abs_i64(significance.value);
		else
			result.value += abs_i64(significance.value);
	}
	return result;
}

struct bw_fixed bw_mul(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	struct bw_fixed res;

	bool arg1_negative = arg1.value < 0;
	bool arg2_negative = arg2.value < 0;

	uint64_t arg1_value = abs_i64(arg1.value);
	uint64_t arg2_value = abs_i64(arg2.value);

	uint64_t arg1_int = BW_FIXED_GET_INTEGER_PART(arg1_value);
	uint64_t arg2_int = BW_FIXED_GET_INTEGER_PART(arg2_value);

	uint64_t arg1_fra = GET_FRACTIONAL_PART(arg1_value);
	uint64_t arg2_fra = GET_FRACTIONAL_PART(arg2_value);

	uint64_t tmp;

	res.value = arg1_int * arg2_int;

	ASSERT(res.value <= BW_FIXED_MAX_I32);

	res.value <<= BW_FIXED_BITS_PER_FRACTIONAL_PART;

	tmp = arg1_int * arg2_fra;

	ASSERT(tmp <= (uint64_t)(MAX_I64 - res.value));

	res.value += tmp;

	tmp = arg2_int * arg1_fra;

	ASSERT(tmp <= (uint64_t)(MAX_I64 - res.value));

	res.value += tmp;

	tmp = arg1_fra * arg2_fra;

	tmp = (tmp >> BW_FIXED_BITS_PER_FRACTIONAL_PART) +
		(tmp >= (uint64_t)(bw_frc_to_fixed(1, 2).value));

	ASSERT(tmp <= (uint64_t)(MAX_I64 - res.value));

	res.value += tmp;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;
	return res;
}

