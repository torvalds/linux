// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform support for LPC32xx SoC
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2012 Roland Stigge <stigge@antcom.de>
 * Copyright (C) 2010 NXP Semiconductors
 */

#include <asm/mach/arch.h>
#include "common.h"

static void __init lpc3250_machine_init(void)
{
	lpc32xx_check_uid();
	lpc32xx_pm_init();
	lpc32xx_serial_init();
}

static const char *const lpc32xx_dt_compat[] __initconst = {
	"nxp,lpc3220",
	"nxp,lpc3230",
	"nxp,lpc3240",
	"nxp,lpc3250",
	NULL
};

DT_MACHINE_START(LPC32XX_DT, "LPC32XX SoC (Flattened Device Tree)")
	.atag_offset	= 0x100,
	.map_io		= lpc32xx_map_io,
	.init_machine	= lpc3250_machine_init,
	.dt_compat	= lpc32xx_dt_compat,
MACHINE_END
