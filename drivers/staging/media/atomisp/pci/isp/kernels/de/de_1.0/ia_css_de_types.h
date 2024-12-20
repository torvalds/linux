/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DE_TYPES_H
#define __IA_CSS_DE_TYPES_H

/* @file
* CSS-API header file for Demosaic (bayer-to-YCgCo) parameters.
*/

/* Demosaic (bayer-to-YCgCo) configuration.
 *
 *  ISP block: DE1
 *  ISP1: DE1 is used.
 * (ISP2: DE2 is used.)
 */
struct ia_css_de_config {
	ia_css_u0_16 pixelnoise; /** Pixel noise used in moire elimination.
				u0.16, [0,65535],
				default 0, ineffective 0 */
	ia_css_u0_16 c1_coring_threshold; /** Coring threshold for C1.
				This is the same as nr_config.threshold_cb.
				u0.16, [0,65535],
				default 128(0.001953125), ineffective 0 */
	ia_css_u0_16 c2_coring_threshold; /** Coring threshold for C2.
				This is the same as nr_config.threshold_cr.
				u0.16, [0,65535],
				default 128(0.001953125), ineffective 0 */
};

#endif /* __IA_CSS_DE_TYPES_H */
