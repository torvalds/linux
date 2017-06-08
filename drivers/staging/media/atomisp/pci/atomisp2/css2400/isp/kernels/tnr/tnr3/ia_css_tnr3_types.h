#ifdef ISP2401
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/

#ifndef _IA_CSS_TNR3_TYPES_H
#define _IA_CSS_TNR3_TYPES_H

/** @file
* CSS-API header file for Temporal Noise Reduction v3 (TNR3) kernel
*/

/**
 * \brief Number of piecewise linear segments.
 * \details The parameters to TNR3 are specified as a piecewise linear segment.
 * The number of such segments is fixed at 3.
 */
#define TNR3_NUM_SEGMENTS    3

/** Temporal Noise Reduction v3 (TNR3) configuration.
 * The parameter to this kernel is fourfold
 * 1. Three piecewise linear graphs (one for each plane) with three segments
 * each. Each line graph has Luma values on the x axis and sigma values for
 * each plane on the y axis. The three linear segments may have a different
 * slope and the point of Luma value which where the slope may change is called
 * a "Knee" point. As there are three such segments, four points need to be
 * specified each on the Luma axis and the per plane Sigma axis. On the Luma
 * axis two points are fixed (namely 0 and maximum luma value - depending on
 * ISP bit depth). The other two points are the points where the slope may
 * change its value. These two points are called knee points. The four points on
 * the per plane sigma axis are also specified at the interface.
 * 2. One rounding adjustment parameter for each plane
 * 3. One maximum feedback threshold value for each plane
 * 4. Selection of the reference frame buffer to be used for noise reduction.
 */
struct ia_css_tnr3_kernel_config {
	unsigned int maxfb_y;                        /**< Maximum Feedback Gain for Y */
	unsigned int maxfb_u;                        /**< Maximum Feedback Gain for U */
	unsigned int maxfb_v;                        /**< Maximum Feedback Gain for V */
	unsigned int round_adj_y;                    /**< Rounding Adjust for Y */
	unsigned int round_adj_u;                    /**< Rounding Adjust for U */
	unsigned int round_adj_v;                    /**< Rounding Adjust for V */
	unsigned int knee_y[TNR3_NUM_SEGMENTS - 1];  /**< Knee points */
	unsigned int sigma_y[TNR3_NUM_SEGMENTS + 1]; /**< Standard deviation for Y at points Y0, Y1, Y2, Y3 */
	unsigned int sigma_u[TNR3_NUM_SEGMENTS + 1]; /**< Standard deviation for U at points U0, U1, U2, U3 */
	unsigned int sigma_v[TNR3_NUM_SEGMENTS + 1]; /**< Standard deviation for V at points V0, V1, V2, V3 */
	unsigned int ref_buf_select;                 /**< Selection of the reference buffer */
};

#endif
#endif
