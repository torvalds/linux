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

#ifndef __IA_CSS_SATM_PARAMS_H
#define __IA_CSS_SATM_PARAMS_H

#include "type_support.h"

/* SATM parameters on ISP. */
struct sh_css_satm_params {
	int32_t test_satm;
};

/* SATM ISP parameters */
struct sh_css_isp_satm_params {
	struct sh_css_satm_params params;
};

#endif /* __IA_CSS_SATM_PARAMS_H */
