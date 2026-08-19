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
 *
 * @strength: Strength of the filter, in u0.13 fixed-point format.
 *            Valid range: [0, 8191]. A value of 0 means the filter is
 *            ineffective (default).
 */
struct ia_css_aa_config {
	u16 strength;
};

#endif /* __IA_CSS_AA2_TYPES_H */
