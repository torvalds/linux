/*
 * Support for the LSI Axxia SoC devices based on ARM cores.
 *
 * Copyright (C) 2012 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <asm/mach/arch.h>

static const char *const axxia_dt_match[] __initconst = {
	"lsi,axm5516",
	"lsi,axm5516-sim",
	"lsi,axm5516-emu",
	NULL
};

DT_MACHINE_START(AXXIA_DT, "LSI Axxia AXM55XX")
	.dt_compat = axxia_dt_match,
MACHINE_END
