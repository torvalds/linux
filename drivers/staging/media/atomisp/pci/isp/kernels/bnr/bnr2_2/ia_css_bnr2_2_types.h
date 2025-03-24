/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BNR2_2_TYPES_H
#define __IA_CSS_BNR2_2_TYPES_H

/* @file
* CSS-API header file for Bayer Noise Reduction parameters.
*/

#include "type_support.h" /* int32_t */

/* Bayer Noise Reduction 2.2 configuration
 *
 * \brief BNR2_2 public parameters.
 * \details Struct with all parameters for the BNR2.2 kernel that can be set
 * from the CSS API.
 *
 * ISP2.6.1: BNR2.2 is used.
 */
struct ia_css_bnr2_2_config {
	/**@{*/
	/* Directional variance gain for R/G/B components in dark region */
	s32 d_var_gain_r;
	s32 d_var_gain_g;
	s32 d_var_gain_b;
	/**@}*/
	/**@{*/
	/* Slope of Directional variance gain between dark and bright region */
	s32 d_var_gain_slope_r;
	s32 d_var_gain_slope_g;
	s32 d_var_gain_slope_b;
	/**@}*/
	/**@{*/
	/* Non-Directional variance gain for R/G/B components in dark region */
	s32 n_var_gain_r;
	s32 n_var_gain_g;
	s32 n_var_gain_b;
	/**@}*/
	/**@{*/
	/* Slope of Non-Directional variance gain between dark and bright region */
	s32 n_var_gain_slope_r;
	s32 n_var_gain_slope_g;
	s32 n_var_gain_slope_b;
	/**@}*/

	s32 dir_thres;		/** Threshold for directional filtering */
	s32 dir_thres_w;		/** Threshold width for directional filtering */
	s32 var_offset_coef;	/** Variance offset coefficient */
	s32 dir_gain;		/** Gain for directional coefficient */
	s32 detail_gain;		/** Gain for low contrast texture control */
	s32 detail_gain_divisor;	/** Gain divisor for low contrast texture control */
	s32 detail_level_offset;	/** Bias value for low contrast texture control */
	s32 d_var_th_min;		/** Minimum clipping value for directional variance*/
	s32 d_var_th_max;		/** Maximum clipping value for diretional variance*/
	s32 n_var_th_min;		/** Minimum clipping value for non-directional variance*/
	s32 n_var_th_max;		/** Maximum clipping value for non-directional variance*/
};

#endif /* __IA_CSS_BNR2_2_TYPES_H */
