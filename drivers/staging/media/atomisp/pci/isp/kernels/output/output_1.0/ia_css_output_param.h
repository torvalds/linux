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

#ifndef __IA_CSS_OUTPUT_PARAM_H
#define __IA_CSS_OUTPUT_PARAM_H

#include <type_support.h>
#include "dma.h"
#include "ia_css_frame_comm.h" /* ia_css_frame_sp_info */

/* output frame */
struct sh_css_isp_output_isp_config {
	u32 width_a_over_b;
	u32 height;
	u32 enable;
	struct ia_css_frame_sp_info info;
	struct dma_port_config port_b;
};

struct sh_css_isp_output_params {
	u8 enable_hflip;
	u8 enable_vflip;
};

#endif /* __IA_CSS_OUTPUT_PARAM_H */
