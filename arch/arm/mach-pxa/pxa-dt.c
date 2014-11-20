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
#include <mach/pxa3xx.h>

#include "generic.h"

#ifdef CONFIG_PXA3xx
extern void __init pxa3xx_dt_init_irq(void);

static const struct of_dev_auxdata pxa3xx_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,pxa-uart",		0x40100000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-uart",		0x40200000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-uart",		0x40700000, "pxa2xx-uart.2", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-uart",		0x41600000, "pxa2xx-uart.3", NULL),
	OF_DEV_AUXDATA("marvell,pxa-mmc",	0x41100000, "pxa2xx-mci.0", NULL),
	OF_DEV_AUXDATA("intel,pxa3xx-gpio",	0x40e00000, "pxa3xx-gpio", NULL),
	OF_DEV_AUXDATA("marvell,pxa-ohci",	0x4c000000, "pxa27x-ohci", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-i2c",		0x40301680, "pxa2xx-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,pwri2c",		0x40f500c0, "pxa3xx-i2c.1", NULL),
	OF_DEV_AUXDATA("marvell,pxa3xx-nand",	0x43100000, "pxa3xx-nand", NULL),
	{}
};

static void __init pxa3xx_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     pxa3xx_auxdata_lookup, NULL);
}

static const char *pxa3xx_dt_board_compat[] __initdata = {
	"marvell,pxa300",
	"marvell,pxa310",
	"marvell,pxa320",
	NULL,
};
#endif

#ifdef CONFIG_PXA3xx
DT_MACHINE_START(PXA_DT, "Marvell PXA3xx (Device Tree Support)")
	.map_io		= pxa3xx_map_io,
	.init_irq	= pxa3xx_dt_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
	.init_machine	= pxa3xx_dt_init,
	.dt_compat	= pxa3xx_dt_board_compat,
MACHINE_END
#endif
