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

#ifndef __IA_CSS_XNR3_TYPES_H
#define __IA_CSS_XNR3_TYPES_H

/* @file
* CSS-API header file for Extra Noise Reduction (XNR) parameters.
*/

/**
 * \brief Scale of the XNR sigma parameters.
 * \details The define specifies which fixed-point value represents 1.0.
 */
#define IA_CSS_XNR3_SIGMA_SCALE  (1 << 10)

/**
 * \brief Scale of the XNR coring parameters.
 * \details The define specifies which fixed-point value represents 1.0.
 */
#define IA_CSS_XNR3_CORING_SCALE (1 << 15)

/**
 * \brief Scale of the XNR blending parameter.
 * \details The define specifies which fixed-point value represents 1.0.
 */
#define IA_CSS_XNR3_BLENDING_SCALE (1 << 11)


/**
 * \brief XNR3 Sigma Parameters.
 * \details Sigma parameters define the strength of the XNR filter.
 * A higher number means stronger filtering. There are two values for each of
 * the three YUV planes: one for dark areas and one for bright areas. All
 * sigma parameters are fixed-point values between 0.0 and 1.0, scaled with
 * IA_CSS_XNR3_SIGMA_SCALE.
 */
struct ia_css_xnr3_sigma_params {
	int y0;     /** Sigma for Y range similarity in dark area */
	int y1;     /** Sigma for Y range similarity in bright area */
	int u0;     /** Sigma for U range similarity in dark area */
	int u1;     /** Sigma for U range similarity in bright area */
	int v0;     /** Sigma for V range similarity in dark area */
	int v1;     /** Sigma for V range similarity in bright area */
};

/**
 * \brief XNR3 Coring Parameters
 * \details Coring parameters define the "coring" strength, which is a soft
 * thresholding technique to avoid false coloring. There are two values for
 * each of the two chroma planes: one for dark areas and one for bright areas.
 * All coring parameters are fixed-point values between 0.0 and 1.0, scaled
 * with IA_CSS_XNR3_CORING_SCALE. The ineffective value is 0.
 */
struct ia_css_xnr3_coring_params {
	int u0;     /** Coring threshold of U channel in dark area */
	int u1;     /** Coring threshold of U channel in bright area */
	int v0;     /** Coring threshold of V channel in dark area */
	int v1;     /** Coring threshold of V channel in bright area */
};

/**
 * \brief XNR3 Blending Parameters
 * \details Blending parameters define the blending strength of filtered
 * output pixels with the original chroma pixels from before xnr3. The
 * blending strength is a fixed-point value between 0.0 and 1.0 (inclusive),
 * scaled with IA_CSS_XNR3_BLENDING_SCALE.
 * A higher number applies xnr filtering more strongly. A value of 1.0
 * disables the blending and returns the xnr3 filtered output, while a
 * value of 0.0 bypasses the entire xnr3 filter.
 */
struct ia_css_xnr3_blending_params {
	int strength;   /** Blending strength */
};

/**
 * \brief XNR3 public parameters.
 * \details Struct with all parameters for the XNR3 kernel that can be set
 * from the CSS API.
 */
struct ia_css_xnr3_config {
	struct ia_css_xnr3_sigma_params    sigma;    /** XNR3 sigma parameters */
	struct ia_css_xnr3_coring_params   coring;   /** XNR3 coring parameters */
	struct ia_css_xnr3_blending_params blending; /** XNR3 blending parameters */
};

#endif /* __IA_CSS_XNR3_TYPES_H */
