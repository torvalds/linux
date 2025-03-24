/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DPC2_TYPES_H
#define __IA_CSS_DPC2_TYPES_H

/* @file
* CSS-API header file for Defect Pixel Correction 2 (DPC2) parameters.
*/

#include "type_support.h"

/**@{*/
/* Floating point constants for different metrics. */
#define METRIC1_ONE_FP	BIT(12)
#define METRIC2_ONE_FP	BIT(5)
#define METRIC3_ONE_FP	BIT(12)
#define WBGAIN_ONE_FP	BIT(9)
/**@}*/

/**@{*/
/* Defect Pixel Correction 2 configuration.
 *
 * \brief DPC2 public parameters.
 * \details Struct with all parameters for the Defect Pixel Correction 2
 * kernel that can be set from the CSS API.
 *
 * ISP block: DPC1 (DPC after WB)
 *            DPC2 (DPC before WB)
 * ISP1: DPC1 is used.
 * ISP2: DPC2 is used.
 *
 */
struct ia_css_dpc2_config {
	/**@{*/
	s32 metric1;
	s32 metric2;
	s32 metric3;
	s32 wb_gain_gr;
	s32 wb_gain_r;
	s32 wb_gain_b;
	s32 wb_gain_gb;
	/**@}*/
};

/**@}*/

#endif /* __IA_CSS_DPC2_TYPES_H */
