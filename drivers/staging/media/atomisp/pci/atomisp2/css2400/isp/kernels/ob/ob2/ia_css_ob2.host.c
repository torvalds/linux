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
#include "sh_css_frac.h"
#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h"
#endif
#include "isp.h"
#include "ia_css_ob2.host.h"

const struct ia_css_ob2_config default_ob2_config = {
	0,
	0,
	0,
	0
};

void
ia_css_ob2_encode(
	struct sh_css_isp_ob2_params *to,
	const struct ia_css_ob2_config *from,
	unsigned size)
{
	(void)size;

	/* Blacklevels types are u0_16 */
	to->blacklevel_gr = uDIGIT_FITTING(from->level_gr, 16, SH_CSS_BAYER_BITS);
	to->blacklevel_r  = uDIGIT_FITTING(from->level_r,  16, SH_CSS_BAYER_BITS);
	to->blacklevel_b  = uDIGIT_FITTING(from->level_b,  16, SH_CSS_BAYER_BITS);
	to->blacklevel_gb = uDIGIT_FITTING(from->level_gb, 16, SH_CSS_BAYER_BITS);
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_ob2_dump(
	const struct sh_css_isp_ob2_params *ob2,
	unsigned level)
{
	if (!ob2)
		return;

	ia_css_debug_dtrace(level, "Optical Black 2:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
		"ob2_blacklevel_gr", ob2->blacklevel_gr);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
		"ob2_blacklevel_r", ob2->blacklevel_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
		"ob2_blacklevel_b", ob2->blacklevel_b);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
		"ob2_blacklevel_gb", ob2->blacklevel_gb);

}


void
ia_css_ob2_debug_dtrace(
	const struct ia_css_ob2_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.level_gr=%d, config.level_r=%d, "
		"config.level_b=%d,  config.level_gb=%d, ",
		config->level_gr, config->level_r,
		config->level_b, config->level_gb);
}
#endif
