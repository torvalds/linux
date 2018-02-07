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

#ifndef __IA_CSS_YUV_LS_PARAM_H
#define __IA_CSS_YUV_LS_PARAM_H

#include "type_support.h"
#ifndef ISP2401

/* The number of load/store kernels in a pipeline can be greater than one.
 * A kernel can consume more than one input or can produce more
 * than one output.
 */
#define NUM_YUV_LS 2

/* YUV load/store */
struct sh_css_isp_yuv_ls_isp_config {
	unsigned base_address[NUM_YUV_LS];
	unsigned width[NUM_YUV_LS];
	unsigned height[NUM_YUV_LS];
	unsigned stride[NUM_YUV_LS];
};

#else
#include "../../io_ls/common/ia_css_common_io_types.h"
#endif

#endif /* __IA_CSS_YUV_LS_PARAM_H */
