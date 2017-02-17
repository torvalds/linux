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

#ifndef __IA_CSS_XNR3_0_11_PARAM_H
#define __IA_CSS_XNR3_0_11_PARAM_H

#include "type_support.h"
#include "vmem.h" /* needed for VMEM_ARRAY */

/* XNR3.0.11 filter size */
#define XNR_FILTER_SIZE             11

/*
 * STRUCT sh_css_isp_xnr3_0_11_vmem_params
 * -----------------------------------------------
 * XNR3.0.11 ISP VMEM parameters
 */
struct sh_css_isp_xnr3_0_11_vmem_params {
	VMEM_ARRAY(x, ISP_VEC_NELEMS);
	VMEM_ARRAY(a, ISP_VEC_NELEMS);
	VMEM_ARRAY(b, ISP_VEC_NELEMS);
	VMEM_ARRAY(c, ISP_VEC_NELEMS);
};

 /*
 * STRUCT sh_css_isp_xnr3_0_11_params
 * -----------------------------------------------
 * XNR3.0.11 ISP parameters
 */
struct sh_css_isp_xnr3_0_11_params {
	int32_t weight_y0;
	int32_t weight_u0;
	int32_t weight_v0;
	int32_t weight_ydiff;
	int32_t weight_udiff;
	int32_t weight_vdiff;
};

#endif  /*__IA_CSS_XNR3_0_11_PARAM_H */
