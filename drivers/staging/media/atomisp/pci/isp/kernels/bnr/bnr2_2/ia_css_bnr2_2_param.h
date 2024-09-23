/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BNR2_2_PARAM_H
#define __IA_CSS_BNR2_2_PARAM_H

#include "type_support.h"

/* BNR (Bayer Noise Reduction) ISP parameters */
struct sh_css_isp_bnr2_2_params {
	s32 d_var_gain_r;
	s32 d_var_gain_g;
	s32 d_var_gain_b;
	s32 d_var_gain_slope_r;
	s32 d_var_gain_slope_g;
	s32 d_var_gain_slope_b;
	s32 n_var_gain_r;
	s32 n_var_gain_g;
	s32 n_var_gain_b;
	s32 n_var_gain_slope_r;
	s32 n_var_gain_slope_g;
	s32 n_var_gain_slope_b;
	s32 dir_thres;
	s32 dir_thres_w;
	s32 var_offset_coef;
	s32 dir_gain;
	s32 detail_gain;
	s32 detail_gain_divisor;
	s32 detail_level_offset;
	s32 d_var_th_min;
	s32 d_var_th_max;
	s32 n_var_th_min;
	s32 n_var_th_max;
};

#endif /* __IA_CSS_BNR2_2_PARAM_H */
