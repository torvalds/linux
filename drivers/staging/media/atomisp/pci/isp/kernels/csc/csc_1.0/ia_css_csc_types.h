/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CSC_TYPES_H
#define __IA_CSS_CSC_TYPES_H

/* @file
* CSS-API header file for Color Space Conversion parameters.
*/

/* Color Correction configuration.
 *
 *  This structure is used for 3 cases.
 *  ("YCgCo" is the output format of Demosaic.)
 *
 *  1. Color Space Conversion (YCgCo to YUV) for ISP1.
 *     ISP block: CSC1 (Color Space Conversion)
 *     struct ia_css_cc_config   *cc_config
 *
 *  2. Color Correction Matrix (YCgCo to RGB) for ISP2.
 *     ISP block: CCM2 (Color Correction Matrix)
 *     struct ia_css_cc_config   *yuv2rgb_cc_config
 *
 *  3. Color Space Conversion (RGB to YUV) for ISP2.
 *     ISP block: CSC2 (Color Space Conversion)
 *     struct ia_css_cc_config   *rgb2yuv_cc_config
 *
 *  default/ineffective:
 *  1. YCgCo -> YUV
 *	1	0.174		0.185
 *	0	-0.66252	-0.66874
 *	0	-0.83738	0.58131
 *
 *	fraction_bits = 12
 *	4096	713	758
 *	0	-2714	-2739
 *	0	-3430	2381
 *
 *  2. YCgCo -> RGB
 *	1	-1	1
 *	1	1	0
 *	1	-1	-1
 *
 *	fraction_bits = 12
 *	4096	-4096	4096
 *	4096	4096	0
 *	4096	-4096	-4096
 *
 *  3. RGB -> YUV
 *	0.299	   0.587	0.114
 *	-0.16874   -0.33126	0.5
 *	0.5	   -0.41869	-0.08131
 *
 *	fraction_bits = 13
 *	2449	4809	934
 *	-1382	-2714	4096
 *	4096	-3430	-666
 */
struct ia_css_cc_config {
	u32 fraction_bits;/** Fractional bits of matrix.
					u8.0, [0,13] */
	s32 matrix[3 * 3]; /** Conversion matrix.
					s[13-fraction_bits].[fraction_bits],
					[-8192,8191] */
};

#endif /* __IA_CSS_CSC_TYPES_H */
