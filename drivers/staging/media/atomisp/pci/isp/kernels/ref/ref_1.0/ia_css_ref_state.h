/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_REF_STATE_H
#define __IA_CSS_REF_STATE_H

#include "type_support.h"

/* REF (temporal noise reduction) */
struct sh_css_isp_ref_dmem_state {
	s32 ref_in_buf_idx;
	s32 ref_out_buf_idx;
};

#endif /* __IA_CSS_REF_STATE_H */
