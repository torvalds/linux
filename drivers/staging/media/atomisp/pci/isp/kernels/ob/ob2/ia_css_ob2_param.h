/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_OB2_PARAM_H
#define __IA_CSS_OB2_PARAM_H

#include "type_support.h"

/* OB2 (Optical Black) */
struct sh_css_isp_ob2_params {
	s32 blacklevel_gr;
	s32 blacklevel_r;
	s32 blacklevel_b;
	s32 blacklevel_gb;
};

#endif /* __IA_CSS_OB2_PARAM_H */
