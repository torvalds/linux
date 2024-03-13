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

#ifndef __SH_CSS_FRAC_H
#define __SH_CSS_FRAC_H

#include <math_support.h>

#define sISP_REG_BIT		      ISP_VEC_ELEMBITS
#define uISP_REG_BIT		      ((unsigned int)(sISP_REG_BIT - 1))
#define sSHIFT				    (16 - sISP_REG_BIT)
#define uSHIFT				    ((unsigned int)(16 - uISP_REG_BIT))
#define sFRACTION_BITS_FITTING(a) (a - sSHIFT)
#define uFRACTION_BITS_FITTING(a) ((unsigned int)(a - uSHIFT))
#define sISP_VAL_MIN		      (-(1 << uISP_REG_BIT))
#define sISP_VAL_MAX		      ((1 << uISP_REG_BIT) - 1)
#define uISP_VAL_MIN		      (0U)
#define uISP_VAL_MAX		      ((unsigned int)((1 << uISP_REG_BIT) - 1))

/* a:fraction bits for 16bit precision, b:fraction bits for ISP precision */
#define sDIGIT_FITTING(v, a, b) \
	min_t(int, max_t(int, (((v) >> sSHIFT) >> max(sFRACTION_BITS_FITTING(a) - (b), 0)), \
	  sISP_VAL_MIN), sISP_VAL_MAX)
#define uDIGIT_FITTING(v, a, b) \
	min((unsigned int)max((unsigned)(((v) >> uSHIFT) \
	>> max((int)(uFRACTION_BITS_FITTING(a) - (b)), 0)), \
	  uISP_VAL_MIN), uISP_VAL_MAX)

#endif /* __SH_CSS_FRAC_H */
