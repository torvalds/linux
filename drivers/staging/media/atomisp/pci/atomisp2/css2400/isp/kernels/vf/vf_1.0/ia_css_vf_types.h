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

#ifndef __IA_CSS_VF_TYPES_H
#define __IA_CSS_VF_TYPES_H

/* Viewfinder decimation
 *
 *  ISP block: vfeven_horizontal_downscale
 */

#include <ia_css_frame_public.h>
#include <type_support.h>

struct ia_css_vf_configuration {
	uint32_t vf_downscale_bits; /** Log VF downscale value */
	const struct ia_css_frame_info *info;
};

#endif /* __IA_CSS_VF_TYPES_H */

