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

#ifndef __IA_CSS_CTC_TYPES_H
#define __IA_CSS_CTC_TYPES_H

/** @file
* CSS-API header file for Chroma Tone Control parameters.
*/

/** Fractional bits for CTC gain (used only for ISP1).
 *
 *  IA_CSS_CTC_COEF_SHIFT(=13) includes not only the fractional bits
 *  of gain(=8), but also the bits(=5) to convert chroma
 *  from 13bit precision to 8bit precision.
 *
 *    Gain (struct ia_css_ctc_table) : u5.8
 *    Input(Chorma) : s0.12 (13bit precision)
 *    Output(Chorma): s0.7  (8bit precision)
 *    Output = (Input * Gain) >> IA_CSS_CTC_COEF_SHIFT
 */
#define IA_CSS_CTC_COEF_SHIFT          13

/** Number of elements in the CTC table. */
#define IA_CSS_VAMEM_1_CTC_TABLE_SIZE_LOG2      10
/** Number of elements in the CTC table. */
#define IA_CSS_VAMEM_1_CTC_TABLE_SIZE           (1U<<IA_CSS_VAMEM_1_CTC_TABLE_SIZE_LOG2)

/** Number of elements in the CTC table. */
#define IA_CSS_VAMEM_2_CTC_TABLE_SIZE_LOG2      8
/** Number of elements in the CTC table. */
#define IA_CSS_VAMEM_2_CTC_TABLE_SIZE           ((1U<<IA_CSS_VAMEM_2_CTC_TABLE_SIZE_LOG2) + 1)

enum ia_css_vamem_type {
	IA_CSS_VAMEM_TYPE_1,
	IA_CSS_VAMEM_TYPE_2
};

/** Chroma Tone Control configuration.
 *
 *  ISP block: CTC2 (CTC by polygonal line approximation)
 * (ISP1: CTC1 (CTC by look-up table) is used.)
 *  ISP2: CTC2 is used.
 */
struct ia_css_ctc_config {
	uint16_t y0;	/**< 1st kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t y1;	/**< 2nd kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t y2;	/**< 3rd kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t y3;	/**< 4th kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t y4;	/**< 5th kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t y5;	/**< 6th kneepoint gain.
				u[ce_gain_exp].[13-ce_gain_exp], [0,8191],
				default/ineffective 4096(0.5) */
	uint16_t ce_gain_exp;	/**< Common exponent of y-axis gain.
				u8.0, [0,13],
				default/ineffective 1 */
	uint16_t x1;	/**< 2nd kneepoint luma.
				u0.13, [0,8191], constraints: 0<x1<x2,
				default/ineffective 1024 */
	uint16_t x2;	/**< 3rd kneepoint luma.
				u0.13, [0,8191], constraints: x1<x2<x3,
				default/ineffective 2048 */
	uint16_t x3;	/**< 4th kneepoint luma.
				u0.13, [0,8191], constraints: x2<x3<x4,
				default/ineffective 6144 */
	uint16_t x4;	/**< 5tn kneepoint luma.
				u0.13, [0,8191], constraints: x3<x4<8191,
				default/ineffective 7168 */
};

union ia_css_ctc_data {
	uint16_t vamem_1[IA_CSS_VAMEM_1_CTC_TABLE_SIZE];
	uint16_t vamem_2[IA_CSS_VAMEM_2_CTC_TABLE_SIZE];
};

/** CTC table, used for Chroma Tone Control.
 *
 *  ISP block: CTC1 (CTC by look-up table)
 *  ISP1: CTC1 is used.
 * (ISP2: CTC2 (CTC by polygonal line approximation) is used.)
 */
struct ia_css_ctc_table {
	enum ia_css_vamem_type vamem_type;
	union ia_css_ctc_data data;
};

#endif /* __IA_CSS_CTC_TYPES_H */


