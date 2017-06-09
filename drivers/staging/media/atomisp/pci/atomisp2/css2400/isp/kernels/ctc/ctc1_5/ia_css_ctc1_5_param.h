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

#ifndef __IA_CSS_CTC1_5_PARAM_H
#define __IA_CSS_CTC1_5_PARAM_H

#include "type_support.h"
#include "ctc/ctc_1.0/ia_css_ctc_param.h" /* vamem params */

/* CTC (Color Tone Control) */
struct sh_css_isp_ctc_params {
	int32_t y0;
	int32_t y1;
	int32_t y2;
	int32_t y3;
	int32_t y4;
	int32_t y5;
	int32_t ce_gain_exp;
	int32_t x1;
	int32_t x2;
	int32_t x3;
	int32_t x4;
	int32_t dydx0;
	int32_t dydx0_shift;
	int32_t dydx1;
	int32_t dydx1_shift;
	int32_t dydx2;
	int32_t dydx2_shift;
	int32_t dydx3;
	int32_t dydx3_shift;
	int32_t dydx4;
	int32_t dydx4_shift;
};

#endif /* __IA_CSS_CTC1_5_PARAM_H */
