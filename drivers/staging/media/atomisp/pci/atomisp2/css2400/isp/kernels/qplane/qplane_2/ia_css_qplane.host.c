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

#include "ia_css_frame.h"
#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "assert_support.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"

#include "ia_css_qplane.host.h"

static const struct ia_css_qplane_configuration default_config = {
	.pipe = (struct sh_css_sp_pipeline *)NULL,
};

void
ia_css_qplane_config(
	struct sh_css_isp_qplane_isp_config *to,
	const struct ia_css_qplane_configuration  *from,
	unsigned size)
{
	unsigned elems_a = ISP_VEC_NELEMS;

	(void)size;
	ia_css_dma_configure_from_info(&to->port_b, from->info);
	to->width_a_over_b = elems_a / to->port_b.elems;

	/* Assume divisiblity here, may need to generalize to fixed point. */
	assert (elems_a % to->port_b.elems == 0);

	to->inout_port_config = from->pipe->inout_port_config;
	to->format = from->info->format;
}

void
ia_css_qplane_configure(
	const struct sh_css_sp_pipeline *pipe,
	const struct ia_css_binary      *binary,
	const struct ia_css_frame_info  *info)
{
	struct ia_css_qplane_configuration config = default_config;

	config.pipe = pipe;
	config.info = info;

	ia_css_configure_qplane(binary, &config);
}
