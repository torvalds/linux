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

#ifndef __IA_CSS_XNR3_0_11_TYPES_H
#define __IA_CSS_XNR3_0_11_TYPES_H

 /*
 * STRUCT ia_css_xnr3_0_11_config
 * -----------------------------------------------
 * Struct with all parameters for the XNR3.0.11 kernel that can be set
 * from the CSS API
 */
struct ia_css_xnr3_0_11_config {
	int32_t weight_y0;     /**< Weight for Y range similarity in dark area */
	int32_t weight_y1;     /**< Weight for Y range similarity in bright area */
	int32_t weight_u0;     /**< Weight for U range similarity in dark area */
	int32_t weight_u1;     /**< Weight for U range similarity in bright area */
	int32_t weight_v0;     /**< Weight for V range similarity in dark area */
	int32_t weight_v1;     /**< Weight for V range similarity in bright area */
};

#endif /* __IA_CSS_XNR3_0_11_TYPES_H */
