/*
 * Copyright (C) 2012-2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clocksource.h>

#include <asm/mach/arch.h>

#include "kona.h"

static void bcm_board_setup_restart(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "brcm,bcm11351");
	if (np) {
		if (of_device_is_available(np))
			bcm_kona_setup_restart();
		of_node_put(np);
	}
	/* Restart setup for other boards goes here */
}

static void __init board_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL,
		&platform_bus);

	bcm_board_setup_restart();
	kona_l2_cache_init();
}

static const char * const bcm281xx_dt_compat[] = {
	"brcm,bcm11351",	/* Have to use the first number upstreamed */
	NULL,
};

DT_MACHINE_START(BCM281XX_DT, "BCM281xx Broadcom Application Processor")
	.init_machine = board_init,
	.restart = bcm_kona_restart,
	.dt_compat = bcm281xx_dt_compat,
MACHINE_END
