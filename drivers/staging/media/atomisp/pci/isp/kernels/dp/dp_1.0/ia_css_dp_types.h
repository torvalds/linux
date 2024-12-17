/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
	u32 gr;	/* unsigned <integer_bits>.<16-integer_bits> */
	u32 r;	/* unsigned <integer_bits>.<16-integer_bits> */
	u32 b;	/* unsigned <integer_bits>.<16-integer_bits> */
	u32 gb;	/* unsigned <integer_bits>.<16-integer_bits> */
};

#endif /* __IA_CSS_DP_TYPES_H */
