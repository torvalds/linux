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

#ifndef __IA_CSS_S3A_STAT_LS_PARAM_H
#define __IA_CSS_S3A_STAT_LS_PARAM_H

#include "type_support.h"
#ifdef ISP2401
#include "../../io_ls/common/ia_css_common_io_types.h"
#endif

#define NUM_S3A_LS 1

/** s3a statistics store */
#ifdef ISP2401
struct ia_css_s3a_stat_ls_configuration {
	uint32_t s3a_grid_size_log2;
};

#endif
struct sh_css_isp_s3a_stat_ls_isp_config {
#ifndef ISP2401
	uint32_t base_address[NUM_S3A_LS];
	uint32_t width[NUM_S3A_LS];
	uint32_t height[NUM_S3A_LS];
	uint32_t stride[NUM_S3A_LS];
#endif
	uint32_t s3a_grid_size_log2[NUM_S3A_LS];
};

#ifndef ISP2401

#endif
#endif /* __IA_CSS_S3A_STAT_LS_PARAM_H */
