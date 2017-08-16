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

#if !defined(HAS_NO_HMEM)

#include "memory_access.h"
#include "ia_css_types.h"
#include "sh_css_internal.h"
#include "sh_css_frac.h"

#include "ia_css_raa.host.h"

void
ia_css_raa_encode(
	struct sh_css_isp_aa_params *to,
	const struct ia_css_aa_config *from,
	unsigned size)
{
	(void)size;
	(void)to;
	(void)from;
}

#endif
