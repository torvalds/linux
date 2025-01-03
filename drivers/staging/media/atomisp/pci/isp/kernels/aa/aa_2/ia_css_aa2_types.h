/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_AA2_TYPES_H
#define __IA_CSS_AA2_TYPES_H

/* @file
* CSS-API header file for Anti-Aliasing parameters.
*/

/* Anti-Aliasing configuration.
 *
 *  This structure is used both for YUV AA and Bayer AA.
 *
 *  1. YUV Anti-Aliasing
 *     struct ia_css_aa_config   *aa_config
 *
 *     ISP block: AA2
 *    (ISP1: AA2 is not used.)
 *     ISP2: AA2 should be used. But, AA2 is not used currently.
 *
 *  2. Bayer Anti-Aliasing
 *     struct ia_css_aa_config   *baa_config
 *
 *     ISP block: BAA2
 *     ISP1: BAA2 is used.
 *     ISP2: BAA2 is used.
 */
struct ia_css_aa_config {
	u16 strength;	/** Strength of the filter.
					u0.13, [0,8191],
					default/ineffective 0 */
};

#endif /* __IA_CSS_AA2_TYPES_H */
