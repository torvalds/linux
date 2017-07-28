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

#include "ia_css_de2.host.h"

const struct ia_css_ecd_config default_ecd_config = {
	(1 << (ISP_VEC_ELEMBITS - 1)) * 2 / 3,	/* 2/3 */
	(1 << (ISP_VEC_ELEMBITS - 1)) - 1,	/* 1.0 */
	0,					/* 0.0 */
};

void
ia_css_ecd_encode(
	struct sh_css_isp_ecd_params *to,
	const struct ia_css_ecd_config *from,
	unsigned size)
{
	(void)size;
	to->zip_strength = from->zip_strength;
	to->fc_strength  = from->fc_strength;
	to->fc_debias    = from->fc_debias;
}

void
ia_css_ecd_dump(
	const struct sh_css_isp_ecd_params *ecd,
	unsigned level);

void
ia_css_ecd_debug_dtrace(
	const struct ia_css_ecd_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.zip_strength=%d, "
		"config.fc_strength=%d, config.fc_debias=%d\n",
		config->zip_strength,
		config->fc_strength, config->fc_debias);
}
