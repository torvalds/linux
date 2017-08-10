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
#define uISP_REG_BIT		      ((unsigned)(sISP_REG_BIT-1))
#define sSHIFT				    (16-sISP_REG_BIT)
#define uSHIFT				    ((unsigned)(16-uISP_REG_BIT))
#define sFRACTION_BITS_FITTING(a) (a-sSHIFT)
#define uFRACTION_BITS_FITTING(a) ((unsigned)(a-uSHIFT))
#define sISP_VAL_MIN		      (-(1<<uISP_REG_BIT))
#define sISP_VAL_MAX		      ((1<<uISP_REG_BIT)-1)
#define uISP_VAL_MIN		      ((unsigned)0)
#define uISP_VAL_MAX		      ((unsigned)((1<<uISP_REG_BIT)-1))

/* a:fraction bits for 16bit precision, b:fraction bits for ISP precision */
#define sDIGIT_FITTING(v, a, b) \
	min(max((((v)>>sSHIFT) >> max(sFRACTION_BITS_FITTING(a)-(b), 0)), \
	  sISP_VAL_MIN), sISP_VAL_MAX)
#define uDIGIT_FITTING(v, a, b) \
	min((unsigned)max((unsigned)(((v)>>uSHIFT) \
	>> max((int)(uFRACTION_BITS_FITTING(a)-(b)), 0)), \
	  uISP_VAL_MIN), uISP_VAL_MAX)

#endif /* __SH_CSS_FRAC_H */
