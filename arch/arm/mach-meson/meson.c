/*
 * Copyright (C) 2014 Carlo Caione <carlo@caione.org>
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

static const char * const m6_common_board_compat[] = {
	"amlogic,meson6",
	NULL,
};

DT_MACHINE_START(AML8726_MX, "Amlogic Meson6 platform")
	.dt_compat	= m6_common_board_compat,
MACHINE_END

