/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SDIS2_TYPES_H
#define __IA_CSS_SDIS2_TYPES_H

/* @file
* CSS-API header file for DVS statistics parameters.
*/

/* Number of DVS coefficient types */
#define IA_CSS_DVS2_NUM_COEF_TYPES     4

#ifndef PIPE_GENERATION
#include "isp/kernels/sdis/common/ia_css_sdis_common_types.h"
#endif

/* DVS 2.0 Coefficient types. This structure contains 4 pointers to
 *  arrays that contain the coefficients for each type.
 */
struct ia_css_dvs2_coef_types {
	s16 *odd_real; /** real part of the odd coefficients*/
	s16 *odd_imag; /** imaginary part of the odd coefficients*/
	s16 *even_real;/** real part of the even coefficients*/
	s16 *even_imag;/** imaginary part of the even coefficients*/
};

/* DVS 2.0 Coefficients. This structure describes the coefficients that are needed for the dvs statistics.
 *  e.g. hor_coefs.odd_real is the pointer to int16_t[grid.num_hor_coefs] containing the horizontal odd real
 *  coefficients.
 */
struct ia_css_dvs2_coefficients {
	struct ia_css_dvs_grid_info
		grid;        /** grid info contains the dimensions of the dvs grid */
	struct ia_css_dvs2_coef_types
		hor_coefs; /** struct with pointers that contain the horizontal coefficients */
	struct ia_css_dvs2_coef_types
		ver_coefs; /** struct with pointers that contain the vertical coefficients */
};

/* DVS 2.0 Statistic types. This structure contains 4 pointers to
 *  arrays that contain the statistics for each type.
 */
struct ia_css_dvs2_stat_types {
	s32 *odd_real; /** real part of the odd statistics*/
	s32 *odd_imag; /** imaginary part of the odd statistics*/
	s32 *even_real;/** real part of the even statistics*/
	s32 *even_imag;/** imaginary part of the even statistics*/
};

/* DVS 2.0 Statistics. This structure describes the statistics that are generated using the provided coefficients.
 *  e.g. hor_prod.odd_real is the pointer to int16_t[grid.aligned_height][grid.aligned_width] containing
 *  the horizontal odd real statistics. Valid statistics data area is int16_t[0..grid.height-1][0..grid.width-1]
 */
struct ia_css_dvs2_statistics {
	struct ia_css_dvs_grid_info
		grid;       /** grid info contains the dimensions of the dvs grid */
	struct ia_css_dvs2_stat_types
		hor_prod; /** struct with pointers that contain the horizontal statistics */
	struct ia_css_dvs2_stat_types
		ver_prod; /** struct with pointers that contain the vertical statistics */
};

#endif /* __IA_CSS_SDIS2_TYPES_H */
