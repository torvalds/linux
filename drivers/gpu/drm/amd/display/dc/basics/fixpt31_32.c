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
#include "include/fixed31_32.h"

static inline unsigned long long abs_i64(
	long long arg)
{
	if (arg > 0)
		return (unsigned long long)arg;
	else
		return (unsigned long long)(-arg);
}

/*
 * @brief
 * result = dividend / divisor
 * *remainder = dividend % divisor
 */
static inline unsigned long long complete_integer_division_u64(
	unsigned long long dividend,
	unsigned long long divisor,
	unsigned long long *remainder)
{
	unsigned long long result;

	ASSERT(divisor);

	result = div64_u64_rem(dividend, divisor, remainder);

	return result;
}


#define FRACTIONAL_PART_MASK \
	((1ULL << FIXED31_32_BITS_PER_FRACTIONAL_PART) - 1)

#define GET_INTEGER_PART(x) \
	((x) >> FIXED31_32_BITS_PER_FRACTIONAL_PART)

#define GET_FRACTIONAL_PART(x) \
	(FRACTIONAL_PART_MASK & (x))

struct fixed31_32 dal_fixed31_32_from_fraction(
	long long numerator,
	long long denominator)
{
	struct fixed31_32 res;

	bool arg1_negative = numerator < 0;
	bool arg2_negative = denominator < 0;

	unsigned long long arg1_value = arg1_negative ? -numerator : numerator;
	unsigned long long arg2_value = arg2_negative ? -denominator : denominator;

	unsigned long long remainder;

	/* determine integer part */

	unsigned long long res_value = complete_integer_division_u64(
		arg1_value, arg2_value, &remainder);

	ASSERT(res_value <= LONG_MAX);

	/* determine fractional part */
	{
		unsigned int i = FIXED31_32_BITS_PER_FRACTIONAL_PART;

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
		unsigned long long summand = (remainder << 1) >= arg2_value;

		ASSERT(res_value <= LLONG_MAX - summand);

		res_value += summand;
	}

	res.value = (long long)res_value;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;

	return res;
}

struct fixed31_32 dal_fixed31_32_from_int_nonconst(
	long long arg)
{
	struct fixed31_32 res;

	ASSERT((LONG_MIN <= arg) && (arg <= LONG_MAX));

	res.value = arg << FIXED31_32_BITS_PER_FRACTIONAL_PART;

	return res;
}

struct fixed31_32 dal_fixed31_32_shl(
	struct fixed31_32 arg,
	unsigned char shift)
{
	struct fixed31_32 res;

	ASSERT(((arg.value >= 0) && (arg.value <= LLONG_MAX >> shift)) ||
		((arg.value < 0) && (arg.value >= LLONG_MIN >> shift)));

	res.value = arg.value << shift;

	return res;
}

struct fixed31_32 dal_fixed31_32_add(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2)
{
	struct fixed31_32 res;

	ASSERT(((arg1.value >= 0) && (LLONG_MAX - arg1.value >= arg2.value)) ||
		((arg1.value < 0) && (LLONG_MIN - arg1.value <= arg2.value)));

	res.value = arg1.value + arg2.value;

	return res;
}

struct fixed31_32 dal_fixed31_32_sub(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2)
{
	struct fixed31_32 res;

	ASSERT(((arg2.value >= 0) && (LLONG_MIN + arg2.value <= arg1.value)) ||
		((arg2.value < 0) && (LLONG_MAX + arg2.value >= arg1.value)));

	res.value = arg1.value - arg2.value;

	return res;
}

struct fixed31_32 dal_fixed31_32_mul(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2)
{
	struct fixed31_32 res;

	bool arg1_negative = arg1.value < 0;
	bool arg2_negative = arg2.value < 0;

	unsigned long long arg1_value = arg1_negative ? -arg1.value : arg1.value;
	unsigned long long arg2_value = arg2_negative ? -arg2.value : arg2.value;

	unsigned long long arg1_int = GET_INTEGER_PART(arg1_value);
	unsigned long long arg2_int = GET_INTEGER_PART(arg2_value);

	unsigned long long arg1_fra = GET_FRACTIONAL_PART(arg1_value);
	unsigned long long arg2_fra = GET_FRACTIONAL_PART(arg2_value);

	unsigned long long tmp;

	res.value = arg1_int * arg2_int;

	ASSERT(res.value <= LONG_MAX);

	res.value <<= FIXED31_32_BITS_PER_FRACTIONAL_PART;

	tmp = arg1_int * arg2_fra;

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg2_int * arg1_fra;

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg1_fra * arg2_fra;

	tmp = (tmp >> FIXED31_32_BITS_PER_FRACTIONAL_PART) +
		(tmp >= (unsigned long long)dal_fixed31_32_half.value);

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;

	return res;
}

struct fixed31_32 dal_fixed31_32_sqr(
	struct fixed31_32 arg)
{
	struct fixed31_32 res;

	unsigned long long arg_value = abs_i64(arg.value);

