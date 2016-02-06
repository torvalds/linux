/*
 *  linux/arch/arm/mach-pxa/pxa-dt.c
 *
 *  Copyright (C) 2012 Daniel Mack
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/irqs.h>

#include "generic.h"

#ifdef CONFIG_PXA3xx
static const char *const pxa3xx_dt_board_compat[] __initconst = {
	"marvell,pxa300",
	"marvell,pxa310",
	"marvell,pxa320",
	NULL,
};

DT_MACHINE_START(PXA_DT, "Marvell PXA3xx (Device Tree Support)")
	.map_io		= pxa3xx_map_io,
	.init_irq	= pxa3xx_dt_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.restart	= pxa_restart,
	.dt_compat	= pxa3xx_dt_board_compat,
MACHINE_END
#endif

#ifdef CONFIG_PXA27x
static const char * const pxa27x_dt_board_compat[] __initconst = {
	"marvell,pxa270",
	NULL,
};

DT_MACHINE_START(PXA27X_DT, "Marvell PXA2xx (Device Tree Support)")
	.map_io		= pxa27x_map_io,
	.init_irq	= pxa27x_dt_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.restart	= pxa_restart,
	.dt_compat	= pxa27x_dt_board_compat,
MACHINE_END
#endif
