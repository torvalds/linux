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
#include <ia_css_types.h>
#include <sh_css_defs.h>
#include <ia_css_debug.h>

#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"

#include "ia_css_fpn.host.h"

void
ia_css_fpn_encode(
    struct sh_css_isp_fpn_params *to,
    const struct ia_css_fpn_table *from,
    unsigned int size)
{
	(void)size;
	to->shift = from->shift;
	to->enabled = from->data != NULL;
}

void
ia_css_fpn_dump(
    const struct sh_css_isp_fpn_params *fpn,
    unsigned int level)
{
	if (!fpn) return;
	ia_css_debug_dtrace(level, "Fixed Pattern Noise Reduction:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "fpn_shift", fpn->shift);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "fpn_enabled", fpn->enabled);
}

void
ia_css_fpn_config(
    struct sh_css_isp_fpn_isp_config *to,
    const struct ia_css_fpn_configuration *from,
    unsigned int size)
{
	unsigned int elems_a = ISP_VEC_NELEMS;

	(void)size;
	ia_css_dma_configure_from_info(&to->port_b, from->info);
	to->width_a_over_b = elems_a / to->port_b.elems;

	/* Assume divisiblity here, may need to generalize to fixed point. */
	assert(elems_a % to->port_b.elems == 0);
}

void
ia_css_fpn_configure(
    const struct ia_css_binary     *binary,
    const struct ia_css_frame_info *info)
{
	struct ia_css_frame_info my_info = IA_CSS_BINARY_DEFAULT_FRAME_INFO;
	const struct ia_css_fpn_configuration config = {
		&my_info
	};

	my_info.res.width       = CEIL_DIV(info->res.width, 2);		/* Packed by 2x */
	my_info.res.height      = info->res.height;
	my_info.padded_width    = CEIL_DIV(info->padded_width, 2);	/* Packed by 2x */
	my_info.format          = info->format;
	my_info.raw_bit_depth   = FPN_BITS_PER_PIXEL;
	my_info.raw_bayer_order = info->raw_bayer_order;
	my_info.crop_info       = info->crop_info;

	ia_css_configure_fpn(binary, &config);
}
