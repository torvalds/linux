/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_CNR2_TYPES_H
#define __IA_CSS_CNR2_TYPES_H

/* @file
* CSS-API header file for Chroma Noise Reduction (CNR) parameters
*/

/* Chroma Noise Reduction configuration.
 *
 *  Small sensitivity of edge means strong smoothness and NR performance.
 *  If you see blurred color on vertical edges,
 *  set higher values on sense_gain_h*.
 *  If you see blurred color on horizontal edges,
 *  set higher values on sense_gain_v*.
 *
 *  ISP block: CNR2
 * (ISP1: CNR1 is used.)
 * (ISP2: CNR1 is used for Preview/Video.)
 *  ISP2: CNR2 is used for Still.
 */
struct ia_css_cnr_config {
	u16 coring_u;	/** Coring level of U.
				u0.13, [0,8191], default/ineffective 0 */
	u16 coring_v;	/** Coring level of V.
				u0.13, [0,8191], default/ineffective 0 */
	u16 sense_gain_vy;	/** Sensitivity of horizontal edge of Y.
				u13.0, [0,8191], default 100, ineffective 8191 */
	u16 sense_gain_vu;	/** Sensitivity of horizontal edge of U.
				u13.0, [0,8191], default 100, ineffective 8191 */
	u16 sense_gain_vv;	/** Sensitivity of horizontal edge of V.
				u13.0, [0,8191], default 100, ineffective 8191 */
	u16 sense_gain_hy;	/** Sensitivity of vertical edge of Y.
				u13.0, [0,8191], default 50, ineffective 8191 */
	u16 sense_gain_hu;	/** Sensitivity of vertical edge of U.
				u13.0, [0,8191], default 50, ineffective 8191 */
	u16 sense_gain_hv;	/** Sensitivity of vertical edge of V.
				u13.0, [0,8191], default 50, ineffective 8191 */
};

#endif /* __IA_CSS_CNR2_TYPES_H */
