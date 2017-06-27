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

#ifndef __IA_CSS_OB_PARAM_H
#define __IA_CSS_OB_PARAM_H

#include "type_support.h"
#include "vmem.h"

#define OBAREA_MASK_SIZE 64
#define OBAREA_LENGTHBQ_INVERSE_SHIFT     12

/* AREA_LENGTH_UNIT is dependent on NWAY, requires rewrite */
#define AREA_LENGTH_UNIT (1<<12)


/* OB (Optical Black) */
struct sh_css_isp_ob_stream_config {
	unsigned isp_pipe_version;
	unsigned raw_bit_depth;
};

struct sh_css_isp_ob_params {
	int32_t blacklevel_gr;
	int32_t blacklevel_r;
	int32_t blacklevel_b;
	int32_t blacklevel_gb;
	int32_t area_start_bq;
	int32_t area_length_bq;
	int32_t area_length_bq_inverse;
};

struct sh_css_isp_ob_vmem_params {
	VMEM_ARRAY(vmask, OBAREA_MASK_SIZE);
};

#endif /* __IA_CSS_OB_PARAM_H */
