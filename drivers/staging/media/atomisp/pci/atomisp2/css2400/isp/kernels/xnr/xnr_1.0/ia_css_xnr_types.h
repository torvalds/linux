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

#ifndef __IA_CSS_XNR_TYPES_H
#define __IA_CSS_XNR_TYPES_H

/* @file
* CSS-API header file for Extra Noise Reduction (XNR) parameters.
*/

/* XNR table.
 *
 *  NOTE: The driver does not need to set this table,
 *        because the default values are set inside the css.
 *
 *  This table contains coefficients used for division in XNR.
 *
 *  	u0.12, [0,4095],
 *      {4095, 2048, 1365, .........., 65, 64}
 *      ({1/1, 1/2, 1/3, ............., 1/63, 1/64})
 *
 *  ISP block: XNR1
 *  ISP1: XNR1 is used.
 *  ISP2: XNR1 is used.
 *
 */

/* Number of elements in the xnr table. */
#define IA_CSS_VAMEM_1_XNR_TABLE_SIZE_LOG2      6
/* Number of elements in the xnr table. */
#define IA_CSS_VAMEM_1_XNR_TABLE_SIZE           (1U<<IA_CSS_VAMEM_1_XNR_TABLE_SIZE_LOG2)

/* Number of elements in the xnr table. */
#define IA_CSS_VAMEM_2_XNR_TABLE_SIZE_LOG2      6
/* Number of elements in the xnr table. */
#define IA_CSS_VAMEM_2_XNR_TABLE_SIZE	        (1U<<IA_CSS_VAMEM_2_XNR_TABLE_SIZE_LOG2)

/** IA_CSS_VAMEM_TYPE_1(ISP2300) or
     IA_CSS_VAMEM_TYPE_2(ISP2400) */
union ia_css_xnr_data {
	uint16_t vamem_1[IA_CSS_VAMEM_1_XNR_TABLE_SIZE];
	/** Coefficients table on vamem type1. u0.12, [0,4095] */
	uint16_t vamem_2[IA_CSS_VAMEM_2_XNR_TABLE_SIZE];
	/** Coefficients table on vamem type2. u0.12, [0,4095] */
};

struct ia_css_xnr_table {
	enum ia_css_vamem_type vamem_type;
	union ia_css_xnr_data data;
};

struct ia_css_xnr_config {
	/* XNR threshold.
	 * type:u0.16 valid range:[0,65535]
	 * default: 6400 */
	uint16_t threshold;
};

#endif /* __IA_CSS_XNR_TYPES_H */

