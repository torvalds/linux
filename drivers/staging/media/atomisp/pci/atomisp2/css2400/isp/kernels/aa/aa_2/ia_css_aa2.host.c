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

