/*
 *  linux/arch/arm/mach-mmp/mmp2-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"

extern void __init mmp_dt_init_timer(void);

static const struct of_dev_auxdata mmp2_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4030000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4017000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4018000, "pxa2xx-uart.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4016000, "pxa2xx-uart.3", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4011000, "pxa2xx-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4025000, "pxa2xx-i2c.1", NULL),
	OF_DEV_AUXDATA("marvell,mmp-gpio", 0xd4019000, "mmp2-gpio", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-rtc", 0xd4010000, "sa1100-rtc", NULL),
	{}
};

static void __init mmp2_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     mmp2_auxdata_lookup, NULL);
}

static const char *mmp2_dt_board_compat[] __initdata = {
	"mrvl,mmp2-brownstone",
	NULL,
};

DT_MACHINE_START(MMP2_DT, "Marvell MMP2 (Device Tree Support)")
	.map_io		= mmp_map_io,
	.init_time	= mmp_dt_init_timer,
	.init_machine	= mmp2_dt_init,
	.dt_compat	= mmp2_dt_board_compat,
MACHINE_END
