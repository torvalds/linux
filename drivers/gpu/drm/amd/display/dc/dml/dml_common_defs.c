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

#include "dml_common_defs.h"
#include "../calcs/dcn_calc_math.h"

double dml_min(double a, double b)
{
	return (double) dcn_bw_min2(a, b);
}

double dml_max(double a, double b)
{
	return (double) dcn_bw_max2(a, b);
}

double dml_ceil(double a, double granularity)
{
	return (double) dcn_bw_ceil2(a, granularity);
}

double dml_floor(double a, double granularity)
{
	return (double) dcn_bw_floor2(a, granularity);
}

double dml_round(double a)
{
	double round_pt = 0.5;
	double ceil = dml_ceil(a, 1);
	double floor = dml_floor(a, 1);

	if (a - floor >= round_pt)
		return ceil;
	else
		return floor;
}

int dml_log2(double x)
{
	return dml_round((double)dcn_bw_log(x, 2));
}

double dml_pow(double a, int exp)
{
	return (double) dcn_bw_pow(a, exp);
}

unsigned int dml_round_to_multiple(
	unsigned int num,
	unsigned int multiple,
	bool up)
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

double dml_fmod(double f, int val)
{
	return (double) dcn_bw_mod(f, val);
}

double dml_ceil_2(double f)
{
	return (double) dcn_bw_ceil2(f, 2);
}

bool dml_util_is_420(enum source_format_class sorce_format)
{
	bool val = false;

	switch (sorce_format) {
	case dm_444_16:
		val = false;
		break;
	case dm_444_32:
		val = false;
		break;
	case dm_444_64:
		val = false;
		break;
	case dm_420_8:
		val = true;
		break;
	case dm_420_10:
		val = true;
		break;
	case dm_422_8:
		val = false;
		break;
	case dm_422_10:
		val = false;
		break;
	default:
		BREAK_TO_DEBUGGER();
	}

	return val;
}

double dml_ceil_ex(double x, double granularity)
{
	return (double) dcn_bw_ceil2(x, granularity);
}

double dml_floor_ex(double x, double granularity)
{
	return (double) dcn_bw_floor2(x, granularity);
}

double dml_log(double x, double base)
{
	return (double) dcn_bw_log(x, base);
}
