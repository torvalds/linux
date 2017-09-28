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

#include "dcn_calc_math.h"

float dcn_bw_floor2(const float arg, const float significance)
{
	if (significance == 0)
		return 0;
	return ((int) (arg / significance)) * significance;
}

float dcn_bw_ceil2(const float arg, const float significance)
{
	float flr = dcn_bw_floor2(arg, significance);
	if (significance == 0)
		return 0;
	return flr + 0.00001 >= arg ? arg : flr + significance;
}

float dcn_bw_pow(float a, float exp)
{
	float temp;
	/*ASSERT(exp == (int)exp);*/
	if ((int)exp == 0)
		return 1;
	temp = dcn_bw_pow(a, (int)(exp / 2));
	if (((int)exp % 2) == 0) {
		return temp * temp;
	} else {
		if ((int)exp > 0)
			return a * temp * temp;
		else
			return (temp * temp) / a;
	}
}

float dcn_bw_log(float a, float b)
{
	int * const exp_ptr = (int *)(&a);
	int x = *exp_ptr;
	const int log_2 = ((x >> 23) & 255) - 128;
	x &= ~(255 << 23);
	x += 127 << 23;
	*exp_ptr = x;

	a = ((-1.0f / 3) * a + 2) * a - 2.0f / 3;

	if (b > 2.00001 || b < 1.99999)
		return (a + log_2) / dcn_bw_log(b, 2);
	else
		return (a + log_2);
}
