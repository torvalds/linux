// SPDX-License-Identifier: GPL-2.0
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

#include <assert_support.h>
#include <ia_css_frame_public.h>
#include <ia_css_frame.h>
#include <ia_css_binary.h>
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"
#include "ia_css_crop.host.h"

static const struct ia_css_crop_configuration default_config = {
	.info = (struct ia_css_frame_info *)NULL,
};

void
ia_css_crop_encode(
    struct sh_css_isp_crop_isp_params *to,
    const struct ia_css_crop_config *from,
    unsigned int size)
{
	(void)size;
	to->crop_pos = from->crop_pos;
}

int ia_css_crop_config(struct sh_css_isp_crop_isp_config *to,
		       const struct ia_css_crop_configuration *from,
		       unsigned int size)
{
	unsigned int elems_a = ISP_VEC_NELEMS;
	int ret;

	ret = ia_css_dma_configure_from_info(&to->port_b, from->info);
	if (ret)
		return ret;

	to->width_a_over_b = elems_a / to->port_b.elems;

	/* Assume divisiblity here, may need to generalize to fixed point. */
	if (elems_a % to->port_b.elems != 0)
		return -EINVAL;

	return 0;
}

int ia_css_crop_configure(const struct ia_css_binary     *binary,
			  const struct ia_css_frame_info *info)
{
	struct ia_css_crop_configuration config = default_config;

	config.info = info;

	return ia_css_configure_crop(binary, &config);
}
