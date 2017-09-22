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

#ifndef __IA_CSS_CNR2_STATE_H
#define __IA_CSS_CNR2_STATE_H

#include "type_support.h"
#include "vmem.h"

typedef struct
{
  VMEM_ARRAY(y, (MAX_VECTORS_PER_BUF_LINE/2)*ISP_NWAY);
  VMEM_ARRAY(u, (MAX_VECTORS_PER_BUF_LINE/2)*ISP_NWAY);
  VMEM_ARRAY(v, (MAX_VECTORS_PER_BUF_LINE/2)*ISP_NWAY);
} s_cnr_buf;

/* CNR (color noise reduction) */
struct sh_css_isp_cnr_vmem_state {
	s_cnr_buf cnr_buf;
};

#endif /* __IA_CSS_CNR2_STATE_H */
