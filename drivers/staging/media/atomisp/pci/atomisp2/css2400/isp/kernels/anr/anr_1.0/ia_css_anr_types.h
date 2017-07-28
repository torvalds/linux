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

#ifndef __IA_CSS_ANR_TYPES_H
#define __IA_CSS_ANR_TYPES_H

/** @file
* CSS-API header file for Advanced Noise Reduction kernel v1
*/

/* Application specific DMA settings  */
#define ANR_BPP                 10
#define ANR_ELEMENT_BITS        ((CEIL_DIV(ANR_BPP, 8))*8)

/** Advanced Noise Reduction configuration.
 *  This is also known as Low-Light.
 */
struct ia_css_anr_config {
	int32_t threshold; /**< Threshold */
	int32_t thresholds[4*4*4];
	int32_t factors[3];
};

#endif /* __IA_CSS_ANR_TYPES_H */

