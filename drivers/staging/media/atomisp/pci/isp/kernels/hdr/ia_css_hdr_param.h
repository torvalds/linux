/* SPDX-License-Identifier: GPL-2.0 */
/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
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

#ifndef __IA_CSS_HDR_PARAMS_H
#define __IA_CSS_HDR_PARAMS_H

#include "type_support.h"

#define HDR_NUM_INPUT_FRAMES         (3)

/* HDR irradiance map parameters on ISP. */
struct sh_css_hdr_irradiance_params {
	s32 test_irr;
	s32 match_shift[HDR_NUM_INPUT_FRAMES -
					     1];  /* Histogram matching shift parameter */
	s32 match_mul[HDR_NUM_INPUT_FRAMES -
					   1];    /* Histogram matching multiplication parameter */
	s32 thr_low[HDR_NUM_INPUT_FRAMES -
					 1];      /* Weight map soft threshold low bound parameter */
	s32 thr_high[HDR_NUM_INPUT_FRAMES -
					  1];     /* Weight map soft threshold high bound parameter */
	s32 thr_coeff[HDR_NUM_INPUT_FRAMES -
					   1];    /* Soft threshold linear function coefficient */
	s32 thr_shift[HDR_NUM_INPUT_FRAMES -
					   1];    /* Soft threshold precision shift parameter */
	s32 weight_bpp;                             /* Weight map bits per pixel */
};

/* HDR deghosting parameters on ISP */
struct sh_css_hdr_deghost_params {
	s32 test_deg;
};

/* HDR exclusion parameters on ISP */
struct sh_css_hdr_exclusion_params {
	s32 test_excl;
};

/* HDR ISP parameters */
struct sh_css_isp_hdr_params {
	struct sh_css_hdr_irradiance_params irradiance;
	struct sh_css_hdr_deghost_params    deghost;
	struct sh_css_hdr_exclusion_params  exclusion;
};

#endif /* __IA_CSS_HDR_PARAMS_H */
