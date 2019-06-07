/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#ifndef LUTS_1D_H
#define LUTS_1D_H

#include "hw_shared.h"

struct point_config {
	uint32_t custom_float_x;
	uint32_t custom_float_y;
	uint32_t custom_float_slope;
};

struct lut_point {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t delta_red;
	uint32_t delta_green;
	uint32_t delta_blue;
};

struct pwl_1dlut_parameter {
	struct gamma_curve	arr_curve_points[34];
	struct point_config	arr_points[2];
	struct lut_point rgb_resulted[256];
	uint32_t hw_points_num;
};
#endif // LUTS_1D_H
