// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/mmp-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 */

#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/of_clk.h>
#include <linux/clocksource.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-tauros2.h>

#include "common.h"

static const char *const pxa168_dt_board_compat[] __initconst = {
	"mrvl,pxa168-aspenite",
	NULL,
};

static const char *const pxa910_dt_board_compat[] __initconst = {
	"mrvl,pxa910-dkb",
	NULL,
};

static void __init mmp_init_time(void)
{
#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	of_clk_init(NULL);
	timer_probe();
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
