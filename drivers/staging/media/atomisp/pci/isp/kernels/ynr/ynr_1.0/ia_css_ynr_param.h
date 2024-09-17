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

#ifndef __IA_CSS_YNR_PARAM_H
#define __IA_CSS_YNR_PARAM_H

#include "type_support.h"

/* YNR (Y Noise Reduction) */
struct sh_css_isp_ynr_params {
	s32 threshold;
	s32 gain_all;
	s32 gain_dir;
	s32 threshold_cb;
	s32 threshold_cr;
};

/* YEE (Y Edge Enhancement) */
struct sh_css_isp_yee_params {
	s32 dirthreshold_s;
	s32 dirthreshold_g;
	s32 dirthreshold_width_log2;
	s32 dirthreshold_width;
	s32 detailgain;
	s32 coring_s;
	s32 coring_g;
	s32 scale_plus_s;
	s32 scale_plus_g;
	s32 scale_minus_s;
	s32 scale_minus_g;
	s32 clip_plus_s;
	s32 clip_plus_g;
	s32 clip_minus_s;
	s32 clip_minus_g;
	s32 Yclip;
};

#endif /* __IA_CSS_YNR_PARAM_H */
