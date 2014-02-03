/*
 * Broadcom BCM470X / BCM5301X ARM platform code.
 *
 * Copyright 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>


static void __init bcm5301x_dt_init(void)
{
	l2x0_of_init(0, ~0UL);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char __initconst *bcm5301x_dt_compat[] = {
	"brcm,bcm4708",
	NULL,
};

DT_MACHINE_START(BCM5301X, "BCM5301X")
	.init_machine	= bcm5301x_dt_init,
	.dt_compat	= bcm5301x_dt_compat,
MACHINE_END
