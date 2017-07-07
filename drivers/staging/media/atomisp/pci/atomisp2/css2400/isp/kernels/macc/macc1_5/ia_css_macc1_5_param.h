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

#ifndef __IA_CSS_MACC1_5_PARAM_H
#define __IA_CSS_MACC1_5_PARAM_H

#include "type_support.h"
#include "vmem.h"
#include "ia_css_macc1_5_types.h"

/* MACC */
struct sh_css_isp_macc1_5_params {
	int32_t exp;
};

struct sh_css_isp_macc1_5_vmem_params {
	VMEM_ARRAY(data, IA_CSS_MACC_NUM_COEFS*ISP_NWAY);
};

#endif /* __IA_CSS_MACC1_5_PARAM_H */
