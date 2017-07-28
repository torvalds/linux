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

#ifndef __IA_CSS_BAYER_LS_PARAM_H
#define __IA_CSS_BAYER_LS_PARAM_H

#include "type_support.h"
#ifndef ISP2401

#define NUM_BAYER_LS 2
#define BAYER_IDX_GR 0
#define BAYER_IDX_R 1
#define BAYER_IDX_B 2
#define BAYER_IDX_GB 3
#define BAYER_QUAD_WIDTH 2
#define BAYER_QUAD_HEIGHT 2
#define NOF_BAYER_VECTORS 4

/** bayer load/store */
struct sh_css_isp_bayer_ls_isp_config {
	uint32_t base_address[NUM_BAYER_LS];
	uint32_t width[NUM_BAYER_LS];
	uint32_t height[NUM_BAYER_LS];
	uint32_t stride[NUM_BAYER_LS];
};

#else
#include "../../io_ls/common/ia_css_common_io_types.h"
#endif

#endif /* __IA_CSS_BAYER_LS_PARAM_H */
