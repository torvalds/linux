/*
 *  linux/arch/arm/mach-mmp/mmp-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-tauros2.h>

#include "common.h"

extern void __init mmp_dt_init_timer(void);

static const char *pxa168_dt_board_compat[] __initdata = {
	"mrvl,pxa168-aspenite",
	NULL,
};

static const char *pxa910_dt_board_compat[] __initdata = {
	"mrvl,pxa910-dkb",
	NULL,
};

static void __init mmp_init_time(void)
{
#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	mmp_dt_init_timer();
	of_clk_init(NULL);
}

DT_MACHINE_START(PXA168_DT, "Marvell PXA168 (Device Tree Support)")
	.map_io		= mmp_map_io,
	.init_time	= mmp_init_time,
	.dt_compat	= pxa168_dt_board_compat,
MACHINE_END

DT_MACHINE_START(PXA910_DT, "Marvell PXA910 (Device Tree Support)")
	.map_io		= mmp_map_io,
	.init_time	= mmp_init_time,
	.dt_compat	= pxa910_dt_board_compat,
MACHINE_END
