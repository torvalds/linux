// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/mmp2-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 */

#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-tauros2.h>

#include "common.h"

static void __init mmp_init_time(void)
{
#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	of_clk_init(NULL);
	timer_probe();
}

static const char *const mmp2_dt_board_compat[] __initconst = {
	"mrvl,mmp2",
	NULL,
};

DT_MACHINE_START(MMP2_DT, "Marvell MMP2 (Device Tree Support)")
	.map_io		= mmp2_map_io,
	.init_time	= mmp_init_time,
	.dt_compat	= mmp2_dt_board_compat,
MACHINE_END
