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

#ifndef __IA_CSS_GC2_HOST_H
#define __IA_CSS_GC2_HOST_H

#include "ia_css_gc2_types.h"
#include "ia_css_gc2_param.h"
#include "ia_css_gc2_table.host.h"

extern const struct ia_css_cc_config default_yuv2rgb_cc_config;
extern const struct ia_css_cc_config default_rgb2yuv_cc_config;

void
ia_css_yuv2rgb_encode(
	struct sh_css_isp_csc_params *to,
	const struct ia_css_cc_config *from,
	unsigned size);

void
ia_css_rgb2yuv_encode(
	struct sh_css_isp_csc_params *to,
	const struct ia_css_cc_config *from,
	unsigned size);

void
ia_css_r_gamma_vamem_encode(
	struct sh_css_isp_rgb_gamma_vamem_params *to,
	const struct ia_css_rgb_gamma_table *from,
	unsigned size);

void
ia_css_g_gamma_vamem_encode(
	struct sh_css_isp_rgb_gamma_vamem_params *to,
	const struct ia_css_rgb_gamma_table *from,
	unsigned size);

void
ia_css_b_gamma_vamem_encode(
	struct sh_css_isp_rgb_gamma_vamem_params *to,
	const struct ia_css_rgb_gamma_table *from,
	unsigned size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_yuv2rgb_dump(
	const struct sh_css_isp_csc_params *yuv2rgb,
	unsigned level);

void
ia_css_rgb2yuv_dump(
	const struct sh_css_isp_csc_params *rgb2yuv,
	unsigned level);

void
ia_css_rgb_gamma_table_debug_dtrace(
	const struct ia_css_rgb_gamma_table *config,
	unsigned level);

#define ia_css_yuv2rgb_debug_dtrace ia_css_cc_config_debug_dtrace
#define ia_css_rgb2yuv_debug_dtrace ia_css_cc_config_debug_dtrace
#define ia_css_r_gamma_debug_dtrace ia_css_rgb_gamma_table_debug_dtrace
#define ia_css_g_gamma_debug_dtrace ia_css_rgb_gamma_table_debug_dtrace
#define ia_css_b_gamma_debug_dtrace ia_css_rgb_gamma_table_debug_dtrace

#endif

#endif /* __IA_CSS_GC2_HOST_H */
