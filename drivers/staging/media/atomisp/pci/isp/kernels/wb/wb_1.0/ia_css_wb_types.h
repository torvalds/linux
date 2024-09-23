/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_WB_TYPES_H
#define __IA_CSS_WB_TYPES_H

/* @file
* CSS-API header file for White Balance parameters.
*/

/* White Balance configuration (Gain Adjust).
 *
 *  ISP block: WB1
 *  ISP1: WB1 is used.
 *  ISP2: WB1 is used.
 */
struct ia_css_wb_config {
	u32 integer_bits; /** Common exponent of gains.
				u8.0, [0,3],
				default 1, ineffective 1 */
	u32 gr;	/** Significand of Gr gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	u32 r;	/** Significand of R gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	u32 b;	/** Significand of B gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	u32 gb;	/** Significand of Gb gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
};

#endif /* __IA_CSS_WB_TYPES_H */
