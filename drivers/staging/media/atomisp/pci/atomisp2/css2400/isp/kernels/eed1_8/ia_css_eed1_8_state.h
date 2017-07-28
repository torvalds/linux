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

#ifndef __IA_CSS_EED1_8_STATE_H
#define __IA_CSS_EED1_8_STATE_H

#include "type_support.h"
#include "vmem.h" /* for VMEM_ARRAY*/

#include "ia_css_eed1_8_param.h"

struct eed1_8_vmem_state {
	VMEM_ARRAY(eed1_8_input_lines[EED1_8_STATE_INPUT_BUFFER_HEIGHT], EED1_8_STATE_INPUT_BUFFER_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_LD_H[EED1_8_STATE_LD_H_HEIGHT], EED1_8_STATE_LD_H_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_LD_V[EED1_8_STATE_LD_V_HEIGHT], EED1_8_STATE_LD_V_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_D_Hr[EED1_8_STATE_D_HR_HEIGHT], EED1_8_STATE_D_HR_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_D_Hb[EED1_8_STATE_D_HB_HEIGHT], EED1_8_STATE_D_HB_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_D_Vr[EED1_8_STATE_D_VR_HEIGHT], EED1_8_STATE_D_VR_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_D_Vb[EED1_8_STATE_D_VB_HEIGHT], EED1_8_STATE_D_VB_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_rb_zipped[EED1_8_STATE_RB_ZIPPED_HEIGHT], EED1_8_STATE_RB_ZIPPED_WIDTH*ISP_NWAY);
#if EED1_8_FC_ENABLE_MEDIAN
	VMEM_ARRAY(eed1_8_Yc[EED1_8_STATE_YC_HEIGHT], EED1_8_STATE_YC_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_Cg[EED1_8_STATE_CG_HEIGHT], EED1_8_STATE_CG_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_Co[EED1_8_STATE_CO_HEIGHT], EED1_8_STATE_CO_WIDTH*ISP_NWAY);
	VMEM_ARRAY(eed1_8_AbsK[EED1_8_STATE_ABSK_HEIGHT], EED1_8_STATE_ABSK_WIDTH*ISP_NWAY);
#endif
};

#endif /* __IA_CSS_EED1_8_STATE_H */
