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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clocksource.h>

#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>

#include "bcm_kona_smc.h"
#include "kona.h"

static int __init kona_l2_cache_init(void)
{
	if (!IS_ENABLED(CONFIG_CACHE_L2X0))
		return 0;

	if (bcm_kona_smc_init() < 0) {
		pr_info("Kona secure API not available. Skipping L2 init\n");
		return 0;
	}

	bcm_kona_smc(SSAPI_ENABLE_L2_CACHE, 0, 0, 0, 0);

	/*
	 * The aux_val and aux_mask have no effect since L2 cache is already
	 * enabled.  Pass 0s for aux_val and 1s for aux_mask for default value.
	 */
	return l2x0_of_init(0, ~0);
}

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

static const char * const bcm11351_dt_compat[] = { "brcm,bcm11351", NULL, };

DT_MACHINE_START(BCM11351_DT, "Broadcom Application Processor")
	.init_machine = board_init,
	.restart = bcm_kona_restart,
	.dt_compat = bcm11351_dt_compat,
MACHINE_END
