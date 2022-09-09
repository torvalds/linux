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

#ifndef __IA_CSS_CSC_PARAM_H
#define __IA_CSS_CSC_PARAM_H

#include "type_support.h"
/* CSC (Color Space Conversion) */
struct sh_css_isp_csc_params {
	u16	m_shift;
	s16		m00;
	s16		m01;
	s16		m02;
	s16		m10;
	s16		m11;
	s16		m12;
	s16		m20;
	s16		m21;
	s16		m22;
};

#endif /* __IA_CSS_CSC_PARAM_H */
