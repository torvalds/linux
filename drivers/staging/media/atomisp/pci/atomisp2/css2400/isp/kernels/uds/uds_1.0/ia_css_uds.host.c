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

#include "ia_css_uds.host.h"

void
ia_css_uds_encode(
	struct sh_css_sp_uds_params *to,
	const struct ia_css_uds_config *from,
	unsigned size)
{
	(void)size;
	to->crop_pos = from->crop_pos;
	to->uds      = from->uds;
}

void
ia_css_uds_dump(
	const struct sh_css_sp_uds_params *uds,
	unsigned level);
