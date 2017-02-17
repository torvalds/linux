#ifdef ISP2600
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

#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "sh_css_frac.h"
#include "ia_css_pdaf.host.h"

const struct ia_css_pdaf_config default_pdaf_config;

void
ia_css_pdaf_dmem_encode(
		struct isp_pdaf_dmem_params *to,
		const struct ia_css_pdaf_config *from,
		unsigned size)
{
	(void)size;
	to->frm_length = from->frm_length;
	to->frm_width  = from->frm_width;

	to->ext_cfg_l_data.num_valid_patterns = from->ext_cfg_data.l_pixel_grid.num_valid_patterns;

	to->ext_cfg_r_data.num_valid_patterns = from->ext_cfg_data.r_pixel_grid.num_valid_patterns;

	to->stats_calc_data.num_valid_elm = from->stats_calc_cfg_data.num_valid_elm;
}

void
ia_css_pdaf_vmem_encode(
		struct isp_pdaf_vmem_params *to,
		const struct ia_css_pdaf_config *from,
		unsigned size)
{

	unsigned int i;
	(void)size;
	/* Initialize left pixel grid */
	for ( i=0 ; i < from->ext_cfg_data.l_pixel_grid.num_valid_patterns ; i++) {

		to->ext_cfg_l_data.y_offset[0][i] = from->ext_cfg_data.l_pixel_grid.y_offset[i];
		to->ext_cfg_l_data.x_offset[0][i] = from->ext_cfg_data.l_pixel_grid.x_offset[i];
		to->ext_cfg_l_data.y_step_size[0][i] = from->ext_cfg_data.l_pixel_grid.y_step_size[i];
		to->ext_cfg_l_data.x_step_size[0][i] = from->ext_cfg_data.l_pixel_grid.x_step_size[i];
	}

	for ( ; i < ISP_NWAY ; i++) {

		to->ext_cfg_l_data.y_offset[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_l_data.x_offset[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_l_data.y_step_size[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_l_data.x_step_size[0][i] = PDAF_INVALID_VAL;
	}

	/* Initialize left pixel grid */

	for ( i=0 ; i < from->ext_cfg_data.r_pixel_grid.num_valid_patterns ; i++) {

		to->ext_cfg_r_data.y_offset[0][i] = from->ext_cfg_data.r_pixel_grid.y_offset[i];
		to->ext_cfg_r_data.x_offset[0][i] = from->ext_cfg_data.r_pixel_grid.x_offset[i];
		to->ext_cfg_r_data.y_step_size[0][i] = from->ext_cfg_data.r_pixel_grid.y_step_size[i];
		to->ext_cfg_r_data.x_step_size[0][i] = from->ext_cfg_data.r_pixel_grid.x_step_size[i];
	}

	for ( ; i < ISP_NWAY ; i++) {

		to->ext_cfg_r_data.y_offset[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_r_data.x_offset[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_r_data.y_step_size[0][i] = PDAF_INVALID_VAL;
		to->ext_cfg_r_data.x_step_size[0][i] = PDAF_INVALID_VAL;
	}
}
#endif
