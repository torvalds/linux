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

#ifndef __IA_CSS_BNR_PARAM_H
#define __IA_CSS_BNR_PARAM_H

#include "type_support.h"

/* BNR (Bayer Noise Reduction) */
struct sh_css_isp_bnr_params {
	s32 gain_all;
	s32 gain_dir;
	s32 threshold_low;
	s32 threshold_width_log2;
	s32 threshold_width;
	s32 clip;
};

#endif /* __IA_CSS_BNR_PARAM_H */
