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

#ifndef __IA_CSS_FPN_TYPES_H
#define __IA_CSS_FPN_TYPES_H

/* @file
* CSS-API header file for Fixed Pattern Noise parameters.
*/

/* Fixed Pattern Noise table.
 *
 *  This contains the fixed patterns noise values
 *  obtained from a black frame capture.
 *
 *  "shift" should be set as the smallest value
 *  which satisfies the requirement the maximum data is less than 64.
 *
 *  ISP block: FPN1
 *  ISP1: FPN1 is used.
 *  ISP2: FPN1 is used.
 */

struct ia_css_fpn_table {
	s16 *data;		/** Table content (fixed patterns noise).
					u0.[13-shift], [0,63] */
	u32 width;		/** Table width (in pixels).
					This is the input frame width. */
	u32 height;	/** Table height (in pixels).
					This is the input frame height. */
	u32 shift;		/** Common exponent of table content.
					u8.0, [0,13] */
	u32 enabled;	/** Fpn is enabled.
					bool */
};

struct ia_css_fpn_configuration {
	const struct ia_css_frame_info *info;
};

#endif /* __IA_CSS_FPN_TYPES_H */
