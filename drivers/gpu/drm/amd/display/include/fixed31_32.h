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

#ifndef __DAL_FIXED31_32_H__
#define __DAL_FIXED31_32_H__

#define FIXED31_32_BITS_PER_FRACTIONAL_PART 32

/*
 * @brief
 * Arithmetic operations on real numbers
 * represented as fixed-point numbers.
 * There are: 1 bit for sign,
 * 31 bit for integer part,
 * 32 bits for fractional part.
 *
 * @note
 * Currently, overflows and underflows are asserted;
 * no special result returned.
 */

struct fixed31_32 {
	long long value;
};

/*
 * @brief
 * Useful constants
 */

static const struct fixed31_32 dal_fixed31_32_zero = { 0 };
static const struct fixed31_32 dal_fixed31_32_epsilon = { 1LL };
static const struct fixed31_32 dal_fixed31_32_half = { 0x80000000LL };
static const struct fixed31_32 dal_fixed31_32_one = { 0x100000000LL };

static const struct fixed31_32 dal_fixed31_32_pi = { 13493037705LL };
static const struct fixed31_32 dal_fixed31_32_two_pi = { 26986075409LL };
static const struct fixed31_32 dal_fixed31_32_e = { 11674931555LL };
static const struct fixed31_32 dal_fixed31_32_ln2 = { 2977044471LL };
static const struct fixed31_32 dal_fixed31_32_ln2_div_2 = { 1488522236LL };

/*
 * @brief
 * Initialization routines
 */

/*
 * @brief
 * result = numerator / denominator
 */
struct fixed31_32 dal_fixed31_32_from_fraction(
	long long numerator,
	long long denominator);

/*
 * @brief
 * result = arg
 */
struct fixed31_32 dal_fixed31_32_from_int_nonconst(long long arg);
static inline struct fixed31_32 dal_fixed31_32_from_int(long long arg)
{
	if (__builtin_constant_p(arg)) {
		struct fixed31_32 res;
		BUILD_BUG_ON((LONG_MIN > arg) || (arg > LONG_MAX));
		res.value = arg << FIXED31_32_BITS_PER_FRACTIONAL_PART;
		return res;
	} else
		return dal_fixed31_32_from_int_nonconst(arg);
}

/*
 * @brief
 * Unary operators
 */

/*
 * @brief
 * result = -arg
 */
static inline struct fixed31_32 dal_fixed31_32_neg(struct fixed31_32 arg)
{
	struct fixed31_32 res;

	res.value = -arg.value;

	return res;
}

/*
 * @brief
 * result = abs(arg) := (arg >= 0) ? arg : -arg
 */
static inline struct fixed31_32 dal_fixed31_32_abs(struct fixed31_32 arg)
{
	if (arg.value < 0)
		return dal_fixed31_32_neg(arg);
	else
		return arg;
}

/*
 * @brief
 * Binary relational operators
 */

/*
 * @brief
 * result = arg1 < arg2
 */
static inline bool dal_fixed31_32_lt(struct fixed31_32 arg1,
				     struct fixed31_32 arg2)
{
	return arg1.value < arg2.value;
}

/*
 * @brief
 * result = arg1 <= arg2
 */
static inline bool dal_fixed31_32_le(struct fixed31_32 arg1,
				     struct fixed31_32 arg2)
{
	return arg1.value <= arg2.value;
}

/*
 * @brief
 * result = arg1 == arg2
 */
static inline bool dal_fixed31_32_eq(struct fixed31_32 arg1,
				     struct fixed31_32 arg2)
{
	return arg1.value == arg2.value;
}

/*
 * @brief
 * result = min(arg1, arg2) := (arg1 <= arg2) ? arg1 : arg2
 */
static inline struct fixed31_32 dal_fixed31_32_min(struct fixed31_32 arg1,
						   struct fixed31_32 arg2)
{
	if (arg1.value <= arg2.value)
		return arg1;
	else
		return arg2;
}

/*
 * @brief
 * result = max(arg1, arg2) := (arg1 <= arg2) ? arg2 : arg1
 */
static inline struct fixed31_32 dal_fixed31_32_max(struct fixed31_32 arg1,
						   struct fixed31_32 arg2)
{
	if (arg1.value <= arg2.value)
		return arg2;
	else
		return arg1;
}

/*
 * @brief
 *          | min_value, when arg <= min_value
 * result = | arg, when min_value < arg < max_value
 *          | max_value, when arg >= max_value
 */
static inline struct fixed31_32 dal_fixed31_32_clamp(
	struct fixed31_32 arg,
	struct fixed31_32 min_value,
	struct fixed31_32 max_value)
{
	if (dal_fixed31_32_le(arg, min_value))
		return min_value;
	else if (dal_fixed31_32_le(max_value, arg))
		return max_value;
	else
		return arg;
}

/*
 * @brief
 * Binary shift operators
 */

/*
 * @brief
 * result = arg << shift
 */
struct fixed31_32 dal_fixed31_32_shl(
	struct fixed31_32 arg,
	unsigned char shift);

/*
 * @brief
 * result = arg >> shift
 */
static inline struct fixed31_32 dal_fixed31_32_shr(
	struct fixed31_32 arg,
	unsigned char shift)
{
	struct fixed31_32 res;
	res.value = arg.value >> shift;
	return res;
}

/*
 * @brief
 * Binary additive operators
 */

/*
 * @brief
 * result = arg1 + arg2
 */
