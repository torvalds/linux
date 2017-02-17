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

#ifndef __IA_CSS_PDAF_TYPES_H
#define __IA_CSS_PDAF_TYPES_H

#include "type_support.h"
#include "isp2600_config.h"
/*
 * Header file for PDAF parameters
 * These parameters shall be filled by host/driver
 * and will be converted to ISP parameters in encode
 * function.
 */

struct ia_css_statistics_calc_config {

	uint16_t num_valid_elm;
};
struct ia_css_pixel_grid_config {

	uint8_t num_valid_patterns;
	int16_t y_step_size[ISP_NWAY];
	int16_t y_offset[ISP_NWAY];
	int16_t x_step_size[ISP_NWAY];
	int16_t x_offset[ISP_NWAY];
};

struct ia_css_extraction_config {

	struct ia_css_pixel_grid_config l_pixel_grid;	/* Left PDAF pixel grid */
	struct ia_css_pixel_grid_config r_pixel_grid;	/* Right PDAF pixel grid */
};

struct ia_css_pdaf_config {

	uint16_t frm_length;
	uint16_t frm_width;
	struct ia_css_extraction_config ext_cfg_data;
	struct ia_css_statistics_calc_config stats_calc_cfg_data;
};

#endif /* __IA_CSS_PDAF_TYPES_H */
