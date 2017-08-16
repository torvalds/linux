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

#ifndef __IA_CSS_YNR_STATE_H
#define __IA_CSS_YNR_STATE_H

#include "type_support.h"
#include "vmem.h"

/* YNR (luminance noise reduction) */
struct sh_css_isp_ynr_vmem_state {
	VMEM_ARRAY(ynr_buf[4], MAX_VECTORS_PER_BUF_LINE*ISP_NWAY);
};

#endif /* __IA_CSS_YNR_STATE_H */
