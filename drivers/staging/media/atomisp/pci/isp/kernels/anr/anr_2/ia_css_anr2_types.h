/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
	s16 data[13 * 64];
};

#endif /* __IA_CSS_ANR2_TYPES_H */
