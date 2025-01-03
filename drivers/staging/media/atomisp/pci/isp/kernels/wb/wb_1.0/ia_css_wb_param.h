/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_WB_PARAM_H
#define __IA_CSS_WB_PARAM_H

#include "type_support.h"

/* WB (White Balance) */
struct sh_css_isp_wb_params {
	s32 gain_shift;
	s32 gain_gr;
	s32 gain_r;
	s32 gain_b;
	s32 gain_gb;
};

#endif /* __IA_CSS_WB_PARAM_H */
