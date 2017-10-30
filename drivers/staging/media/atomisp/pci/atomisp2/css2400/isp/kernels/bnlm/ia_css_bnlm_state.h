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

#ifndef __IA_CSS_BNLM_STATE_H
#define __IA_CSS_BNLM_STATE_H


#include "type_support.h"
#include "vmem.h" /* for VMEM_ARRAY*/
#include "bnlm.isp.h"

struct bnlm_vmem_state {
	/* State buffers required for BNLM */
	VMEM_ARRAY(buf[BNLM_STATE_BUF_HEIGHT], BNLM_STATE_BUF_WIDTH*ISP_NWAY);
};



#endif /* __IA_CSS_BNLM_STATE_H */

