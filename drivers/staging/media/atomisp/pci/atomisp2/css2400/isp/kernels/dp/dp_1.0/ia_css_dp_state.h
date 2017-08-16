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

#ifndef __IA_CSS_DP_STATE_H
#define __IA_CSS_DP_STATE_H

#include "type_support.h"

#include "vmem.h"
#ifndef ISP2401
#if NEED_BDS_OTHER_THAN_1_00
#else
#if ENABLE_FIXED_BAYER_DS
#endif
#define MAX_VECTORS_PER_DP_LINE MAX_VECTORS_PER_BUF_INPUT_LINE
#else
#define MAX_VECTORS_PER_DP_LINE MAX_VECTORS_PER_BUF_LINE
#endif

/* DP (Defect Pixel Correction) */
struct sh_css_isp_dp_vmem_state {
	VMEM_ARRAY(dp_buf[4], MAX_VECTORS_PER_DP_LINE*ISP_NWAY);
};

#endif /* __IA_CSS_DP_STATE_H */
