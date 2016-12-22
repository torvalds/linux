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

#ifndef COLOR_MOD_COLOR_HELPER_H_
#define COLOR_MOD_COLOR_HELPER_H_

enum predefined_gamut_type {
	gamut_type_bt709,
	gamut_type_bt601,
	gamut_type_adobe_rgb,
	gamut_type_srgb,
	gamut_type_bt2020,
	gamut_type_unknown,
};

enum predefined_white_point_type {
	white_point_type_5000k_horizon,
	white_point_type_6500k_noon,
	white_point_type_7500k_north_sky,
	white_point_type_9300k,
	white_point_type_unknown,
};

bool mod_color_find_predefined_gamut(
		struct gamut_space_coordinates *out_gamut,
		enum predefined_gamut_type type);

bool mod_color_find_predefined_white_point(
		struct white_point_coodinates *out_white_point,
		enum predefined_white_point_type type);

bool mod_color_find_white_point_from_temperature(
		struct white_point_coodinates *out_white_point,
		unsigned int temperature);

bool mod_color_find_temperature_from_white_point(
		struct white_point_coodinates *in_white_point,
		unsigned int *out_temperature);

#endif /* COLOR_MOD_COLOR_HELPER_H_ */
