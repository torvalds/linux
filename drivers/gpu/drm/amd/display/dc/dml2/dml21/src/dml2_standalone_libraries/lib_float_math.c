// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "lib_float_math.h"

#define ASSERT(condition)

#define isNaN(number) ((number) != (number))

 /*
  * NOTE:
  *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
  *
  * It doesn't adhere to Linux kernel style and sometimes will do things in odd
  * ways. Unless there is something clearly wrong with it the code should
  * remain as-is as it provides us with a guarantee from HW that it is correct.
  */

double math_mod(const double arg1, const double arg2)
{
	if (isNaN(arg1))
		return arg2;
	if (isNaN(arg2))
		return arg1;
	return arg1 - arg1 * ((int)(arg1 / arg2));
}

double math_min2(const double arg1, const double arg2)
{
	if (isNaN(arg1))
		return arg2;
	if (isNaN(arg2))
		return arg1;
	return arg1 < arg2 ? arg1 : arg2;
}

double math_max2(const double arg1, const double arg2)
{
	if (isNaN(arg1))
		return arg2;
	if (isNaN(arg2))
		return arg1;
	return arg1 > arg2 ? arg1 : arg2;
}

double math_floor2(const double arg, const double significance)
{
	ASSERT(significance != 0);

	return ((int)(arg / significance)) * significance;
}

double math_floor(const double arg)
{
	return ((int)(arg));
}

double math_ceil(const double arg)
{
	return (int)(arg + 0.99999);
}

double math_ceil2(const double arg, const double significance)
{
	return ((int)(arg / significance + 0.99999)) * significance;
}

double math_max3(double v1, double v2, double v3)
{
	return v3 > math_max2(v1, v2) ? v3 : math_max2(v1, v2);
}

double math_max4(double v1, double v2, double v3, double v4)
{
	return v4 > math_max3(v1, v2, v3) ? v4 : math_max3(v1, v2, v3);
}

double math_max5(double v1, double v2, double v3, double v4, double v5)
{
	return math_max3(v1, v2, v3) > math_max2(v4, v5) ? math_max3(v1, v2, v3) : math_max2(v4, v5);
}

float math_pow(float a, float exp)
{
	double temp;
	if ((int)exp == 0)
		return 1;
	temp = math_pow(a, (float)((int)(exp / 2)));
	if (((int)exp % 2) == 0) {
		return (float)(temp * temp);
	} else {
		if ((int)exp > 0)
			return (float)(a * temp * temp);
		else
			return (float)((temp * temp) / a);
	}
}

double math_fabs(double a)
{
	if (a > 0)
		return (a);
	else
		return (-a);
}

float math_log(float a, float b)
{
	int *const exp_ptr = (int *)(&a);
	int x = *exp_ptr;
	const int log_2 = ((x >> 23) & 255) - 128;
	x &= ~(255 << 23);
	x += 127 << 23;
	*exp_ptr = x;

	a = ((-1.0f / 3) * a + 2) * a - 2.0f / 3;

	if (b > 2.00001 || b < 1.99999)
		return (a + log_2) / math_log(b, 2);
	else
		return (a + log_2);
}

float math_log2(float a)
{
	return math_log(a, 2.0);
}

// approximate log2 value of a input
//  - precise if the input pwr of 2, else the approximation will be an integer = floor(actual_log2)
unsigned int math_log2_approx(unsigned int a)
{
	unsigned int log2_val = 0;
	while (a > 1) {
		a = a >> 1;
		log2_val++;
	}
	return log2_val;
}

double math_round(double a)
{
	const double round_pt = 0.5;

	return math_floor(a + round_pt);
}
