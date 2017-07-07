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

#ifndef __IA_CSS_CSC_PARAM_H
#define __IA_CSS_CSC_PARAM_H

#include "type_support.h"
/* CSC (Color Space Conversion) */
struct sh_css_isp_csc_params {
	uint16_t	m_shift;
	int16_t		m00;
	int16_t		m01;
	int16_t		m02;
	int16_t		m10;
	int16_t		m11;
	int16_t		m12;
	int16_t		m20;
	int16_t		m21;
	int16_t		m22;
};


#endif /* __IA_CSS_CSC_PARAM_H */
