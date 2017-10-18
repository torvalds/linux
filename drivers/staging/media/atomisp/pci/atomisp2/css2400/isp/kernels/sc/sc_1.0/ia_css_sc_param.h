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

#ifndef __IA_CSS_SC_PARAM_H
#define __IA_CSS_SC_PARAM_H

#include "type_support.h"

#ifdef ISP2401
/* To position the shading center grid point on the center of output image,
 * one more grid cell is needed as margin. */
#define SH_CSS_SCTBL_CENTERING_MARGIN	1

/* The shading table width and height are the number of grids, not cells. The last grid should be counted. */
#define SH_CSS_SCTBL_LAST_GRID_COUNT	1

/* Number of horizontal grids per color in the shading table. */
#define _ISP_SCTBL_WIDTH_PER_COLOR(input_width, deci_factor_log2) \
	(ISP_BQ_GRID_WIDTH(input_width, deci_factor_log2) + \
	SH_CSS_SCTBL_CENTERING_MARGIN + SH_CSS_SCTBL_LAST_GRID_COUNT)

/* Number of vertical grids per color in the shading table. */
#define _ISP_SCTBL_HEIGHT(input_height, deci_factor_log2) \
	(ISP_BQ_GRID_HEIGHT(input_height, deci_factor_log2) + \
	SH_CSS_SCTBL_CENTERING_MARGIN + SH_CSS_SCTBL_LAST_GRID_COUNT)

/* Legacy API: Number of horizontal grids per color in the shading table. */
#define _ISP_SCTBL_LEGACY_WIDTH_PER_COLOR(input_width, deci_factor_log2) \
	(ISP_BQ_GRID_WIDTH(input_width, deci_factor_log2) + SH_CSS_SCTBL_LAST_GRID_COUNT)

/* Legacy API: Number of vertical grids per color in the shading table. */
#define _ISP_SCTBL_LEGACY_HEIGHT(input_height, deci_factor_log2) \
	(ISP_BQ_GRID_HEIGHT(input_height, deci_factor_log2) + SH_CSS_SCTBL_LAST_GRID_COUNT)

#endif
/* SC (Shading Corrction) */
struct sh_css_isp_sc_params {
	int32_t gain_shift;
};

#ifdef ISP2401
/* Number of horizontal slice times for interpolated gain:
 *
 * The start position of the internal frame does not match the start position of the shading table.
 * To get a vector of shading gains (interpolated horizontally and vertically)
 * which matches a vector on the internal frame,
 * vec_slice is used for 2 adjacent vectors of shading gains.
 * The number of shift times by vec_slice is 8.
 *     Max grid cell bqs to support the shading table centerting: N = 32
 *     CEIL_DIV(N-1, ISP_SLICE_NELEMS) = CEIL_DIV(31, 4) = 8
 */
#define SH_CSS_SC_INTERPED_GAIN_HOR_SLICE_TIMES   8

struct sh_css_isp_sc_isp_config {
	uint32_t interped_gain_hor_slice_bqs[SH_CSS_SC_INTERPED_GAIN_HOR_SLICE_TIMES];
	uint32_t internal_frame_origin_y_bqs_on_sctbl;
};

#endif
#endif /* __IA_CSS_SC_PARAM_H */
