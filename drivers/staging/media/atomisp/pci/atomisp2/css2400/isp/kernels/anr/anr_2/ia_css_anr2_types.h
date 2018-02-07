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

#ifndef __IA_CSS_ANR2_TYPES_H
#define __IA_CSS_ANR2_TYPES_H

/* @file
* CSS-API header file for Advanced Noise Reduction kernel v2
*/

#include "type_support.h"

#define ANR_PARAM_SIZE          13

/* Advanced Noise Reduction (ANR) thresholds */
struct ia_css_anr_thres {
	int16_t data[13*64];
};

#endif /* __IA_CSS_ANR2_TYPES_H */

