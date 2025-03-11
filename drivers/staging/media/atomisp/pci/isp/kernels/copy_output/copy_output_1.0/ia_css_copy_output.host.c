// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_copy_output.host.h"
#include "ia_css_binary.h"
#include "type_support.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"

static const struct ia_css_copy_output_configuration default_config = {
	.enable = false,
};

void
ia_css_copy_output_config(
    struct sh_css_isp_copy_output_isp_config      *to,
    const struct ia_css_copy_output_configuration *from,
    unsigned int size)
{
	(void)size;
	to->enable = from->enable;
}

int ia_css_copy_output_configure(const struct ia_css_binary *binary,
				 bool enable)
{
	struct ia_css_copy_output_configuration config = default_config;

	config.enable = enable;

	return ia_css_configure_copy_output(binary, &config);
}
