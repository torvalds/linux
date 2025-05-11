// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "spl_fixpt31_32.h"

static const struct spl_fixed31_32 spl_fixpt_two_pi = { 26986075409LL };
static const struct spl_fixed31_32 spl_fixpt_ln2 = { 2977044471LL };
static const struct spl_fixed31_32 spl_fixpt_ln2_div_2 = { 1488522236LL };

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
static inline unsigned long long spl_complete_integer_division_u64(
	unsigned long long dividend,
	unsigned long long divisor,
	unsigned long long *remainder)
{
	unsigned long long result;

	result = spl_div64_u64_rem(dividend, divisor, remainder);

	return result;
}


#define FRACTIONAL_PART_MASK \
	((1ULL << FIXED31_32_BITS_PER_FRACTIONAL_PART) - 1)

#define GET_INTEGER_PART(x) \
	((x) >> FIXED31_32_BITS_PER_FRACTIONAL_PART)

#define GET_FRACTIONAL_PART(x) \
	(FRACTIONAL_PART_MASK & (x))

struct spl_fixed31_32 spl_fixpt_from_fraction(long long numerator, long long denominator)
{
	struct spl_fixed31_32 res;

	bool arg1_negative = numerator < 0;
	bool arg2_negative = denominator < 0;

	unsigned long long arg1_value = arg1_negative ? -numerator : numerator;
	unsigned long long arg2_value = arg2_negative ? -denominator : denominator;

	unsigned long long remainder;

	/* determine integer part */

	unsigned long long res_value = spl_complete_integer_division_u64(
		arg1_value, arg2_value, &remainder);

	SPL_ASSERT(res_value <= (unsigned long long)LONG_MAX);

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

		SPL_ASSERT(res_value <= (unsigned long long)LLONG_MAX - summand);

		res_value += summand;
	}

	res.value = (long long)res_value;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;

	return res;
}

struct spl_fixed31_32 spl_fixpt_mul(struct spl_fixed31_32 arg1, struct spl_fixed31_32 arg2)
{
	struct spl_fixed31_32 res;

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

	SPL_ASSERT(res.value <= (long long)LONG_MAX);

	res.value <<= FIXED31_32_BITS_PER_FRACTIONAL_PART;

	tmp = arg1_int * arg2_fra;

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg2_int * arg1_fra;

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg1_fra * arg2_fra;

	tmp = (tmp >> FIXED31_32_BITS_PER_FRACTIONAL_PART) +
		(tmp >= (unsigned long long)spl_fixpt_half.value);

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;

	return res;
}

struct spl_fixed31_32 spl_fixpt_sqr(struct spl_fixed31_32 arg)
{
	struct spl_fixed31_32 res;

	unsigned long long arg_value = abs_i64(arg.value);

	unsigned long long arg_int = GET_INTEGER_PART(arg_value);

	unsigned long long arg_fra = GET_FRACTIONAL_PART(arg_value);

	unsigned long long tmp;

	res.value = arg_int * arg_int;

	SPL_ASSERT(res.value <= (long long)LONG_MAX);

	res.value <<= FIXED31_32_BITS_PER_FRACTIONAL_PART;

	tmp = arg_int * arg_fra;

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	tmp = arg_fra * arg_fra;

	tmp = (tmp >> FIXED31_32_BITS_PER_FRACTIONAL_PART) +
		(tmp >= (unsigned long long)spl_fixpt_half.value);

	SPL_ASSERT(tmp <= (unsigned long long)(LLONG_MAX - res.value));

	res.value += tmp;

	return res;
}

struct spl_fixed31_32 spl_fixpt_recip(struct spl_fixed31_32 arg)
{
	/*
	 * @note
	 * Good idea to use Newton's method
	 */

	return spl_fixpt_from_fraction(
		spl_fixpt_one.value,
		arg.value);
}

struct spl_fixed31_32 spl_fixpt_sinc(struct spl_fixed31_32 arg)
{
	struct spl_fixed31_32 square;

	struct spl_fixed31_32 res = spl_fixpt_one;