	unsigned long long arg_int = GET_INTEGER_PART(arg_value);

	unsigned long long arg_fra = GET_FRACTIONAL_PART(arg_value);

	unsigned long long tmp;

	res.value = arg_int * arg_int;

	ASSERT(res.value <= LONG_MAX);

	res.value <<= FIXED31_32_BITS_PER_FRACTIONAL_PART;

	tmp = arg_int * arg_fra;

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg_fra * arg_fra;

	tmp = (tmp >> FIXED31_32_BITS_PER_FRACTIONAL_PART) +
		(tmp >= (unsigned long long)dal_fixed31_32_half.value);

	ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	return res;
}

struct fixed31_32 dal_fixed31_32_recip(
	struct fixed31_32 arg)
{
	/*
	 * @note
	 * Good idea to use Newton's method
	 */

	ASSERT(arg.value);

	return dal_fixed31_32_from_fraction(
		dal_fixed31_32_one.value,
		arg.value);
}

struct fixed31_32 dal_fixed31_32_sinc(
	struct fixed31_32 arg)
{
	struct fixed31_32 square;

	struct fixed31_32 res = dal_fixed31_32_one;

	int n = 27;

	struct fixed31_32 arg_norm = arg;

	if (dal_fixed31_32_le(
		dal_fixed31_32_two_pi,
		dal_fixed31_32_abs(arg))) {
		arg_norm = dal_fixed31_32_sub(
			arg_norm,
			dal_fixed31_32_mul_int(
				dal_fixed31_32_two_pi,
				(int)div64_s64(
					arg_norm.value,
					dal_fixed31_32_two_pi.value)));
	}

	square = dal_fixed31_32_sqr(arg_norm);

	do {
		res = dal_fixed31_32_sub(
			dal_fixed31_32_one,
			dal_fixed31_32_div_int(
				dal_fixed31_32_mul(
					square,
					res),
				n * (n - 1)));

		n -= 2;
	} while (n > 2);

	if (arg.value != arg_norm.value)
		res = dal_fixed31_32_div(
			dal_fixed31_32_mul(res, arg_norm),
			arg);

	return res;
}

struct fixed31_32 dal_fixed31_32_sin(
	struct fixed31_32 arg)
{
	return dal_fixed31_32_mul(
		arg,
		dal_fixed31_32_sinc(arg));
}

struct fixed31_32 dal_fixed31_32_cos(
	struct fixed31_32 arg)
{
	/* TODO implement argument normalization */

	const struct fixed31_32 square = dal_fixed31_32_sqr(arg);

	struct fixed31_32 res = dal_fixed31_32_one;

	int n = 26;

	do {
		res = dal_fixed31_32_sub(
			dal_fixed31_32_one,
			dal_fixed31_32_div_int(
				dal_fixed31_32_mul(
					square,
					res),
				n * (n - 1)));

		n -= 2;
	} while (n != 0);

	return res;
}

/*
 * @brief
 * result = exp(arg),
 * where abs(arg) < 1
 *
 * Calculated as Taylor series.
 */
static struct fixed31_32 fixed31_32_exp_from_taylor_series(
	struct fixed31_32 arg)
{
	unsigned int n = 9;

	struct fixed31_32 res = dal_fixed31_32_from_fraction(
		n + 2,
		n + 1);
	/* TODO find correct res */

	ASSERT(dal_fixed31_32_lt(arg, dal_fixed31_32_one));

	do
		res = dal_fixed31_32_add(
			dal_fixed31_32_one,
			dal_fixed31_32_div_int(
				dal_fixed31_32_mul(
					arg,
					res),
				n));
	while (--n != 1);

	return dal_fixed31_32_add(
		dal_fixed31_32_one,
		dal_fixed31_32_mul(
			arg,
			res));
}

struct fixed31_32 dal_fixed31_32_exp(
	struct fixed31_32 arg)
{
	/*
	 * @brief
	 * Main equation is:
	 * exp(x) = exp(r + m * ln(2)) = (1 << m) * exp(r),
	 * where m = round(x / ln(2)), r = x - m * ln(2)
	 */

	if (dal_fixed31_32_le(
		dal_fixed31_32_ln2_div_2,
		dal_fixed31_32_abs(arg))) {
		int m = dal_fixed31_32_round(
			dal_fixed31_32_div(
				arg,
				dal_fixed31_32_ln2));

		struct fixed31_32 r = dal_fixed31_32_sub(
			arg,
			dal_fixed31_32_mul_int(
				dal_fixed31_32_ln2,
				m));

		ASSERT(m != 0);

		ASSERT(dal_fixed31_32_lt(
			dal_fixed31_32_abs(r),
			dal_fixed31_32_one));

		if (m > 0)
			return dal_fixed31_32_shl(
				fixed31_32_exp_from_taylor_series(r),
				(unsigned char)m);
		else
			return dal_fixed31_32_div_int(
				fixed31_32_exp_from_taylor_series(r),
				1LL << -m);
	} else if (arg.value != 0)
		return fixed31_32_exp_from_taylor_series(arg);
	else
		return dal_fixed31_32_one;
}

