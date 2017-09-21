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

#include "ia_css_types.h"
#include "ia_css_frame.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "sh_css_frac.h"
#include "assert_support.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"

#include "ia_css_tnr.host.h"
const struct ia_css_tnr_config default_tnr_config = {
	32768,
	32,
	32,
};

void
ia_css_tnr_encode(
	struct sh_css_isp_tnr_params *to,
	const struct ia_css_tnr_config *from,
	unsigned size)
{
	(void)size;
	to->coef =
	    uDIGIT_FITTING(from->gain, 16, SH_CSS_TNR_COEF_SHIFT);
	to->threshold_Y =
	    uDIGIT_FITTING(from->threshold_y, 16, SH_CSS_ISP_YUV_BITS);
	to->threshold_C =
	    uDIGIT_FITTING(from->threshold_uv, 16, SH_CSS_ISP_YUV_BITS);
}

void
ia_css_tnr_dump(
	const struct sh_css_isp_tnr_params *tnr,
	unsigned level)
{
	if (!tnr) return;
	ia_css_debug_dtrace(level, "Temporal Noise Reduction:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			"tnr_coef", tnr->coef);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			"tnr_threshold_Y", tnr->threshold_Y);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			"tnr_threshold_C", tnr->threshold_C);
}

void
ia_css_tnr_debug_dtrace(
	const struct ia_css_tnr_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.gain=%d, "
		"config.threshold_y=%d, config.threshold_uv=%d\n",
		config->gain,
		config->threshold_y, config->threshold_uv);
}

void
ia_css_tnr_config(
	struct sh_css_isp_tnr_isp_config *to,
	const struct ia_css_tnr_configuration *from,
	unsigned size)
{
	unsigned elems_a = ISP_VEC_NELEMS;
	unsigned i;

	(void)size;
	ia_css_dma_configure_from_info(&to->port_b, &from->tnr_frames[0]->info);
	to->width_a_over_b = elems_a / to->port_b.elems;
	to->frame_height = from->tnr_frames[0]->info.res.height;
#ifndef ISP2401
	for (i = 0; i < NUM_VIDEO_TNR_FRAMES; i++) {
#else
	for (i = 0; i < NUM_TNR_FRAMES; i++) {
#endif
		to->tnr_frame_addr[i] = from->tnr_frames[i]->data + from->tnr_frames[i]->planes.yuyv.offset;
	}

	/* Assume divisiblity here, may need to generalize to fixed point. */
	assert (elems_a % to->port_b.elems == 0);
}

void
ia_css_tnr_configure(
	const struct ia_css_binary     *binary,
	const struct ia_css_frame **frames)
{
	struct ia_css_tnr_configuration config;
	unsigned i;

#ifndef ISP2401
	for (i = 0; i < NUM_VIDEO_TNR_FRAMES; i++)
#else
	for (i = 0; i < NUM_TNR_FRAMES; i++)
#endif
		config.tnr_frames[i] = frames[i];

	ia_css_configure_tnr(binary, &config);
}

void
ia_css_init_tnr_state(
	struct sh_css_isp_tnr_dmem_state *state,
	size_t size)
{
	(void)size;

#ifndef ISP2401
	assert(NUM_VIDEO_TNR_FRAMES >= 2);
#endif
	assert(sizeof(*state) == size);
	state->tnr_in_buf_idx = 0;
	state->tnr_out_buf_idx = 1;
}
