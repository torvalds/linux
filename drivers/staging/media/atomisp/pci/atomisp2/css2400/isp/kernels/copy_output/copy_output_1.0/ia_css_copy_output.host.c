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
	unsigned size)
{
	(void)size;
	to->enable = from->enable;
}

void
ia_css_copy_output_configure(
	const struct ia_css_binary     *binary,
	bool enable)
{
	struct ia_css_copy_output_configuration config = default_config;

	config.enable = enable;

	ia_css_configure_copy_output(binary, &config);
}

