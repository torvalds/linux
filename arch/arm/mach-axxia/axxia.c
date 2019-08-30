// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for the LSI Axxia SoC devices based on ARM cores.
 *
 * Copyright (C) 2012 LSI
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
