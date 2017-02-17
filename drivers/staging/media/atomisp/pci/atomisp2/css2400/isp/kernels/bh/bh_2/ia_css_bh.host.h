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

#ifndef __IA_CSS_BH_HOST_H
#define __IA_CSS_BH_HOST_H

#include "ia_css_bh_param.h"
#include "s3a/s3a_1.0/ia_css_s3a_types.h"

void
ia_css_bh_hmem_decode(
	struct ia_css_3a_rgby_output *out_ptr,
	const struct ia_css_bh_table *hmem_buf);

void
ia_css_bh_encode(
	struct sh_css_isp_bh_params *to,
	const struct ia_css_3a_config *from,
	unsigned size);

void
ia_css_bh_hmem_encode(
	struct sh_css_isp_bh_hmem_params *to,
	const struct ia_css_3a_config *from,
	unsigned size);

#endif /* __IA_CSS_BH_HOST_H */
