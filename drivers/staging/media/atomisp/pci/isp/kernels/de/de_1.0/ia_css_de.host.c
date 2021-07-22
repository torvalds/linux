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

#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "sh_css_frac.h"
#include "ia_css_de.host.h"

const struct ia_css_de_config default_de_config = {
	0,
	0,
	0
};

void
ia_css_de_encode(
    struct sh_css_isp_de_params *to,
    const struct ia_css_de_config *from,
    unsigned int size)
{
	(void)size;
	to->pixelnoise =
	    uDIGIT_FITTING(from->pixelnoise, 16, SH_CSS_BAYER_BITS);
	to->c1_coring_threshold =
	    uDIGIT_FITTING(from->c1_coring_threshold, 16,
			   SH_CSS_BAYER_BITS);
	to->c2_coring_threshold =
	    uDIGIT_FITTING(from->c2_coring_threshold, 16,
			   SH_CSS_BAYER_BITS);
}

void
ia_css_de_dump(
    const struct sh_css_isp_de_params *de,
    unsigned int level)
{
	if (!de) return;
	ia_css_debug_dtrace(level, "Demosaic:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "de_pixelnoise", de->pixelnoise);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "de_c1_coring_threshold",
			    de->c1_coring_threshold);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "de_c2_coring_threshold",
			    de->c2_coring_threshold);
}

void
ia_css_de_debug_dtrace(
    const struct ia_css_de_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.pixelnoise=%d, config.c1_coring_threshold=%d, config.c2_coring_threshold=%d\n",
			    config->pixelnoise,
			    config->c1_coring_threshold, config->c2_coring_threshold);
}

void
ia_css_init_de_state(
    void/*struct sh_css_isp_de_vmem_state*/ * state,
    size_t size)
{
	memset(state, 0, size);
}
