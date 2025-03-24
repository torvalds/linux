/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
	const struct ia_css_frame *tnr_frames[NUM_VIDEO_TNR_FRAMES];
};

struct sh_css_isp_tnr_isp_config {
	u32 width_a_over_b;
	u32 frame_height;
	struct dma_port_config port_b;
	ia_css_ptr tnr_frame_addr[NUM_VIDEO_TNR_FRAMES];
};

#endif /* __IA_CSS_TNR_PARAM_H */