	int n = 27;

	struct spl_fixed31_32 arg_norm = arg;

	if (spl_fixpt_le(
		spl_fixpt_two_pi,
		spl_fixpt_abs(arg))) {
		arg_norm = spl_fixpt_sub(
			arg_norm,
			spl_fixpt_mul_int(
				spl_fixpt_two_pi,
				(int)spl_div64_s64(
					arg_norm.value,
					spl_fixpt_two_pi.value)));
	}

	square = spl_fixpt_sqr(arg_norm);

	do {
		res = spl_fixpt_sub(
			spl_fixpt_one,
			spl_fixpt_div_int(
				spl_fixpt_mul(
					square,
					res),
				n * (n - 1)));

		n -= 2;
	} while (n > 2);

	if (arg.value != arg_norm.value)
		res = spl_fixpt_div(
			spl_fixpt_mul(res, arg_norm),
			arg);

	return res;
}

struct spl_fixed31_32 spl_fixpt_sin(struct spl_fixed31_32 arg)
{
	return spl_fixpt_mul(
		arg,
		spl_fixpt_sinc(arg));
}

struct spl_fixed31_32 spl_fixpt_cos(struct spl_fixed31_32 arg)
{
	/* TODO implement argument normalization */

	const struct spl_fixed31_32 square = spl_fixpt_sqr(arg);

	struct spl_fixed31_32 res = spl_fixpt_one;

	int n = 26;

