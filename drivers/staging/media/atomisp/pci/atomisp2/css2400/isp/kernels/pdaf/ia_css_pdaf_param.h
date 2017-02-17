#ifndef ISP2401
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

#ifndef __IA_CSS_PDAF_PARAM_H
#define __IA_CSS_PDAF_PARAM_H

#define PDAF_INVALID_VAL 0x7FFF
#include "vmem.h"

struct isp_stats_calc_dmem_params {

	uint16_t num_valid_elm;
};
/*
 * Extraction configuration parameters
 */

struct isp_extraction_dmem_params {

	uint8_t num_valid_patterns;
};

struct isp_extraction_vmem_params {

	VMEM_ARRAY(y_step_size, ISP_VEC_NELEMS);
	VMEM_ARRAY(y_offset, ISP_VEC_NELEMS);
	VMEM_ARRAY(x_step_size, ISP_VEC_NELEMS);
	VMEM_ARRAY(x_offset, ISP_VEC_NELEMS);
};

/*
 * PDAF configuration parameters
 */
struct isp_pdaf_vmem_params {

	struct isp_extraction_vmem_params ext_cfg_l_data;
	struct isp_extraction_vmem_params ext_cfg_r_data;
};

struct isp_pdaf_dmem_params {

	uint16_t frm_length;
	uint16_t frm_width;
	struct isp_stats_calc_dmem_params stats_calc_data;
	struct isp_extraction_dmem_params ext_cfg_l_data;
	struct isp_extraction_dmem_params ext_cfg_r_data;
};

#endif /* __IA_CSS_PDAF_PARAM_H */
#endif
