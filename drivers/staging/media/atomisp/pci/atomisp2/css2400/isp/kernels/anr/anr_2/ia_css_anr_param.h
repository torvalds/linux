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

#ifndef __IA_CSS_ANR2_PARAM_H
#define __IA_CSS_ANR2_PARAM_H

#include "vmem.h"
#include "ia_css_anr2_types.h"

/** Advanced Noise Reduction (ANR) thresholds */

struct ia_css_isp_anr2_params {
	VMEM_ARRAY(data, ANR_PARAM_SIZE*ISP_VEC_NELEMS);
};

#endif /* __IA_CSS_ANR2_PARAM_H */
