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

#ifndef __IA_CSS_BNR2_2_TYPES_H
#define __IA_CSS_BNR2_2_TYPES_H

/** @file
* CSS-API header file for Bayer Noise Reduction parameters.
*/

#include "type_support.h" /* int32_t */

/** Bayer Noise Reduction 2.2 configuration
 *
 * \brief BNR2_2 public parameters.
 * \details Struct with all parameters for the BNR2.2 kernel that can be set
 * from the CSS API.
 *
 * ISP2.6.1: BNR2.2 is used.
 */
struct ia_css_bnr2_2_config {
	/**@{*/
	/** Directional variance gain for R/G/B components in dark region */
	int32_t d_var_gain_r;
	int32_t d_var_gain_g;
	int32_t d_var_gain_b;
	/**@}*/
	/**@{*/
	/** Slope of Directional variance gain between dark and bright region */
	int32_t d_var_gain_slope_r;
	int32_t d_var_gain_slope_g;
	int32_t d_var_gain_slope_b;
	/**@}*/
	/**@{*/
	/** Non-Directional variance gain for R/G/B components in dark region */
	int32_t n_var_gain_r;
	int32_t n_var_gain_g;
	int32_t n_var_gain_b;
	/**@}*/
	/**@{*/
	/** Slope of Non-Directional variance gain between dark and bright region */
	int32_t n_var_gain_slope_r;
	int32_t n_var_gain_slope_g;
	int32_t n_var_gain_slope_b;
	/**@}*/

	int32_t dir_thres;		/**< Threshold for directional filtering */
	int32_t dir_thres_w;		/**< Threshold width for directional filtering */
	int32_t var_offset_coef;	/**< Variance offset coefficient */
	int32_t dir_gain;		/**< Gain for directional coefficient */
	int32_t detail_gain;		/**< Gain for low contrast texture control */
	int32_t detail_gain_divisor;	/**< Gain divisor for low contrast texture control */
	int32_t detail_level_offset;	/**< Bias value for low contrast texture control */
	int32_t d_var_th_min;		/**< Minimum clipping value for directional variance*/
	int32_t d_var_th_max;		/**< Maximum clipping value for diretional variance*/
	int32_t n_var_th_min;		/**< Minimum clipping value for non-directional variance*/
	int32_t n_var_th_max;		/**< Maximum clipping value for non-directional variance*/
};

#endif /* __IA_CSS_BNR2_2_TYPES_H */
