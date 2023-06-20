// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Modified from mach-omap/omap2/board-generic.c
 */

#include <asm/mach/arch.h>

#include "common.h"
#include "da8xx.h"

#ifdef CONFIG_ARCH_DAVINCI_DA850

static void __init da850_init_machine(void)
{
	davinci_pm_init();
	pdata_quirks_init();
}

static const char *const da850_boards_compat[] __initconst = {
	"enbw,cmc",
	"ti,da850-lcdk",
	"ti,da850-evm",
	"ti,da850",
	NULL,
};

DT_MACHINE_START(DA850_DT, "Generic DA850/OMAP-L138/AM18x")
	.map_io		= da850_init,
	.init_machine	= da850_init_machine,
	.dt_compat	= da850_boards_compat,
	.init_late	= davinci_init_late,
MACHINE_END

#endif
