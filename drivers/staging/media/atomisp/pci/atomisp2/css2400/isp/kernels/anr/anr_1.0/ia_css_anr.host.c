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
#include "sh_css_defs.h"
#include "ia_css_debug.h"

#include "ia_css_anr.host.h"

const struct ia_css_anr_config default_anr_config = {
	10,
	{ 0, 3, 1, 2, 3, 6, 4, 5, 1, 4, 2, 3, 2, 5, 3, 4,
	  0, 3, 1, 2, 3, 6, 4, 5, 1, 4, 2, 3, 2, 5, 3, 4,
	  0, 3, 1, 2, 3, 6, 4, 5, 1, 4, 2, 3, 2, 5, 3, 4,
	  0, 3, 1, 2, 3, 6, 4, 5, 1, 4, 2, 3, 2, 5, 3, 4},
	{10, 20, 30}
};

void
ia_css_anr_encode(
	struct sh_css_isp_anr_params *to,
	const struct ia_css_anr_config *from,
	unsigned size)
{
	(void)size;
	to->threshold = from->threshold;
}

void
ia_css_anr_dump(
	const struct sh_css_isp_anr_params *anr,
	unsigned level)
{
	if (!anr) return;
	ia_css_debug_dtrace(level, "Advance Noise Reduction:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			"anr_threshold", anr->threshold);
}

void
ia_css_anr_debug_dtrace(
	const struct ia_css_anr_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.threshold=%d\n",
		config->threshold);
}

