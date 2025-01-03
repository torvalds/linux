/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SC_PARAM_H
#define __IA_CSS_SC_PARAM_H

#include "type_support.h"

/* SC (Shading Corrction) */
struct sh_css_isp_sc_params {
	s32 gain_shift;
};

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
	u32 interped_gain_hor_slice_bqs[SH_CSS_SC_INTERPED_GAIN_HOR_SLICE_TIMES];
	u32 internal_frame_origin_y_bqs_on_sctbl;
};

#endif /* __IA_CSS_SC_PARAM_H */
