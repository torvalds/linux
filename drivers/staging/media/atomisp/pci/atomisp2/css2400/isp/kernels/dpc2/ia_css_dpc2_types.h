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

#ifndef __IA_CSS_DPC2_TYPES_H
#define __IA_CSS_DPC2_TYPES_H

/** @file
* CSS-API header file for Defect Pixel Correction 2 (DPC2) parameters.
*/

#include "type_support.h"

/**@{*/
/** Floating point constants for different metrics. */
#define METRIC1_ONE_FP	(1<<12)
#define METRIC2_ONE_FP	(1<<5)
#define METRIC3_ONE_FP	(1<<12)
#define WBGAIN_ONE_FP	(1<<9)
/**@}*/

/**@{*/
/** Defect Pixel Correction 2 configuration.
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
	int32_t metric1;
	int32_t metric2;
	int32_t metric3;
	int32_t wb_gain_gr;
	int32_t wb_gain_r;
	int32_t wb_gain_b;
	int32_t wb_gain_gb;
	/**@}*/
};
/**@}*/

#endif /* __IA_CSS_DPC2_TYPES_H */

