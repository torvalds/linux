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

#ifndef __IA_CSS_GC2_PARAM_H
#define __IA_CSS_GC2_PARAM_H

#include "type_support.h"
/* Extend GC1 */
#include "ia_css_gc2_types.h"
#include "gc/gc_1.0/ia_css_gc_param.h"
#include "csc/csc_1.0/ia_css_csc_param.h"

#ifndef PIPE_GENERATION
#if defined(IS_VAMEM_VERSION_1)
#define SH_CSS_ISP_RGB_GAMMA_TABLE_SIZE IA_CSS_VAMEM_1_RGB_GAMMA_TABLE_SIZE
#elif defined(IS_VAMEM_VERSION_2)
#define SH_CSS_ISP_RGB_GAMMA_TABLE_SIZE IA_CSS_VAMEM_2_RGB_GAMMA_TABLE_SIZE
#else
#error "Undefined vamem version"
#endif

#else
/* For pipe generation, the size is not relevant */
#define SH_CSS_ISP_RGB_GAMMA_TABLE_SIZE 0
#endif

/* This should be vamem_data_t, but that breaks the pipe generator */
struct sh_css_isp_rgb_gamma_vamem_params {
	uint16_t gc[SH_CSS_ISP_RGB_GAMMA_TABLE_SIZE];
};

#endif /* __IA_CSS_GC2_PARAM_H */
