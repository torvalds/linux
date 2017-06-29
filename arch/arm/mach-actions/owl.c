/*
 * Copyright (C) 2017 Andreas Färber
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>

static const char * const owl_dt_compat[] = {
	"actions,s500",
	NULL
};

DT_MACHINE_START(OWL, "Actions Semi Owl platform")
	.dt_compat	= owl_dt_compat,
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
MACHINE_END
