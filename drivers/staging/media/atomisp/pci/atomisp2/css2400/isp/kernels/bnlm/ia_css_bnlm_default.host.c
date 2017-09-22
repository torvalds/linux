/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_bnlm_types.h"

const struct ia_css_bnlm_config default_bnlm_config = {

	.rad_enable = true,
	.rad_x_origin = 0,
	.rad_y_origin = 0,
	.avg_min_th = 127,
	.max_min_th = 2047,

	.exp_coeff_a = 6048,
	.exp_coeff_b = 7828,
	.exp_coeff_c = 0,
	.exp_exponent = 3,

	.nl_th = {2252, 2251, 2250},
	.match_quality_max_idx = {2, 3, 3, 1},

	.mu_root_lut_thr = {
		26, 56, 128, 216, 462, 626, 932, 1108, 1480, 1564, 1824, 1896, 2368, 3428, 4560},
	.mu_root_lut_val = {
		384, 320, 320, 264, 248, 240, 224, 192, 192, 160, 160, 160, 136, 130, 96, 80},
	.sad_norm_lut_thr = {
		236, 328, 470, 774, 964, 1486, 2294, 3244, 4844, 6524, 6524, 6524, 6524, 6524, 6524},
	.sad_norm_lut_val = {
		8064, 7680, 7168, 6144, 5120, 3840, 2560, 2304, 1984, 1792, 1792, 1792, 1792, 1792, 1792, 1792},
	.sig_detail_lut_thr = {
		2936, 3354, 3943, 4896, 5230, 5682, 5996, 7299, 7299, 7299, 7299, 7299, 7299, 7299, 7299},
	.sig_detail_lut_val = {
		8191, 7680, 7168, 6144, 5120, 4608, 4224, 4032, 4032, 4032, 4032, 4032, 4032, 4032, 4032, 4032},
	.sig_rad_lut_thr = {
		18, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20},
	.sig_rad_lut_val = {
		2560, 7168, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188, 8188},
	.rad_pow_lut_thr = {
		0, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013, 7013},
	.rad_pow_lut_val = {
		8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191},
	.nl_0_lut_thr = {
		1072, 7000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000},
	.nl_0_lut_val = {
		2560, 3072, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120, 5120},
	.nl_1_lut_thr = {
		624, 3224, 3392, 7424, 7424, 7424, 7424, 7424, 7424, 7424, 7424, 7424, 7424, 7424, 7424},
	.nl_1_lut_val = {
		3584, 4608, 5120, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144, 6144},
	.nl_2_lut_thr = {
		745, 2896, 3720, 6535, 7696, 8040, 8040, 8040, 8040, 8040, 8040, 8040, 8040, 8040, 8040},
	.nl_2_lut_val = {
		3584, 4608, 6144, 7168, 7936, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191},
	.nl_3_lut_thr = {
		4848, 4984, 5872, 6000, 6517, 6960, 7944, 8088, 8161, 8161, 8161, 8161, 8161, 8161, 8161},
	.nl_3_lut_val = {
		3072, 4104, 4608, 5120, 6144, 7168, 7680, 8128, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191},

};

