/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_CTC2_PARAM_H
#define __IA_CSS_CTC2_PARAM_H

#define IA_CSS_CTC_COEF_SHIFT          13
#include "vmem.h" /* needed for VMEM_ARRAY */

/* CTC (Chroma Tone Control)ISP Parameters */

/*VMEM Luma params*/
struct ia_css_isp_ctc2_vmem_params {
	/** Gains by Y(Luma) at Y = 0.0,Y_X1, Y_X2, Y_X3, Y_X4*/
	VMEM_ARRAY(y_x, ISP_VEC_NELEMS);
	/* kneepoints by Y(Luma) 0.0, y_x1, y_x2, y _x3, y_x4*/
	VMEM_ARRAY(y_y, ISP_VEC_NELEMS);
	/* Slopes of lines interconnecting
	 *  0.0 -> y_x1 -> y_x2 -> y _x3 -> y_x4 -> 1.0*/
	VMEM_ARRAY(e_y_slope, ISP_VEC_NELEMS);
};

/*DMEM Chroma params*/
struct ia_css_isp_ctc2_dmem_params {
	/* Gains by UV(Chroma) under kneepoints uv_x0 and uv_x1*/
	s32 uv_y0;
	s32 uv_y1;

	/* Kneepoints by UV(Chroma)- uv_x0 and uv_x1*/
	s32 uv_x0;
	s32 uv_x1;

	/* Slope of line interconnecting uv_x0 -> uv_x1*/
	s32 uv_dydx;

};
#endif /* __IA_CSS_CTC2_PARAM_H */
