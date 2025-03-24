/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
