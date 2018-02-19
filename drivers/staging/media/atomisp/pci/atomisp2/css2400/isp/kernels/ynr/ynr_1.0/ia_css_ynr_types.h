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

#ifndef __IA_CSS_YNR_TYPES_H
#define __IA_CSS_YNR_TYPES_H

/* @file
* CSS-API header file for Noise Reduction (BNR) and YCC Noise Reduction (YNR,CNR).
*/

/* Configuration used by Bayer Noise Reduction (BNR) and
 *  YCC Noise Reduction (YNR,CNR).
 *
 *  ISP block: BNR1, YNR1, CNR1
 *  ISP1: BNR1,YNR1,CNR1 are used.
 *  ISP2: BNR1,YNR1,CNR1 are used for Preview/Video.
 *        BNR1,YNR2,CNR2 are used for Still.
 */
struct ia_css_nr_config {
	ia_css_u0_16 bnr_gain;	   /** Strength of noise reduction (BNR).
				u0.16, [0,65535],
				default 14336(0.21875), ineffective 0 */
	ia_css_u0_16 ynr_gain;	   /** Strength of noise reduction (YNR).
				u0.16, [0,65535],
				default 14336(0.21875), ineffective 0 */
	ia_css_u0_16 direction;    /** Sensitivity of edge (BNR).
				u0.16, [0,65535],
				default 512(0.0078125), ineffective 0 */
	ia_css_u0_16 threshold_cb; /** Coring threshold for Cb (CNR).
				This is the same as
				de_config.c1_coring_threshold.
				u0.16, [0,65535],
				default 0(0), ineffective 0 */
	ia_css_u0_16 threshold_cr; /** Coring threshold for Cr (CNR).
				This is the same as
				de_config.c2_coring_threshold.
				u0.16, [0,65535],
				default 0(0), ineffective 0 */
};

/* Edge Enhancement (sharpen) configuration.
 *
 *  ISP block: YEE1
 *  ISP1: YEE1 is used.
 *  ISP2: YEE1 is used for Preview/Video.
 *       (YEE2 is used for Still.)
 */
struct ia_css_ee_config {
	ia_css_u5_11 gain;	  /** The strength of sharpness.
					u5.11, [0,65535],
					default 8192(4.0), ineffective 0 */
	ia_css_u8_8 threshold;    /** The threshold that divides noises from
					edge.
					u8.8, [0,65535],
					default 256(1.0), ineffective 65535 */
	ia_css_u5_11 detail_gain; /** The strength of sharpness in pell-mell
					area.
					u5.11, [0,65535],
					default 2048(1.0), ineffective 0 */
};

/* YNR and YEE (sharpen) configuration.
 */
struct ia_css_yee_config {
	struct ia_css_nr_config nr; /** The NR configuration. */
	struct ia_css_ee_config ee; /** The EE configuration. */
};

#endif /* __IA_CSS_YNR_TYPES_H */

