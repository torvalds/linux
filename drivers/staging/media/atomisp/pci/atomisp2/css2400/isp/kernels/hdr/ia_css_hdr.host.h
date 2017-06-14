/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
/* Release Version: irci_ecr-master_20150911_0724 */
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

#ifndef __IA_CSS_HDR_HOST_H
#define __IA_CSS_HDR_HOST_H

#include "ia_css_hdr_param.h"
#include "ia_css_hdr_types.h"

extern const struct ia_css_hdr_config default_hdr_config;

void
ia_css_hdr_init_config(
	struct sh_css_isp_hdr_params *to,
	const struct ia_css_hdr_config *from,
	unsigned size);

#endif /* __IA_CSS_HDR_HOST_H */
