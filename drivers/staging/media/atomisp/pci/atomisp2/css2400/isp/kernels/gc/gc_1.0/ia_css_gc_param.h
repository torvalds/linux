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

#ifndef __IA_CSS_GC_PARAM_H
#define __IA_CSS_GC_PARAM_H

#include "type_support.h"
#ifndef PIPE_GENERATION
#ifdef __ISP
#define __INLINE_VAMEM__
#endif
#include "vamem.h"
#include "ia_css_gc_types.h"

#if defined(IS_VAMEM_VERSION_1)
#define SH_CSS_ISP_GAMMA_TABLE_SIZE_LOG2 IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE_LOG2
#define SH_CSS_ISP_GC_TABLE_SIZE	 IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE
#elif defined(IS_VAMEM_VERSION_2)
#define SH_CSS_ISP_GAMMA_TABLE_SIZE_LOG2 IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE_LOG2
#define SH_CSS_ISP_GC_TABLE_SIZE	 IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE
#else
#error "Undefined vamem version"
#endif

#else
/* For pipe generation, the size is not relevant */
#define SH_CSS_ISP_GC_TABLE_SIZE 0
#endif

#define GAMMA_OUTPUT_BITS		8
#define GAMMA_OUTPUT_MAX_VAL		((1<<GAMMA_OUTPUT_BITS)-1)

/* GC (Gamma Correction) */
struct sh_css_isp_gc_params {
	int32_t gain_k1;
	int32_t gain_k2;
};

/* CE (Chroma Enhancement) */
struct sh_css_isp_ce_params {
	int32_t uv_level_min;
	int32_t uv_level_max;
};

/* This should be vamem_data_t, but that breaks the pipe generator */
struct sh_css_isp_gc_vamem_params {
	uint16_t gc[SH_CSS_ISP_GC_TABLE_SIZE];
};

#endif /* __IA_CSS_GC_PARAM_H */
