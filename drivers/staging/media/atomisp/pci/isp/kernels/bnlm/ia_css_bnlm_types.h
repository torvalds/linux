/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BNLM_TYPES_H
#define __IA_CSS_BNLM_TYPES_H

/* @file
* CSS-API header file for Bayer Non-Linear Mean parameters.
*/

#include "type_support.h" /* int32_t */

/* Bayer Non-Linear Mean configuration
 *
 * \brief BNLM public parameters.
 * \details Struct with all parameters for the BNLM kernel that can be set
 * from the CSS API.
 *
 * ISP2.6.1: BNLM is used.
 */
struct ia_css_bnlm_config {
	bool		rad_enable;	/** Enable a radial dependency in a weight calculation */
	s32		rad_x_origin;	/** Initial x coordinate for a radius calculation */
	s32		rad_y_origin;	/** Initial x coordinate for a radius calculation */
	/* a threshold for average of weights if this < Th, do not denoise pixel */
	s32		avg_min_th;
	/* minimum weight for denoising if max < th, do not denoise pixel */
	s32		max_min_th;

	/**@{*/
	/* Coefficient for approximation, in the form of (1 + x / N)^N,
	 * that fits the first-order exp() to default exp_lut in BNLM sheet
	 * */
	s32		exp_coeff_a;
	u32	exp_coeff_b;
	s32		exp_coeff_c;
	u32	exp_exponent;
	/**@}*/

	s32 nl_th[3];	/** Detail thresholds */

	/* Index for n-th maximum candidate weight for each detail group */
	s32 match_quality_max_idx[4];

	/**@{*/
	/* A lookup table for 1/sqrt(1+mu) approximation */
	s32 mu_root_lut_thr[15];
	s32 mu_root_lut_val[16];
	/**@}*/
	/**@{*/
	/* A lookup table for SAD normalization */
	s32 sad_norm_lut_thr[15];
	s32 sad_norm_lut_val[16];
	/**@}*/
	/**@{*/
	/* A lookup table that models a weight's dependency on textures */
	s32 sig_detail_lut_thr[15];
	s32 sig_detail_lut_val[16];
	/**@}*/
	/**@{*/
	/* A lookup table that models a weight's dependency on a pixel's radial distance */
	s32 sig_rad_lut_thr[15];
	s32 sig_rad_lut_val[16];
	/**@}*/
	/**@{*/
	/* A lookup table to control denoise power depending on a pixel's radial distance */
	s32 rad_pow_lut_thr[15];
	s32 rad_pow_lut_val[16];
	/**@}*/
	/**@{*/
	/* Non linear transfer functions to calculate the blending coefficient depending on detail group */
	/* detail group 0 */
	/**@{*/
	s32 nl_0_lut_thr[15];
	s32 nl_0_lut_val[16];
	/**@}*/
	/**@{*/
	/* detail group 1 */
	s32 nl_1_lut_thr[15];
	s32 nl_1_lut_val[16];
	/**@}*/
	/**@{*/
	/* detail group 2 */
	s32 nl_2_lut_thr[15];
	s32 nl_2_lut_val[16];
	/**@}*/
	/**@{*/
	/* detail group 3 */
	s32 nl_3_lut_thr[15];
	s32 nl_3_lut_val[16];
	/**@}*/
	/**@}*/
};

#endif /* __IA_CSS_BNLM_TYPES_H */
