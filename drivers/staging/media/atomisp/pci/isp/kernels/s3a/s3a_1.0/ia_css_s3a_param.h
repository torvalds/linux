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

#ifndef __IA_CSS_S3A_PARAM_H
#define __IA_CSS_S3A_PARAM_H

#include "type_support.h"

/* AE (3A Support) */
struct sh_css_isp_ae_params {
	/* coefficients to calculate Y */
	s32 y_coef_r;
	s32 y_coef_g;
	s32 y_coef_b;
};

/* AWB (3A Support) */
struct sh_css_isp_awb_params {
	s32 lg_high_raw;
	s32 lg_low;
	s32 lg_high;
};

/* AF (3A Support) */
struct sh_css_isp_af_params {
	s32 fir1[7];
	s32 fir2[7];
};

/* S3A (3A Support) */
struct sh_css_isp_s3a_params {
	/* coefficients to calculate Y */
	struct sh_css_isp_ae_params ae;

	/* AWB level gate */
	struct sh_css_isp_awb_params awb;

	/* af fir coefficients */
	struct sh_css_isp_af_params af;
};

#endif /* __IA_CSS_S3A_PARAM_H */
