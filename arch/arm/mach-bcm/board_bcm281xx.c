/*
 * Copyright (C) 2012-2014 Broadcom Corporation
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

#include <linux/clocksource.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "kona.h"

#define SECWDOG_OFFSET			0x00000000
#define SECWDOG_RESERVED_MASK		0xe2000000
#define SECWDOG_WD_LOAD_FLAG_MASK	0x10000000
#define SECWDOG_EN_MASK			0x08000000
#define SECWDOG_SRSTEN_MASK		0x04000000
#define SECWDOG_CLKS_SHIFT		20
#define SECWDOG_COUNT_SHIFT		0

static void bcm281xx_restart(enum reboot_mode mode, const char *cmd)
{
	uint32_t val;
	void __iomem *base;
	struct device_node *np_wdog;

	np_wdog = of_find_compatible_node(NULL, NULL, "brcm,kona-wdt");
	if (!np_wdog) {
		pr_emerg("Couldn't find brcm,kona-wdt\n");
		return;
	}
	base = of_iomap(np_wdog, 0);
	if (!base) {
		pr_emerg("Couldn't map brcm,kona-wdt\n");
		return;
	}

	/* Enable watchdog with short timeout (244us). */
	val = readl(base + SECWDOG_OFFSET);
	val &= SECWDOG_RESERVED_MASK | SECWDOG_WD_LOAD_FLAG_MASK;
	val |= SECWDOG_EN_MASK | SECWDOG_SRSTEN_MASK |
		(0x15 << SECWDOG_CLKS_SHIFT) |
		(0x8 << SECWDOG_COUNT_SHIFT);
	writel(val, base + SECWDOG_OFFSET);

	/* Wait for reset */
	while (1);
}

static void __init bcm281xx_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL,
		&platform_bus);
	kona_l2_cache_init();
}

static const char * const bcm281xx_dt_compat[] = {
	"brcm,bcm11351",	/* Have to use the first number upstreamed */
	NULL,
};

DT_MACHINE_START(BCM281XX_DT, "BCM281xx Broadcom Application Processor")
	.init_machine = bcm281xx_init,
	.restart = bcm281xx_restart,
	.dt_compat = bcm281xx_dt_compat,
MACHINE_END
