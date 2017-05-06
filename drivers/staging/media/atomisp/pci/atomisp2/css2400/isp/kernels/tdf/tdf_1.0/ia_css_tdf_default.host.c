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

#include "ia_css_tdf_types.h"

const struct ia_css_tdf_config default_tdf_config = {
	.thres_flat_table = {0},
	.thres_detail_table = {0},
	.epsilon_0 = 4095,
	.epsilon_1 = 5733,
	.eps_scale_text = 409,
	.eps_scale_edge = 3686,
	.sepa_flat = 1294,
	.sepa_edge = 4095,
	.blend_flat = 819,
	.blend_text = 819,
	.blend_edge = 8191,
	.shading_gain = 1024,
	.shading_base_gain = 8191,
	.local_y_gain = 0,
	.local_y_base_gain = 2047,
	.rad_x_origin = 0,
	.rad_y_origin = 0
};

