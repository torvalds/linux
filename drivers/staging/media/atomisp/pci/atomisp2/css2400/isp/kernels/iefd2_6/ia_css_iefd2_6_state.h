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

#ifndef _IA_CSS_IEFD2_6_STATE_H
#define _IA_CSS_IEFD2_6_STATE_H

#include "type_support.h"
#include "vmem.h" /* for VMEM_ARRAY*/
#include "iefd2_6_vssnlm.isp.h"
#include "iefd2_6.isp.h"

struct iefd2_6_vmem_state {
	/* State buffers required for main IEFD2_6 */
	VMEM_ARRAY(iefd2_6_input_lines[IEFD2_6_STATE_INPUT_BUFFER_HEIGHT], IEFD2_6_STATE_INPUT_BUFFER_WIDTH*ISP_NWAY);
	/* State buffers required for VSSNLM sub-kernel */
	VMEM_ARRAY(vssnlm_input_y[VSSNLM_STATE_INPUT_BUFFER_HEIGHT], VSSNLM_STATE_INPUT_BUFFER_WIDTH*ISP_NWAY);
	VMEM_ARRAY(vssnlm_input_diff_grad[1], VSSNLM_STATE_INPUT_BUFFER_WIDTH*ISP_NWAY);
};

#endif /* _IA_CSS_IEFD2_6_STATE_H */

