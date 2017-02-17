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
#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h"
#endif

#include "ia_css_aa2.host.h"

/* YUV Anti-Aliasing configuration. */
const struct ia_css_aa_config default_aa_config = {
	8191 /* default should be 0 */
};

/* Bayer Anti-Aliasing configuration. */
const struct ia_css_aa_config default_baa_config = {
	8191 /* default should be 0 */
};

void
ia_css_aa_encode(
	struct sh_css_isp_aa_params *to,
	const struct ia_css_aa_config *from,
	unsigned size)
{
	(void)size;
	to->strength = from->strength;
}

void
ia_css_init_aa_state(
	void *state,
	size_t size)
{
	memset(state, 0, size);
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_aa_dump(
	const struct sh_css_isp_aa_params *aa,
	unsigned level);

void
ia_css_aa_debug_dtrace(
	const struct ia_css_aa_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.strength=%d\n",
		config->strength);
}
#endif /* IA_CSS_NO_DEBUG */
