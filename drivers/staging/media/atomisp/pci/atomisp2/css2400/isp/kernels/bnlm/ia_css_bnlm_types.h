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

#ifndef __IA_CSS_BNLM_TYPES_H
#define __IA_CSS_BNLM_TYPES_H

/** @file
* CSS-API header file for Bayer Non-Linear Mean parameters.
*/

#include "type_support.h" /* int32_t */

/** Bayer Non-Linear Mean configuration
 *
 * \brief BNLM public parameters.
 * \details Struct with all parameters for the BNLM kernel that can be set
 * from the CSS API.
 *
 * ISP2.6.1: BNLM is used.
 */
struct ia_css_bnlm_config {
	bool		rad_enable;	/**< Enable a radial dependency in a weight calculation */
	int32_t		rad_x_origin;	/**< Initial x coordinate for a radius calculation */
	int32_t		rad_y_origin;	/**< Initial x coordinate for a radius calculation */
	/* a threshold for average of weights if this < Th, do not denoise pixel */
	int32_t		avg_min_th;
	/* minimum weight for denoising if max < th, do not denoise pixel */
	int32_t		max_min_th;

	/**@{*/
	/** Coefficient for approximation, in the form of (1 + x / N)^N,
	 * that fits the first-order exp() to default exp_lut in BNLM sheet
	 * */
	int32_t		exp_coeff_a;
	uint32_t	exp_coeff_b;
	int32_t		exp_coeff_c;
	uint32_t	exp_exponent;
	/**@}*/

	int32_t nl_th[3];	/**< Detail thresholds */

	/** Index for n-th maximum candidate weight for each detail group */
	int32_t match_quality_max_idx[4];

	/**@{*/
	/** A lookup table for 1/sqrt(1+mu) approximation */
	int32_t mu_root_lut_thr[15];
	int32_t mu_root_lut_val[16];
	/**@}*/
	/**@{*/
	/** A lookup table for SAD normalization */
	int32_t sad_norm_lut_thr[15];
	int32_t sad_norm_lut_val[16];
	/**@}*/
	/**@{*/
	/** A lookup table that models a weight's dependency on textures */
	int32_t sig_detail_lut_thr[15];
	int32_t sig_detail_lut_val[16];
	/**@}*/
	/**@{*/
	/** A lookup table that models a weight's dependency on a pixel's radial distance */
	int32_t sig_rad_lut_thr[15];
	int32_t sig_rad_lut_val[16];
	/**@}*/
	/**@{*/
	/** A lookup table to control denoise power depending on a pixel's radial distance */
	int32_t rad_pow_lut_thr[15];
	int32_t rad_pow_lut_val[16];
	/**@}*/
	/**@{*/
	/** Non linear transfer functions to calculate the blending coefficient depending on detail group */
	/** detail group 0 */
	/**@{*/
	int32_t nl_0_lut_thr[15];
	int32_t nl_0_lut_val[16];
	/**@}*/
	/**@{*/
	/** detail group 1 */
	int32_t nl_1_lut_thr[15];
	int32_t nl_1_lut_val[16];
	/**@}*/
	/**@{*/
	/** detail group 2 */
	int32_t nl_2_lut_thr[15];
	int32_t nl_2_lut_val[16];
	/**@}*/
	/**@{*/
	/** detail group 3 */
	int32_t nl_3_lut_thr[15];
	int32_t nl_3_lut_val[16];
	/**@}*/
	/**@}*/
};

#endif /* __IA_CSS_BNLM_TYPES_H */