struct fixed31_32 dal_fixed31_32_log(
	struct fixed31_32 arg)
{
	struct fixed31_32 res = dal_fixed31_32_neg(dal_fixed31_32_one);
	/* TODO improve 1st estimation */

	struct fixed31_32 error;

	ASSERT(arg.value > 0);
	/* TODO if arg is negative, return NaN */
	/* TODO if arg is zero, return -INF */

	do {
		struct fixed31_32 res1 = dal_fixed31_32_add(
			dal_fixed31_32_sub(
				res,
				dal_fixed31_32_one),
			dal_fixed31_32_div(
				arg,
				dal_fixed31_32_exp(res)));

		error = dal_fixed31_32_sub(
			res,
			res1);

		res = res1;
		/* TODO determine max_allowed_error based on quality of exp() */
	} while (abs_i64(error.value) > 100ULL);

	return res;
}

struct fixed31_32 dal_fixed31_32_pow(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2)
{
	return dal_fixed31_32_exp(
		dal_fixed31_32_mul(
			dal_fixed31_32_log(arg1),
			arg2));
}

int dal_fixed31_32_floor(
	struct fixed31_32 arg)
{
	unsigned long long arg_value = abs_i64(arg.value);

	if (arg.value >= 0)
		return (int)GET_INTEGER_PART(arg_value);
	else
		return -(int)GET_INTEGER_PART(arg_value);
}

int dal_fixed31_32_round(
	struct fixed31_32 arg)
{
	unsigned long long arg_value = abs_i64(arg.value);

	const long long summand = dal_fixed31_32_half.value;

	ASSERT(LLONG_MAX - (long long)arg_value >= summand);

	arg_value += summand;

	if (arg.value >= 0)
		return (int)GET_INTEGER_PART(arg_value);
	else
		return -(int)GET_INTEGER_PART(arg_value);
}

int dal_fixed31_32_ceil(
	struct fixed31_32 arg)
{
	unsigned long long arg_value = abs_i64(arg.value);

	const long long summand = dal_fixed31_32_one.value -
		dal_fixed31_32_epsilon.value;

	ASSERT(LLONG_MAX - (long long)arg_value >= summand);

	arg_value += summand;

	if (arg.value >= 0)
		return (int)GET_INTEGER_PART(arg_value);
	else
		return -(int)GET_INTEGER_PART(arg_value);
}

/* this function is a generic helper to translate fixed point value to
 * specified integer format that will consist of integer_bits integer part and
 * fractional_bits fractional part. For example it is used in
 * dal_fixed31_32_u2d19 to receive 2 bits integer part and 19 bits fractional
 * part in 32 bits. It is used in hw programming (scaler)
 */

static inline unsigned int ux_dy(
	long long value,
	unsigned int integer_bits,
	unsigned int fractional_bits)
{
	/* 1. create mask of integer part */
	unsigned int result = (1 << integer_bits) - 1;
	/* 2. mask out fractional part */
	unsigned int fractional_part = FRACTIONAL_PART_MASK & value;
	/* 3. shrink fixed point integer part to be of integer_bits width*/
	result &= GET_INTEGER_PART(value);
	/* 4. make space for fractional part to be filled in after integer */
	result <<= fractional_bits;
	/* 5. shrink fixed point fractional part to of fractional_bits width*/
	fractional_part >>= FIXED31_32_BITS_PER_FRACTIONAL_PART - fractional_bits;
	/* 6. merge the result */
	return result | fractional_part;
}

static inline unsigned int clamp_ux_dy(
	long long value,
	unsigned int integer_bits,
	unsigned int fractional_bits,
	unsigned int min_clamp)
{
	unsigned int truncated_val = ux_dy(value, integer_bits, fractional_bits);

	if (value >= (1LL << (integer_bits + FIXED31_32_BITS_PER_FRACTIONAL_PART)))
		return (1 << (integer_bits + fractional_bits)) - 1;
	else if (truncated_val > min_clamp)
		return truncated_val;
	else
		return min_clamp;
}

unsigned int dal_fixed31_32_u2d19(
	struct fixed31_32 arg)
{
	return ux_dy(arg.value, 2, 19);
}

unsigned int dal_fixed31_32_u0d19(
	struct fixed31_32 arg)
{
	return ux_dy(arg.value, 0, 19);
}

unsigned int dal_fixed31_32_clamp_u0d14(
	struct fixed31_32 arg)
{
	return clamp_ux_dy(arg.value, 0, 14, 1);
}

unsigned int dal_fixed31_32_clamp_u0d10(
	struct fixed31_32 arg)
{
	return clamp_ux_dy(arg.value, 0, 10, 1);
}

int dal_fixed31_32_s4d19(
	struct fixed31_32 arg)
{
	if (arg.value < 0)
		return -(int)ux_dy(dal_fixed31_32_abs(arg).value, 4, 19);
	else
		return ux_dy(arg.value, 4, 19);
}
