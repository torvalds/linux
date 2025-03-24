/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_EED1_8_TYPES_H
#define __IA_CSS_EED1_8_TYPES_H

/* @file
* CSS-API header file for Edge Enhanced Demosaic parameters.
*/

#include "type_support.h"

/**
 * \brief EED1_8 public parameters.
 * \details Struct with all parameters for the EED1.8 kernel that can be set
 * from the CSS API.
 */

/* parameter list is based on ISP261 CSS API public parameter list_all.xlsx from 28-01-2015 */

/* Number of segments + 1 segment used in edge reliability enhancement
 * Ineffective: N/A
 * Default:	9
 */
#define IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS	9

/* Edge Enhanced Demosaic configuration
 *
 * ISP2.6.1: EED1_8 is used.
 */

struct ia_css_eed1_8_config {
	s32 rbzp_strength;	/** Strength of zipper reduction. */

	s32 fcstrength;	/** Strength of false color reduction. */
	s32 fcthres_0;	/** Threshold to prevent chroma coring due to noise or green disparity in dark region. */
	s32 fcthres_1;	/** Threshold to prevent chroma coring due to noise or green disparity in bright region. */
	s32 fc_sat_coef;	/** How much color saturation to maintain in high color saturation region. */
	s32 fc_coring_prm;	/** Chroma coring coefficient for tint color suppression. */

	s32 aerel_thres0;	/** Threshold for Non-Directional Reliability at dark region. */
	s32 aerel_gain0;	/** Gain for Non-Directional Reliability at dark region. */
	s32 aerel_thres1;	/** Threshold for Non-Directional Reliability at bright region. */
	s32 aerel_gain1;	/** Gain for Non-Directional Reliability at bright region. */

	s32 derel_thres0;	/** Threshold for Directional Reliability at dark region. */
	s32 derel_gain0;	/** Gain for Directional Reliability at dark region. */
	s32 derel_thres1;	/** Threshold for Directional Reliability at bright region. */
	s32 derel_gain1;	/** Gain for Directional Reliability at bright region. */

	s32 coring_pos0;	/** Positive Edge Coring Threshold in dark region. */
	s32 coring_pos1;	/** Positive Edge Coring Threshold in bright region. */
	s32 coring_neg0;	/** Negative Edge Coring Threshold in dark region. */
	s32 coring_neg1;	/** Negative Edge Coring Threshold in bright region. */

	s32 gain_exp;	/** Common Exponent of Gain. */
	s32 gain_pos0;	/** Gain for Positive Edge in dark region. */
	s32 gain_pos1;	/** Gain for Positive Edge in bright region. */
	s32 gain_neg0;	/** Gain for Negative Edge in dark region. */
	s32 gain_neg1;	/** Gain for Negative Edge in bright region. */

	s32 pos_margin0;	/** Margin for Positive Edge in dark region. */
	s32 pos_margin1;	/** Margin for Positive Edge in bright region. */
	s32 neg_margin0;	/** Margin for Negative Edge in dark region. */
	s32 neg_margin1;	/** Margin for Negative Edge in bright region. */

	s32 dew_enhance_seg_x[IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS];		/** Segment data for directional edge weight: X. */
	s32 dew_enhance_seg_y[IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS];		/** Segment data for directional edge weight: Y. */
	s32 dew_enhance_seg_slope[(IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS -
				   1)];	/** Segment data for directional edge weight: Slope. */
	s32 dew_enhance_seg_exp[(IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS -
				 1)];	/** Segment data for directional edge weight: Exponent. */
	s32 dedgew_max;	/** Max Weight for Directional Edge. */
};

#endif /* __IA_CSS_EED1_8_TYPES_H */
