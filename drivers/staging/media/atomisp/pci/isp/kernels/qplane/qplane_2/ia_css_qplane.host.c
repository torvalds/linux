// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

int ia_css_qplane_config(struct sh_css_isp_qplane_isp_config *to,
			 const struct ia_css_qplane_configuration  *from,
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

	to->inout_port_config = from->pipe->inout_port_config;
	to->format = from->info->format;

	return 0;
}

int ia_css_qplane_configure(const struct sh_css_sp_pipeline *pipe,
			    const struct ia_css_binary      *binary,
			    const struct ia_css_frame_info  *info)
{
	struct ia_css_qplane_configuration config = default_config;

	config.pipe = pipe;
	config.info = info;

	return ia_css_configure_qplane(binary, &config);
}
