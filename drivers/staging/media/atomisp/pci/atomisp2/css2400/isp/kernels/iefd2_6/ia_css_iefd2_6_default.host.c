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

#include "ia_css_iefd2_6_types.h"

const struct ia_css_iefd2_6_config default_iefd2_6_config = {
	.horver_diag_coeff = 45,
	.ed_horver_diag_coeff = 64,
	.dir_smooth_enable = true,
	.dir_metric_update = 16,
	.unsharp_c00 = 60,
	.unsharp_c01 = 30,
	.unsharp_c02 = 16,
	.unsharp_c11 = 1,
	.unsharp_c12 = 2,
	.unsharp_c22 = 0,
	.unsharp_weight = 32,
	.unsharp_amount = 128,
	.cu_dir_sharp_pow = 20,
	.cu_dir_sharp_pow_bright = 20,
	.cu_non_dir_sharp_pow = 24,
	.cu_non_dir_sharp_pow_bright = 24,
	.dir_far_sharp_weight = 2,
	.rad_cu_dir_sharp_x1 = 0,
	.rad_cu_non_dir_sharp_x1 = 128,
	.rad_dir_far_sharp_weight = 8,
	.sharp_nega_lmt_txt = 1024,
	.sharp_posi_lmt_txt = 1024,
	.sharp_nega_lmt_dir = 128,
	.sharp_posi_lmt_dir = 128,
	.clamp_stitch = 0,
	.rad_enable = true,
	.rad_x_origin = 0,
	.rad_y_origin = 0,
	.rad_nf = 7,
	.rad_inv_r2 = 157,
	.vssnlm_enable = true,
	.vssnlm_x0 = 24,
	.vssnlm_x1 = 96,
	.vssnlm_x2 = 172,
	.vssnlm_y1 = 1,
	.vssnlm_y2 = 3,
	.vssnlm_y3 = 8,
	.cu_ed_points_x = {
		0,
		256,
		656,
		2456,
		3272,
		4095
		},
	.cu_ed_slopes_a = {
		4,
		160,
		0,
		0,
		0
		},
	.cu_ed_slopes_b = {
		0,
		9,
		510,
		511,
		511
		},
	.cu_ed2_points_x = {
		218,
		308
		},
	.cu_ed2_slopes_a = 11,
	.cu_ed2_slopes_b = 0,
	.cu_dir_sharp_points_x = {
		247,
		298,
		342,
		448
		},
	.cu_dir_sharp_slopes_a = {
		14,
		4,
		0
		},
	.cu_dir_sharp_slopes_b = {
		1,
		46,
		58
		},
	.cu_non_dir_sharp_points_x = {
		26,
		45,
		81,
		500
		},
	.cu_non_dir_sharp_slopes_a = {
		39,
		7,
		0
		},
	.cu_non_dir_sharp_slopes_b = {
		1,
		47,
		63
		},
	.cu_radial_points_x = {
		50,
		86,
		142,
		189,
		224,
		255
		},
	.cu_radial_slopes_a = {
		713,
		278,
		295,
		286,
		-1
		},
	.cu_radial_slopes_b = {
		1,
		101,
		162,
		216,
		255
		},
	.cu_vssnlm_points_x = {
		100,
		141
		},
	.cu_vssnlm_slopes_a = 25,
	.cu_vssnlm_slopes_b = 0
};

