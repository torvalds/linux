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
#ifndef __DC_COMMON_DEFS_H__
#define __DC_COMMON_DEFS_H__

#include "dm_services.h"
#include "dc_features.h"
#include "display_mode_structs.h"
#include "display_mode_enums.h"

#define DTRACE(str, ...) dm_logger_write(mode_lib->logger, LOG_DML, str, ##__VA_ARGS__);

double dml_min(double a, double b);
double dml_max(double a, double b);
bool dml_util_is_420(enum source_format_class sorce_format);
double dml_ceil_ex(double x, double granularity);
double dml_floor_ex(double x, double granularity);
double dml_log(double x, double base);
double dml_ceil(double a);
double dml_floor(double a);
double dml_round(double a);
int dml_log2(double x);
double dml_pow(double a, int exp);
unsigned int dml_round_to_multiple(
			unsigned int num, unsigned int multiple, bool up);
double dml_fmod(double f, int val);
double dml_ceil_2(double f);

#endif /* __DC_COMMON_DEFS_H__ */