	do {
		res = spl_fixpt_sub(
			spl_fixpt_one,
			spl_fixpt_div_int(
				spl_fixpt_mul(
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
static struct spl_fixed31_32 spl_fixed31_32_exp_from_taylor_series(struct spl_fixed31_32 arg)
{
	unsigned int n = 9;

	struct spl_fixed31_32 res = spl_fixpt_from_fraction(
		n + 2,
		n + 1);
	/* TODO find correct res */

	SPL_ASSERT(spl_fixpt_lt(arg, spl_fixpt_one));

	do
		res = spl_fixpt_add(
			spl_fixpt_one,
			spl_fixpt_div_int(
				spl_fixpt_mul(
					arg,
					res),
				n));
	while (--n != 1);

	return spl_fixpt_add(
		spl_fixpt_one,
		spl_fixpt_mul(
			arg,
			res));
}

struct spl_fixed31_32 spl_fixpt_exp(struct spl_fixed31_32 arg)
{
	/*
	 * @brief
	 * Main equation is:
	 * exp(x) = exp(r + m * ln(2)) = (1 << m) * exp(r),
	 * where m = round(x / ln(2)), r = x - m * ln(2)
	 */

	if (spl_fixpt_le(
		spl_fixpt_ln2_div_2,
		spl_fixpt_abs(arg))) {
		int m = spl_fixpt_round(
			spl_fixpt_div(
				arg,
				spl_fixpt_ln2));

		struct spl_fixed31_32 r = spl_fixpt_sub(
			arg,
			spl_fixpt_mul_int(
				spl_fixpt_ln2,
				m));

		SPL_ASSERT(m != 0);

		SPL_ASSERT(spl_fixpt_lt(
			spl_fixpt_abs(r),
			spl_fixpt_one));

		if (m > 0)
			return spl_fixpt_shl(
				spl_fixed31_32_exp_from_taylor_series(r),
				(unsigned int)m);
		else
			return spl_fixpt_div_int(
				spl_fixed31_32_exp_from_taylor_series(r),
				1LL << -m);
	} else if (arg.value != 0)
		return spl_fixed31_32_exp_from_taylor_series(arg);
	else
		return spl_fixpt_one;
}

struct spl_fixed31_32 spl_fixpt_log(struct spl_fixed31_32 arg)
{
	struct spl_fixed31_32 res = spl_fixpt_neg(spl_fixpt_one);
	/* TODO improve 1st estimation */

	struct spl_fixed31_32 error;

	SPL_ASSERT(arg.value > 0);
	/* TODO if arg is negative, return NaN */
	/* TODO if arg is zero, return -INF */

	do {
		struct spl_fixed31_32 res1 = spl_fixpt_add(
			spl_fixpt_sub(
				res,
				spl_fixpt_one),
			spl_fixpt_div(
				arg,
				spl_fixpt_exp(res)));

		error = spl_fixpt_sub(
			res,
			res1);

		res = res1;
		/* TODO determine max_allowed_error based on quality of exp() */
	} while (abs_i64(error.value) > 100ULL);

	return res;
}


/* this function is a generic helper to translate fixed point value to
 * specified integer format that will consist of integer_bits integer part and
 * fractional_bits fractional part. For example it is used in
 * spl_fixpt_u2d19 to receive 2 bits integer part and 19 bits fractional
 * part in 32 bits. It is used in hw programming (scaler)
 */

static inline unsigned int spl_ux_dy(
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

static inline unsigned int spl_clamp_ux_dy(
	long long value,
	unsigned int integer_bits,
	unsigned int fractional_bits,
	unsigned int min_clamp)
{
	unsigned int truncated_val = spl_ux_dy(value, integer_bits, fractional_bits);

	if (value >= (1LL << (integer_bits + FIXED31_32_BITS_PER_FRACTIONAL_PART)))
		return (1 << (integer_bits + fractional_bits)) - 1;
	else if (truncated_val > min_clamp)
		return truncated_val;
	else
		return min_clamp;
}

unsigned int spl_fixpt_u4d19(struct spl_fixed31_32 arg)
{
	return spl_ux_dy(arg.value, 4, 19);
}

unsigned int spl_fixpt_u3d19(struct spl_fixed31_32 arg)
{
	return spl_ux_dy(arg.value, 3, 19);
}

unsigned int spl_fixpt_u2d19(struct spl_fixed31_32 arg)
{
	return spl_ux_dy(arg.value, 2, 19);
}

unsigned int spl_fixpt_u0d19(struct spl_fixed31_32 arg)
{
	return spl_ux_dy(arg.value, 0, 19);
}

unsigned int spl_fixpt_clamp_u0d14(struct spl_fixed31_32 arg)
{
	return spl_clamp_ux_dy(arg.value, 0, 14, 1);
}

unsigned int spl_fixpt_clamp_u0d10(struct spl_fixed31_32 arg)
{
	return spl_clamp_ux_dy(arg.value, 0, 10, 1);
}

int spl_fixpt_s4d19(struct spl_fixed31_32 arg)
{
	if (arg.value < 0)
		return -(int)spl_ux_dy(spl_fixpt_abs(arg).value, 4, 19);
	else
		return spl_ux_dy(arg.value, 4, 19);
}

struct spl_fixed31_32 spl_fixpt_from_ux_dy(unsigned int value,
	unsigned int integer_bits,
	unsigned int fractional_bits)
{
	struct spl_fixed31_32 fixpt_value = spl_fixpt_zero;
	struct spl_fixed31_32 fixpt_int_value = spl_fixpt_zero;
	long long frac_mask = ((long long)1 << (long long)integer_bits) - 1;

	fixpt_value.value = (long long)value << (FIXED31_32_BITS_PER_FRACTIONAL_PART - fractional_bits);
	frac_mask = frac_mask << fractional_bits;
	fixpt_int_value.value = value & frac_mask;
	fixpt_int_value.value <<= (FIXED31_32_BITS_PER_FRACTIONAL_PART - fractional_bits);
	fixpt_value.value |= fixpt_int_value.value;
	return fixpt_value;
}

struct spl_fixed31_32 spl_fixpt_from_int_dy(unsigned int int_value,
	unsigned int frac_value,
	unsigned int integer_bits,
	unsigned int fractional_bits)
{
	struct spl_fixed31_32 fixpt_value = spl_fixpt_from_int(int_value);

	fixpt_value.value |= (long long)frac_value << (FIXED31_32_BITS_PER_FRACTIONAL_PART - fractional_bits);
	return fixpt_value;
}
