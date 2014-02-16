/*
 * Copyright (C) 2013 Broadcom Corporation
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

#include <linux/of_address.h>
#include <asm/io.h>

#include "kona.h"

static void __iomem *watchdog_base;

void bcm_kona_setup_restart(void)
{
	struct device_node *np_wdog;

	/*
	 * The assumption is that whoever calls bcm_kona_setup_restart()
	 * also needs a Kona Watchdog Timer entry in Device Tree, i.e. we
	 * report an error if the DT entry is missing.
	 */
	np_wdog = of_find_compatible_node(NULL, NULL, "brcm,kona-wdt");
	if (!np_wdog) {
		pr_err("brcm,kona-wdt not found in DT, reboot disabled\n");
		return;
	}
	watchdog_base = of_iomap(np_wdog, 0);
	WARN(!watchdog_base, "failed to map watchdog base");
	of_node_put(np_wdog);
}

#define SECWDOG_OFFSET			0x00000000
#define SECWDOG_RESERVED_MASK		0xE2000000
#define SECWDOG_WD_LOAD_FLAG_MASK	0x10000000
#define SECWDOG_EN_MASK			0x08000000
#define SECWDOG_SRSTEN_MASK		0x04000000
#define SECWDOG_CLKS_SHIFT		20
#define SECWDOG_LOCK_SHIFT		0

void bcm_kona_restart(enum reboot_mode mode, const char *cmd)
{
	uint32_t val;

	if (!watchdog_base)
		panic("Watchdog not mapped. Reboot failed.\n");

	/* Enable watchdog2 with very short timeout. */
	val = readl(watchdog_base + SECWDOG_OFFSET);
	val &= SECWDOG_RESERVED_MASK | SECWDOG_WD_LOAD_FLAG_MASK;
	val |= SECWDOG_EN_MASK | SECWDOG_SRSTEN_MASK |
		(0x8 << SECWDOG_CLKS_SHIFT) |
		(0x8 << SECWDOG_LOCK_SHIFT);
	writel(val, watchdog_base + SECWDOG_OFFSET);

	while (1)
		;
}
