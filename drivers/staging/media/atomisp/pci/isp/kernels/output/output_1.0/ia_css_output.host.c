// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_frame.h"
#include "ia_css_debug.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "ia_css_output.host.h"
#include "isp.h"

#include "assert_support.h"

const struct ia_css_output_config default_output_config = {
	0,
	0
};

static const struct ia_css_output_configuration default_output_configuration = {
	.info = (struct ia_css_frame_info *)NULL,
};

static const struct ia_css_output0_configuration default_output0_configuration
	= {
	.info = (struct ia_css_frame_info *)NULL,
};

static const struct ia_css_output1_configuration default_output1_configuration
	= {
	.info = (struct ia_css_frame_info *)NULL,
};

void
ia_css_output_encode(
    struct sh_css_isp_output_params *to,
    const struct ia_css_output_config *from,
    unsigned int size)
{
	(void)size;
	to->enable_hflip = from->enable_hflip;
	to->enable_vflip = from->enable_vflip;
}

int ia_css_output_config(struct sh_css_isp_output_isp_config *to,
			 const struct ia_css_output_configuration  *from,
			 unsigned int size)
{
	unsigned int elems_a = ISP_VEC_NELEMS;
	int ret;

	ret = ia_css_dma_configure_from_info(&to->port_b, from->info);
	if (ret)
		return ret;

	to->width_a_over_b = elems_a / to->port_b.elems;
	to->height = from->info ? from->info->res.height : 0;
	to->enable = from->info != NULL;
	ia_css_frame_info_to_frame_sp_info(&to->info, from->info);

	/* Assume divisiblity here, may need to generalize to fixed point. */
	if (elems_a % to->port_b.elems != 0)
		return -EINVAL;

	return 0;
}

int ia_css_output0_config(struct sh_css_isp_output_isp_config       *to,
			  const struct ia_css_output0_configuration *from,
			  unsigned int size)
{
	return ia_css_output_config(to, (const struct ia_css_output_configuration *)from, size);
}

int ia_css_output1_config(struct sh_css_isp_output_isp_config       *to,
		          const struct ia_css_output1_configuration *from,
			  unsigned int size)
{
	return ia_css_output_config(to, (const struct ia_css_output_configuration *)from, size);
}

int ia_css_output_configure(const struct ia_css_binary     *binary,
			    const struct ia_css_frame_info *info)
{
	if (info) {
		struct ia_css_output_configuration config =
			    default_output_configuration;

		config.info = info;

		return ia_css_configure_output(binary, &config);
	}
	return 0;
}

int ia_css_output0_configure(const struct ia_css_binary    *binary,
			    const struct ia_css_frame_info *info)
{
	if (info) {
		struct ia_css_output0_configuration config =
			    default_output0_configuration;

		config.info = info;

		return ia_css_configure_output0(binary, &config);
	}
	return 0;
}

int ia_css_output1_configure(const struct ia_css_binary     *binary,
			     const struct ia_css_frame_info *info)
{
	if (info) {
		struct ia_css_output1_configuration config =
			    default_output1_configuration;

		config.info = info;

		return ia_css_configure_output1(binary, &config);
	}
	return 0;
}

void
ia_css_output_dump(
    const struct sh_css_isp_output_params *output,
    unsigned int level)
{
	if (!output) return;
	ia_css_debug_dtrace(level, "Horizontal Output Flip:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "enable", output->enable_hflip);
	ia_css_debug_dtrace(level, "Vertical Output Flip:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "enable", output->enable_vflip);
}

void
ia_css_output_debug_dtrace(
    const struct ia_css_output_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.enable_hflip=%d",
			    config->enable_hflip);
	ia_css_debug_dtrace(level,
			    "config.enable_vflip=%d",
			    config->enable_vflip);
}
