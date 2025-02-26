/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef __DML_INLINE_DEFS_H__
#define __DML_INLINE_DEFS_H__

#include "dcn_calc_math.h"
#include "dml_logger.h"

static inline double dml_min(double a, double b)
{
	return (double) dcn_bw_min2(a, b);
}

static inline double dml_min3(double a, double b, double c)
{
	return dml_min(dml_min(a, b), c);
}

static inline double dml_min4(double a, double b, double c, double d)
{
	return dml_min(dml_min(a, b), dml_min(c, d));
}

static inline double dml_max(double a, double b)
{
	return (double) dcn_bw_max2(a, b);
}

static inline double dml_max3(double a, double b, double c)
{
	return dml_max(dml_max(a, b), c);
}

static inline double dml_max4(double a, double b, double c, double d)
{
	return dml_max(dml_max(a, b), dml_max(c, d));
}

static inline double dml_max5(double a, double b, double c, double d, double e)
{
	return dml_max(dml_max4(a, b, c, d), e);
}

static inline double dml_ceil(double a, double granularity)
{
	if (granularity == 0)
		return 0;
	return (double) dcn_bw_ceil2(a, granularity);
}

static inline double dml_floor(double a, double granularity)
{
	if (granularity == 0)
		return 0;
	return (double) dcn_bw_floor2(a, granularity);
}

static inline double dml_round(double a)
{
	const double round_pt = 0.5;

	return dml_floor(a + round_pt, 1);
}

/* float
static inline int dml_log2(float x)
{
	unsigned int ix = *((unsigned int *)&x);

	return (int)((ix >> 23) & 0xff) - 127;
}*/

/* double */
static inline int dml_log2(double x)
{
	unsigned long long ix = *((unsigned long long *)&x);

	return (int)((ix >> 52) & 0x7ff) - 1023;
}

static inline double dml_pow(double a, int exp)
{
	return (double) dcn_bw_pow(a, exp);
}

static inline double dml_fmod(double f, int val)
{
	return (double) dcn_bw_mod(f, val);
}

static inline double dml_ceil_2(double f)
{
	return (double) dcn_bw_ceil2(f, 2);
}

static inline double dml_ceil_ex(double x, double granularity)
{
	if (granularity == 0)
		return 0;
	return (double) dcn_bw_ceil2(x, granularity);
}

static inline double dml_floor_ex(double x, double granularity)
{
	if (granularity == 0)
		return 0;
	return (double) dcn_bw_floor2(x, granularity);
}

static inline unsigned int dml_round_to_multiple(unsigned int num,
						 unsigned int multiple,
						 unsigned char up)
{
	unsigned int remainder;

	if (multiple == 0)
		return num;

	remainder = num % multiple;

	if (remainder == 0)
		return num;

	if (up)
		return (num + multiple - remainder);
	else
		return (num - remainder);
}
static inline double dml_abs(double a)
{
	if (a > 0)
		return a;
	else
		return (a*(-1));
}

#endif
