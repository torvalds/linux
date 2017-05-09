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

#ifndef __IA_CSS_REF_PARAM_H
#define __IA_CSS_REF_PARAM_H

#include <type_support.h>
#include "sh_css_defs.h"
#include "dma.h"

/** Reference frame */
struct ia_css_ref_configuration {
	const struct ia_css_frame *ref_frames[MAX_NUM_VIDEO_DELAY_FRAMES];
	uint32_t dvs_frame_delay;
};

struct sh_css_isp_ref_isp_config {
	uint32_t width_a_over_b;
	struct dma_port_config port_b;
	hrt_vaddress ref_frame_addr_y[MAX_NUM_VIDEO_DELAY_FRAMES];
	hrt_vaddress ref_frame_addr_c[MAX_NUM_VIDEO_DELAY_FRAMES];
	uint32_t dvs_frame_delay;
};

#endif /* __IA_CSS_REF_PARAM_H */
