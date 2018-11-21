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

#ifndef COLOR_MOD_COLOR_GAMMA_H_
#define COLOR_MOD_COLOR_GAMMA_H_

struct dc_transfer_func;
struct dc_gamma;
struct dc_transfer_func_distributed_points;
struct dc_rgb_fixed;
enum dc_transfer_func_predefined;

void setup_x_points_distribution(void);
void precompute_pq(void);
void precompute_de_pq(void);

bool mod_color_calculate_regamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp);

bool mod_color_calculate_degamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp);

bool mod_color_calculate_curve(enum dc_transfer_func_predefined  trans,
		struct dc_transfer_func_distributed_points *points);

bool  mod_color_calculate_degamma_curve(enum dc_transfer_func_predefined trans,
				struct dc_transfer_func_distributed_points *points);



#endif /* COLOR_MOD_COLOR_GAMMA_H_ */
