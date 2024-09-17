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

#ifndef __IA_CSS_DP_PARAM_H
#define __IA_CSS_DP_PARAM_H

#include "type_support.h"
#include "bnr/bnr_1.0/ia_css_bnr_param.h"

/* DP (Defect Pixel Correction) */
struct sh_css_isp_dp_params {
	s32 threshold_single;
	s32 threshold_2adjacent;
	s32 gain;
	s32 coef_rr_gr;
	s32 coef_rr_gb;
	s32 coef_bb_gb;
	s32 coef_bb_gr;
	s32 coef_gr_rr;
	s32 coef_gr_bb;
	s32 coef_gb_bb;
	s32 coef_gb_rr;
};

#endif /* __IA_CSS_DP_PARAM_H */
