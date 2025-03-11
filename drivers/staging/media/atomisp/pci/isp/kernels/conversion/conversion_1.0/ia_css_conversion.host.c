// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
    unsigned int size)
{
	(void)size;
	to->en     = from->en;
	to->dummy0 = from->dummy0;
	to->dummy1 = from->dummy1;
	to->dummy2 = from->dummy2;
}
