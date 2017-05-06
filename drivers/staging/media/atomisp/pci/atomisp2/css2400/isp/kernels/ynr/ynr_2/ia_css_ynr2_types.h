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

#ifndef __IA_CSS_YNR2_TYPES_H
#define __IA_CSS_YNR2_TYPES_H

/** @file
* CSS-API header file for Y(Luma) Noise Reduction.
*/

/** Y(Luma) Noise Reduction configuration.
 *
 *  ISP block: YNR2 & YEE2
 * (ISP1: YNR1 and YEE1 are used.)
 * (ISP2: YNR1 and YEE1 are used for Preview/Video.)
 *  ISP2: YNR2 and YEE2 are used for Still.
 */
struct ia_css_ynr_config {
	uint16_t edge_sense_gain_0;   /**< Sensitivity of edge in dark area.
					u13.0, [0,8191],
					default 1000, ineffective 0 */
	uint16_t edge_sense_gain_1;   /**< Sensitivity of edge in bright area.
					u13.0, [0,8191],
					default 1000, ineffective 0 */
	uint16_t corner_sense_gain_0; /**< Sensitivity of corner in dark area.
					u13.0, [0,8191],
					default 1000, ineffective 0 */
	uint16_t corner_sense_gain_1; /**< Sensitivity of corner in bright area.
					u13.0, [0,8191],
					default 1000, ineffective 0 */
};

/** Fringe Control configuration.
 *
 *  ISP block: FC2 (FC2 is used with YNR2/YEE2.)
 * (ISP1: FC2 is not used.)
 * (ISP2: FC2 is not for Preview/Video.)
 *  ISP2: FC2 is used for Still.
 */
struct ia_css_fc_config {
	uint8_t  gain_exp;   /**< Common exponent of gains.
				u8.0, [0,13],
				default 1, ineffective 0 */
	uint16_t coring_pos_0; /**< Coring threshold for positive edge in dark area.
				u0.13, [0,8191],
				default 0(0), ineffective 0 */
	uint16_t coring_pos_1; /**< Coring threshold for positive edge in bright area.
				u0.13, [0,8191],
				default 0(0), ineffective 0 */
	uint16_t coring_neg_0; /**< Coring threshold for negative edge in dark area.
				u0.13, [0,8191],
				default 0(0), ineffective 0 */
	uint16_t coring_neg_1; /**< Coring threshold for negative edge in bright area.
				u0.13, [0,8191],
				default 0(0), ineffective 0 */
	uint16_t gain_pos_0; /**< Gain for positive edge in dark area.
				u0.13, [0,8191],
				default 4096(0.5), ineffective 0 */
	uint16_t gain_pos_1; /**< Gain for positive edge in bright area.
				u0.13, [0,8191],
				default 4096(0.5), ineffective 0 */
	uint16_t gain_neg_0; /**< Gain for negative edge in dark area.
				u0.13, [0,8191],
				default 4096(0.5), ineffective 0 */
	uint16_t gain_neg_1; /**< Gain for negative edge in bright area.
				u0.13, [0,8191],
				default 4096(0.5), ineffective 0 */
	uint16_t crop_pos_0; /**< Limit for positive edge in dark area.
				u0.13, [0,8191],
				default/ineffective 8191(almost 1.0) */
	uint16_t crop_pos_1; /**< Limit for positive edge in bright area.
				u0.13, [0,8191],
				default/ineffective 8191(almost 1.0) */
	int16_t  crop_neg_0; /**< Limit for negative edge in dark area.
				s0.13, [-8192,0],
				default/ineffective -8192(-1.0) */
	int16_t  crop_neg_1; /**< Limit for negative edge in bright area.
				s0.13, [-8192,0],
				default/ineffective -8192(-1.0) */
};

#endif /* __IA_CSS_YNR2_TYPES_H */

