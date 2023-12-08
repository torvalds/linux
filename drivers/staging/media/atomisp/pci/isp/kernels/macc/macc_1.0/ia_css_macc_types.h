/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_MACC_TYPES_H
#define __IA_CSS_MACC_TYPES_H

/* @file
* CSS-API header file for Multi-Axis Color Correction (MACC) parameters.
*/

/* Number of axes in the MACC table. */
#define IA_CSS_MACC_NUM_AXES           16
/* Number of coefficients per MACC axes. */
#define IA_CSS_MACC_NUM_COEFS          4
/* The number of planes in the morphing table. */

/* Multi-Axis Color Correction (MACC) table.
 *
 *  ISP block: MACC1 (MACC by only matrix)
 *             MACC2 (MACC by matrix and exponent(ia_css_macc_config))
 *  ISP1: MACC1 is used.
 *  ISP2: MACC2 is used.
 *
 *  [MACC1]
 *   OutU = (data00 * InU + data01 * InV) >> 13
 *   OutV = (data10 * InU + data11 * InV) >> 13
 *
 *   default/ineffective:
 *   OutU = (8192 * InU +    0 * InV) >> 13
 *   OutV = (   0 * InU + 8192 * InV) >> 13
 *
 *  [MACC2]
 *   OutU = (data00 * InU + data01 * InV) >> (13 - exp)
 *   OutV = (data10 * InU + data11 * InV) >> (13 - exp)
 *
 *   default/ineffective: (exp=1)
 *   OutU = (4096 * InU +    0 * InV) >> (13 - 1)
 *   OutV = (   0 * InU + 4096 * InV) >> (13 - 1)
 */

struct ia_css_macc_table {
	s16 data[IA_CSS_MACC_NUM_COEFS * IA_CSS_MACC_NUM_AXES];
	/** 16 of 2x2 matix
	  MACC1: s2.13, [-65536,65535]
	    default/ineffective:
		16 of "identity 2x2 matix" {8192,0,0,8192}
	  MACC2: s[macc_config.exp].[13-macc_config.exp], [-8192,8191]
	    default/ineffective: (s1.12)
		16 of "identity 2x2 matix" {4096,0,0,4096} */
};

#endif /* __IA_CSS_MACC_TYPES_H */
