/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_TNR_PARAM_H
#define __IA_CSS_TNR_PARAM_H

#include "type_support.h"
#include "sh_css_defs.h"
#include "dma.h"

/* TNR (Temporal Noise Reduction) */
struct sh_css_isp_tnr_params {
	s32 coef;
	s32 threshold_Y;
	s32 threshold_C;
};

struct ia_css_tnr_configuration {
	const struct ia_css_frame *tnr_frames[NUM_TNR_FRAMES];
};

struct sh_css_isp_tnr_isp_config {
	u32 width_a_over_b;
	u32 frame_height;
	struct dma_port_config port_b;
	ia_css_ptr tnr_frame_addr[NUM_TNR_FRAMES];
};

#endif /* __IA_CSS_TNR_PARAM_H */
