/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CNR2_PARAM_H
#define __IA_CSS_CNR2_PARAM_H

#include "type_support.h"

/* CNR (Chroma Noise Reduction) */
struct sh_css_isp_cnr_params {
	s32 coring_u;
	s32 coring_v;
	s32 sense_gain_vy;
	s32 sense_gain_vu;
	s32 sense_gain_vv;
	s32 sense_gain_hy;
	s32 sense_gain_hu;
	s32 sense_gain_hv;
};

#endif /* __IA_CSS_CNR2_PARAM_H */
