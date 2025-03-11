/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DE2_PARAM_H
#define __IA_CSS_DE2_PARAM_H

#include "type_support.h"

/* Reuse DE1 params and extend them */
#include "../de_1.0/ia_css_de_param.h"

/* DE (Demosaic) */
struct sh_css_isp_ecd_params {
	s32 zip_strength;
	s32 fc_strength;
	s32 fc_debias;
};

#endif /* __IA_CSS_DE2_PARAM_H */
