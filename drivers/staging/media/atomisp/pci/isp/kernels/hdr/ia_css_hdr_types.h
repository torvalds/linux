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

#ifndef __IA_CSS_HDR_TYPES_H
#define __IA_CSS_HDR_TYPES_H

#define IA_CSS_HDR_MAX_NUM_INPUT_FRAMES         (3)

/**
 * \brief HDR Irradiance Parameters
 * \detail Currently HDR parameters are used only for testing purposes
 */
struct ia_css_hdr_irradiance_params {
	int test_irr;                                          /** Test parameter */
	int match_shift[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
							1];  /** Histogram matching shift parameter */
	int match_mul[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
						      1];    /** Histogram matching multiplication parameter */
	int thr_low[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
						    1];      /** Weight map soft threshold low bound parameter */
	int thr_high[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
						     1];     /** Weight map soft threshold high bound parameter */
	int thr_coeff[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
						      1];    /** Soft threshold linear function coefficien */
	int thr_shift[IA_CSS_HDR_MAX_NUM_INPUT_FRAMES -
						      1];    /** Soft threshold precision shift parameter */
	int weight_bpp;                                        /** Weight map bits per pixel */
};

/**
 * \brief HDR Deghosting Parameters
 * \detail Currently HDR parameters are used only for testing purposes
 */
struct ia_css_hdr_deghost_params {
	int test_deg; /** Test parameter */
};

/**
 * \brief HDR Exclusion Parameters
 * \detail Currently HDR parameters are used only for testing purposes
 */
struct ia_css_hdr_exclusion_params {
	int test_excl; /** Test parameter */
};

/**
 * \brief HDR public paramterers.
 * \details Struct with all parameters for HDR that can be seet from
 * the CSS API. Currenly, only test parameters are defined.
 */
struct ia_css_hdr_config {
	struct ia_css_hdr_irradiance_params irradiance; /** HDR irradiance parameters */
	struct ia_css_hdr_deghost_params    deghost;    /** HDR deghosting parameters */
	struct ia_css_hdr_exclusion_params  exclusion; /** HDR exclusion parameters */
};

#endif /* __IA_CSS_HDR_TYPES_H */