struct fixed31_32 dal_fixed31_32_add(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2);

/*
 * @brief
 * result = arg1 + arg2
 */
static inline struct fixed31_32 dal_fixed31_32_add_int(struct fixed31_32 arg1,
						       int arg2)
{
	return dal_fixed31_32_add(arg1,
				  dal_fixed31_32_from_int(arg2));
}

/*
 * @brief
 * result = arg1 - arg2
 */
struct fixed31_32 dal_fixed31_32_sub(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2);

/*
 * @brief
 * result = arg1 - arg2
 */
static inline struct fixed31_32 dal_fixed31_32_sub_int(struct fixed31_32 arg1,
						       int arg2)
{
	return dal_fixed31_32_sub(arg1,
				  dal_fixed31_32_from_int(arg2));
}


/*
 * @brief
 * Binary multiplicative operators
 */

/*
 * @brief
 * result = arg1 * arg2
 */
struct fixed31_32 dal_fixed31_32_mul(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2);


/*
 * @brief
 * result = arg1 * arg2
 */
static inline struct fixed31_32 dal_fixed31_32_mul_int(struct fixed31_32 arg1,
						       int arg2)
{
	return dal_fixed31_32_mul(arg1,
				  dal_fixed31_32_from_int(arg2));
}

/*
 * @brief
 * result = square(arg) := arg * arg
 */
struct fixed31_32 dal_fixed31_32_sqr(
	struct fixed31_32 arg);

/*
 * @brief
 * result = arg1 / arg2
 */
static inline struct fixed31_32 dal_fixed31_32_div_int(struct fixed31_32 arg1,
						       long long arg2)
{
	return dal_fixed31_32_from_fraction(arg1.value,
					    dal_fixed31_32_from_int(arg2).value);
}

/*
 * @brief
 * result = arg1 / arg2
 */
static inline struct fixed31_32 dal_fixed31_32_div(struct fixed31_32 arg1,
						   struct fixed31_32 arg2)
{
	return dal_fixed31_32_from_fraction(arg1.value,
					    arg2.value);
}

/*
 * @brief
 * Reciprocal function
 */

/*
 * @brief
 * result = reciprocal(arg) := 1 / arg
 *
 * @note
 * No special actions taken in case argument is zero.
 */
struct fixed31_32 dal_fixed31_32_recip(
	struct fixed31_32 arg);

/*
 * @brief
 * Trigonometric functions
 */

/*
 * @brief
 * result = sinc(arg) := sin(arg) / arg
 *
 * @note
 * Argument specified in radians,
 * internally it's normalized to [-2pi...2pi] range.
 */
struct fixed31_32 dal_fixed31_32_sinc(
	struct fixed31_32 arg);

/*
 * @brief
 * result = sin(arg)
 *
 * @note
 * Argument specified in radians,
 * internally it's normalized to [-2pi...2pi] range.
 */
struct fixed31_32 dal_fixed31_32_sin(
	struct fixed31_32 arg);

/*
 * @brief
 * result = cos(arg)
 *
 * @note
 * Argument specified in radians
 * and should be in [-2pi...2pi] range -
 * passing arguments outside that range
 * will cause incorrect result!
 */
struct fixed31_32 dal_fixed31_32_cos(
	struct fixed31_32 arg);

/*
 * @brief
 * Transcendent functions
 */

/*
 * @brief
 * result = exp(arg)
 *
 * @note
 * Currently, function is verified for abs(arg) <= 1.
 */
struct fixed31_32 dal_fixed31_32_exp(
	struct fixed31_32 arg);

/*
 * @brief
 * result = log(arg)
 *
 * @note
 * Currently, abs(arg) should be less than 1.
 * No normalization is done.
 * Currently, no special actions taken
 * in case of invalid argument(s). Take care!
 */
struct fixed31_32 dal_fixed31_32_log(
	struct fixed31_32 arg);

/*
 * @brief
 * Power function
 */

/*
 * @brief
 * result = pow(arg1, arg2)
 *
 * @note
 * Currently, abs(arg1) should be less than 1. Take care!
 */
struct fixed31_32 dal_fixed31_32_pow(
	struct fixed31_32 arg1,
	struct fixed31_32 arg2);

/*
 * @brief
 * Rounding functions
 */

/*
 * @brief
 * result = floor(arg) := greatest integer lower than or equal to arg
 */
int dal_fixed31_32_floor(
	struct fixed31_32 arg);

/*
 * @brief
 * result = round(arg) := integer nearest to arg
 */
int dal_fixed31_32_round(
	struct fixed31_32 arg);

/*
 * @brief
 * result = ceil(arg) := lowest integer greater than or equal to arg
 */
int dal_fixed31_32_ceil(
	struct fixed31_32 arg);

/* the following two function are used in scaler hw programming to convert fixed
 * point value to format 2 bits from integer part and 19 bits from fractional
 * part. The same applies for u0d19, 0 bits from integer part and 19 bits from
 * fractional
 */

unsigned int dal_fixed31_32_u2d19(
	struct fixed31_32 arg);

unsigned int dal_fixed31_32_u0d19(
	struct fixed31_32 arg);


unsigned int dal_fixed31_32_clamp_u0d14(
	struct fixed31_32 arg);

unsigned int dal_fixed31_32_clamp_u0d10(
	struct fixed31_32 arg);

int dal_fixed31_32_s4d19(
	struct fixed31_32 arg);

#endif
