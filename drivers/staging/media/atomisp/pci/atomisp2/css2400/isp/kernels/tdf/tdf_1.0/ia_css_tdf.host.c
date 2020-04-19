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

#include "ia_css_debug.h"
#include "ia_css_tdf.host.h"

static const int16_t g_pyramid[8][8] = {
{128, 384, 640, 896, 896, 640, 384, 128},
{384, 1152, 1920, 2688, 2688, 1920, 1152, 384},
{640, 1920, 3200, 4480, 4480, 3200, 1920, 640},
{896, 2688, 4480, 6272, 6272, 4480, 2688, 896},
{896, 2688, 4480, 6272, 6272, 4480, 2688, 896},
{640, 1920, 3200, 4480, 4480, 3200, 1920, 640},
{384, 1152, 1920, 2688, 2688, 1920, 1152, 384},
{128, 384, 640, 896, 896, 640, 384, 128}
};

void
ia_css_tdf_vmem_encode(
	struct ia_css_isp_tdf_vmem_params *to,
	const struct ia_css_tdf_config *from,
	size_t size)
{
	unsigned i;
	(void)size;

	for (i = 0; i < ISP_VEC_NELEMS; i++) {
		to->pyramid[0][i]          = g_pyramid[i/8][i%8];
		to->threshold_flat[0][i]   = from->thres_flat_table[i];
		to->threshold_detail[0][i] = from->thres_detail_table[i];
	}

}

void
ia_css_tdf_encode(
	struct ia_css_isp_tdf_dmem_params *to,
	const struct ia_css_tdf_config *from,
	size_t size)
{
	(void)size;
	to->Epsilon_0        = from->epsilon_0;
	to->Epsilon_1        = from->epsilon_1;
	to->EpsScaleText     = from->eps_scale_text;
	to->EpsScaleEdge     = from->eps_scale_edge;
	to->Sepa_flat	     = from->sepa_flat;
	to->Sepa_Edge	     = from->sepa_edge;
	to->Blend_Flat	     = from->blend_flat;
	to->Blend_Text	     = from->blend_text;
	to->Blend_Edge	     = from->blend_edge;
	to->Shading_Gain     = from->shading_gain;
	to->Shading_baseGain = from->shading_base_gain;
	to->LocalY_Gain      = from->local_y_gain;
	to->LocalY_baseGain  = from->local_y_base_gain;
}

void
ia_css_tdf_debug_dtrace(
	const struct ia_css_tdf_config *config,
	unsigned level)
{
	(void)config;
	(void)level;
}

