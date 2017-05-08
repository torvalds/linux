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

#ifndef __IA_CSS_WB_PARAM_H
#define __IA_CSS_WB_PARAM_H

#include "type_support.h"

/* WB (White Balance) */
struct sh_css_isp_wb_params {
	int32_t gain_shift;
	int32_t gain_gr;
	int32_t gain_r;
	int32_t gain_b;
	int32_t gain_gb;
};

#endif /* __IA_CSS_WB_PARAM_H */
