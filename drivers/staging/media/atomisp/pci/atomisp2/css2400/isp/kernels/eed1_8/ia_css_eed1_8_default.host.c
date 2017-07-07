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

#include "ia_css_eed1_8_types.h"

/* The default values for the kernel parameters are based on
 * ISP261 CSS API public parameter list_all.xlsx from 12-09-2014
 * The parameter list is available on the ISP261 sharepoint
 */

/* Default kernel parameters. */
const struct ia_css_eed1_8_config default_eed1_8_config = {
	.rbzp_strength = 5489,
	.fcstrength = 6554,
	.fcthres_0 = 0,
	.fcthres_1 = 0,
	.fc_sat_coef = 8191,
	.fc_coring_prm = 128,
	.aerel_thres0 = 0,
	.aerel_gain0 = 8191,
	.aerel_thres1 = 16,
	.aerel_gain1 = 20,
	.derel_thres0 = 1229,
	.derel_gain0 = 1,
	.derel_thres1 = 819,
	.derel_gain1 = 1,
	.coring_pos0 = 0,
	.coring_pos1 = 0,
	.coring_neg0 = 0,
	.coring_neg1 = 0,
	.gain_exp = 2,
	.gain_pos0 = 6144,
	.gain_pos1 = 2048,
	.gain_neg0 = 2048,
	.gain_neg1 = 6144,
	.pos_margin0 = 1475,
	.pos_margin1 = 1475,
	.neg_margin0 = 1475,
	.neg_margin1 = 1475,
	.dew_enhance_seg_x = {
		0,
		64,
		272,
		688,
		1376,
		2400,
		3840,
		5744,
		8191
		},
	.dew_enhance_seg_y = {
		0,
		144,
		480,
		1040,
		1852,
		2945,
		4357,
		6094,
		8191
		},
	.dew_enhance_seg_slope = {
		4608,
		3308,
		2757,
		2417,
		2186,
		8033,
		7473,
		7020
		},
	.dew_enhance_seg_exp = {
		2,
		2,
		2,
		2,
		2,
		0,
		0,
		0
		},
	.dedgew_max = 6144
};
