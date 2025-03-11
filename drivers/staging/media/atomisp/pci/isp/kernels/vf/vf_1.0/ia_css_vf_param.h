/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_VF_PARAM_H
#define __IA_CSS_VF_PARAM_H

#include "type_support.h"
#include "dma.h"
#include "gc/gc_1.0/ia_css_gc_param.h" /* GAMMA_OUTPUT_BITS */
#include "ia_css_frame_comm.h" /* ia_css_frame_sp_info */
#include "ia_css_vf_types.h"

#define VFDEC_BITS_PER_PIXEL	GAMMA_OUTPUT_BITS

/* Viewfinder decimation */
struct sh_css_isp_vf_isp_config {
	u32 vf_downscale_bits; /** Log VF downscale value */
	u32 enable;
	struct ia_css_frame_sp_info info;
	struct {
		u32 width_a_over_b;
		struct dma_port_config port_b;
	} dma;
};

#endif /* __IA_CSS_VF_PARAM_H */
