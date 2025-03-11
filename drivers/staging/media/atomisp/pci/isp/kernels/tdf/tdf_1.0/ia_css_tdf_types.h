/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_TDF_TYPES_H
#define __IA_CSS_TDF_TYPES_H

/* @file
* CSS-API header file for Transform Domain Filter parameters.
*/

#include "type_support.h"

/* Transform Domain Filter configuration
 *
 * \brief TDF public parameters.
 * \details Struct with all parameters for the TDF kernel that can be set
 * from the CSS API.
 *
 * ISP2.6.1: TDF is used.
 */
struct ia_css_tdf_config {
	s32 thres_flat_table[64];	/** Final optimized strength table of NR for flat region. */
	s32 thres_detail_table[64];	/** Final optimized strength table of NR for detail region. */
	s32 epsilon_0;		/** Coefficient to control variance for dark area (for flat region). */
	s32 epsilon_1;		/** Coefficient to control variance for bright area (for flat region). */
	s32 eps_scale_text;		/** Epsilon scaling coefficient for texture region. */
	s32 eps_scale_edge;		/** Epsilon scaling coefficient for edge region. */
	s32 sepa_flat;		/** Threshold to judge flat (edge < m_Flat_thre). */
	s32 sepa_edge;		/** Threshold to judge edge (edge > m_Edge_thre). */
	s32 blend_flat;		/** Blending ratio at flat region. */
	s32 blend_text;		/** Blending ratio at texture region. */
	s32 blend_edge;		/** Blending ratio at edge region. */
	s32 shading_gain;		/** Gain of Shading control. */
	s32 shading_base_gain;	/** Base Gain of Shading control. */
	s32 local_y_gain;		/** Gain of local luminance control. */
	s32 local_y_base_gain;	/** Base gain of local luminance control. */
	s32 rad_x_origin;		/** Initial x coord. for radius computation. */
	s32 rad_y_origin;		/** Initial y coord. for radius computation. */
};

#endif /* __IA_CSS_TDF_TYPES_H */
