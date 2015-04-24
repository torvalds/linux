/*
 * Copyright (C) 2010 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk/bcm2835.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

static void __init bcm2835_init(void)
{
	int ret;

	bcm2835_init_clocks();

	ret = of_platform_populate(NULL, of_default_bus_match_table, NULL,
				   NULL);
	if (ret) {
		pr_err("of_platform_populate failed: %d\n", ret);
		BUG();
	}
}

static const char * const bcm2835_compat[] = {
	"brcm,bcm2835",
	NULL
};

DT_MACHINE_START(BCM2835, "BCM2835")
	.init_machine = bcm2835_init,
	.dt_compat = bcm2835_compat
MACHINE_END
