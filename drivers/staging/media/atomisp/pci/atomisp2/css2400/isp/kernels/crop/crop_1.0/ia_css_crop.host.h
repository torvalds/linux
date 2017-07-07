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

#ifndef __IA_CSS_CROP_HOST_H
#define __IA_CSS_CROP_HOST_H

#include <ia_css_frame_public.h>
#include <ia_css_binary.h>

#include "ia_css_crop_types.h"
#include "ia_css_crop_param.h"

void
ia_css_crop_encode(
	struct sh_css_isp_crop_isp_params *to,
	const struct ia_css_crop_config *from,
	unsigned size);

void
ia_css_crop_config(
	struct sh_css_isp_crop_isp_config      *to,
	const struct ia_css_crop_configuration *from,
	unsigned size);

void
ia_css_crop_configure(
	const struct ia_css_binary     *binary,
	const struct ia_css_frame_info *from);

#endif /* __IA_CSS_CROP_HOST_H */
