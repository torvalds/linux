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

#ifndef __IA_CSS_TDF_PARAM_H
#define __IA_CSS_TDF_PARAM_H

#include "type_support.h"
#include "vmem.h" /* needed for VMEM_ARRAY */

struct ia_css_isp_tdf_vmem_params {
	VMEM_ARRAY(pyramid, ISP_VEC_NELEMS);
	VMEM_ARRAY(threshold_flat, ISP_VEC_NELEMS);
	VMEM_ARRAY(threshold_detail, ISP_VEC_NELEMS);
};

struct ia_css_isp_tdf_dmem_params {
	int32_t Epsilon_0;
	int32_t Epsilon_1;
	int32_t EpsScaleText;
	int32_t EpsScaleEdge;
	int32_t Sepa_flat;
	int32_t Sepa_Edge;
	int32_t Blend_Flat;
	int32_t Blend_Text;
	int32_t Blend_Edge;
	int32_t Shading_Gain;
	int32_t Shading_baseGain;
	int32_t LocalY_Gain;
	int32_t LocalY_baseGain;
};

#endif /* __IA_CSS_TDF_PARAM_H */
