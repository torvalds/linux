// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __LIB_FLOAT_MATH_H__
#define __LIB_FLOAT_MATH_H__

double math_mod(const double arg1, const double arg2);
double math_min2(const double arg1, const double arg2);
double math_max2(const double arg1, const double arg2);
double math_floor2(const double arg, const double significance);
double math_floor(const double arg);
double math_ceil(const double arg);
double math_ceil2(const double arg, const double significance);
double math_max3(double v1, double v2, double v3);
double math_max4(double v1, double v2, double v3, double v4);
double math_max5(double v1, double v2, double v3, double v4, double v5);
float math_pow(float a, float exp);
double math_fabs(double a);
float math_log(float a, float b);
float math_log2(float a);
unsigned int math_log2_approx(unsigned int a);
double math_round(double a);

#endif
