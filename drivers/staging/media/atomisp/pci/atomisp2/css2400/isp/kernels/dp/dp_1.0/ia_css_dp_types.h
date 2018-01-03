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

#ifndef __IA_CSS_DP_TYPES_H
#define __IA_CSS_DP_TYPES_H

/* @file
* CSS-API header file for Defect Pixel Correction (DPC) parameters.
*/


/* Defect Pixel Correction configuration.
 *
 *  ISP block: DPC1 (DPC after WB)
 *             DPC2 (DPC before WB)
 *  ISP1: DPC1 is used.
 *  ISP2: DPC2 is used.
 */
struct ia_css_dp_config {
	ia_css_u0_16 threshold; /** The threshold of defect pixel correction,
			      representing the permissible difference of
			      intensity between one pixel and its
			      surrounding pixels. Smaller values result
				in more frequent pixel corrections.
				u0.16, [0,65535],
				default 8192, ineffective 65535 */
	ia_css_u8_8 gain;	 /** The sensitivity of mis-correction. ISP will
			      miss a lot of defects if the value is set
				too large.
				u8.8, [0,65535],
				default 4096, ineffective 65535 */
	uint32_t gr;	/* unsigned <integer_bits>.<16-integer_bits> */
	uint32_t r;	/* unsigned <integer_bits>.<16-integer_bits> */
	uint32_t b;	/* unsigned <integer_bits>.<16-integer_bits> */
	uint32_t gb;	/* unsigned <integer_bits>.<16-integer_bits> */
};

#endif /* __IA_CSS_DP_TYPES_H */

