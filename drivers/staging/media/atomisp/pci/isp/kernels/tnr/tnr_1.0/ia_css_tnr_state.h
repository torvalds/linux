/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_TNR_STATE_H
#define __IA_CSS_TNR_STATE_H

#include "type_support.h"

/* TNR (temporal noise reduction) */
struct sh_css_isp_tnr_dmem_state {
	u32 tnr_in_buf_idx;
	u32 tnr_out_buf_idx;
};

#endif /* __IA_CSS_TNR_STATE_H */
