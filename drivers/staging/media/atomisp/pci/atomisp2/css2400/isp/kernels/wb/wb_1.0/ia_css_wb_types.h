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

#ifndef __IA_CSS_WB_TYPES_H
#define __IA_CSS_WB_TYPES_H

/** @file
* CSS-API header file for White Balance parameters.
*/


/** White Balance configuration (Gain Adjust).
 *
 *  ISP block: WB1
 *  ISP1: WB1 is used.
 *  ISP2: WB1 is used.
 */
struct ia_css_wb_config {
	uint32_t integer_bits; /**< Common exponent of gains.
				u8.0, [0,3],
				default 1, ineffective 1 */
	uint32_t gr;	/**< Significand of Gr gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	uint32_t r;	/**< Significand of R gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	uint32_t b;	/**< Significand of B gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
	uint32_t gb;	/**< Significand of Gb gain.
				u[integer_bits].[16-integer_bits], [0,65535],
				default/ineffective 32768(u1.15, 1.0) */
};

#endif /* __IA_CSS_WB_TYPES_H */
