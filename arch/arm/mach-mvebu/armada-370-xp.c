/*
 * Device Tree support for Armada 370 and XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/time-armada-370-xp.h>
#include <linux/dma-mapping.h>
#include <linux/mbus.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include "armada-370-xp.h"
#include "common.h"
#include "coherency.h"

static void __init armada_370_xp_map_io(void)
{
	debug_ll_io_init();
}

static void __init armada_370_xp_timer_and_clk_init(void)
{
	of_clk_init(NULL);
	armada_370_xp_timer_init();
	coherency_init();
	BUG_ON(mvebu_mbus_dt_init());
#ifdef CONFIG_CACHE_L2X0
	l2x0_of_init(0, ~0UL);
#endif
}

static void __init armada_370_xp_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const armada_370_xp_dt_compat[] = {
	"marvell,armada-370-xp",
	NULL,
};

DT_MACHINE_START(ARMADA_XP_DT, "Marvell Armada 370/XP (Device Tree)")
	.smp		= smp_ops(armada_xp_smp_ops),
	.init_machine	= armada_370_xp_dt_init,
	.map_io		= armada_370_xp_map_io,
	.init_time	= armada_370_xp_timer_and_clk_init,
	.restart	= mvebu_restart,
	.dt_compat	= armada_370_xp_dt_compat,
MACHINE_END
