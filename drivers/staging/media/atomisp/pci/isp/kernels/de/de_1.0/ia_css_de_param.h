/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DE_PARAM_H
#define __IA_CSS_DE_PARAM_H

#include "type_support.h"

/* DE (Demosaic) */
struct sh_css_isp_de_params {
	s32 pixelnoise;
	s32 c1_coring_threshold;
	s32 c2_coring_threshold;
};

#endif /* __IA_CSS_DE_PARAM_H */
