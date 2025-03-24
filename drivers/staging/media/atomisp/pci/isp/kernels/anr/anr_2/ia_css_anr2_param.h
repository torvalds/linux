/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_ANR2_PARAM_H
#define __IA_CSS_ANR2_PARAM_H

#include "vmem.h"
#include "ia_css_anr2_types.h"

/* Advanced Noise Reduction (ANR) thresholds */

struct ia_css_isp_anr2_params {
	VMEM_ARRAY(data, ANR_PARAM_SIZE * ISP_VEC_NELEMS);
};

#endif /* __IA_CSS_ANR2_PARAM_H */
