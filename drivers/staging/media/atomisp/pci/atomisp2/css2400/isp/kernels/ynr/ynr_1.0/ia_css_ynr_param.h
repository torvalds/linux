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

#ifndef __IA_CSS_YNR_PARAM_H
#define __IA_CSS_YNR_PARAM_H

#include "type_support.h"

/* YNR (Y Noise Reduction) */
struct sh_css_isp_ynr_params {
	int32_t threshold;
	int32_t gain_all;
	int32_t gain_dir;
	int32_t threshold_cb;
	int32_t threshold_cr;
};

/* YEE (Y Edge Enhancement) */
struct sh_css_isp_yee_params {
	int32_t dirthreshold_s;
	int32_t dirthreshold_g;
	int32_t dirthreshold_width_log2;
	int32_t dirthreshold_width;
	int32_t detailgain;
	int32_t coring_s;
	int32_t coring_g;
	int32_t scale_plus_s;
	int32_t scale_plus_g;
	int32_t scale_minus_s;
	int32_t scale_minus_g;
	int32_t clip_plus_s;
	int32_t clip_plus_g;
	int32_t clip_minus_s;
	int32_t clip_minus_g;
	int32_t Yclip;
};

#endif /* __IA_CSS_YNR_PARAM_H */
