/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DPC2_PARAM_H
#define __IA_CSS_DPC2_PARAM_H

#include <linux/math.h>

#include "type_support.h"
#include "vmem.h" /* for VMEM_ARRAY*/

/* 4 planes : GR, R, B, GB */
#define NUM_PLANES		4

/* ToDo: Move this to testsetup */
#define MAX_FRAME_SIMDWIDTH	30

/* 3 lines state per color plane input_line_state */
#define DPC2_STATE_INPUT_BUFFER_HEIGHT	(3 * NUM_PLANES)
/* Each plane has width equal to half frame line */
#define DPC2_STATE_INPUT_BUFFER_WIDTH	DIV_ROUND_UP(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line state per color plane for local deviation state*/
#define DPC2_STATE_LOCAL_DEVIATION_BUFFER_HEIGHT	(1 * NUM_PLANES)
/* Each plane has width equal to half frame line */
#define DPC2_STATE_LOCAL_DEVIATION_BUFFER_WIDTH		DIV_ROUND_UP(MAX_FRAME_SIMDWIDTH, 2)

/* MINMAX state buffer stores 1 full input line (GR-R color line) */
#define DPC2_STATE_SECOND_MINMAX_BUFFER_HEIGHT	1
#define DPC2_STATE_SECOND_MINMAX_BUFFER_WIDTH	MAX_FRAME_SIMDWIDTH

struct ia_css_isp_dpc2_params {
	s32 metric1;
	s32 metric2;
	s32 metric3;
	s32 wb_gain_gr;
	s32 wb_gain_r;
	s32 wb_gain_b;
	s32 wb_gain_gb;
};

#endif /* __IA_CSS_DPC2_PARAM_H */
