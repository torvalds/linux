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

#include "ia_css_types.h"
#include "ia_css_conversion.host.h"

const struct ia_css_conversion_config default_conversion_config = {
	0,
	0,
	0,
	0,
};

void
ia_css_conversion_encode(
	struct sh_css_isp_conversion_params *to,
	const struct ia_css_conversion_config *from,
	unsigned size)
{
	(void)size;
	to->en     = from->en;
	to->dummy0 = from->dummy0;
	to->dummy1 = from->dummy1;
	to->dummy2 = from->dummy2;
}
