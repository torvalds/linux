/*
 * Copyright (C) 2014 Broadcom Corporation
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

#include "bcm_kona_smc.h"
#include "kona.h"

#define RSTMGR_DT_STRING		"brcm,bcm21664-resetmgr"

#define RSTMGR_REG_WR_ACCESS_OFFSET	0
#define RSTMGR_REG_CHIP_SOFT_RST_OFFSET	4

#define RSTMGR_WR_PASSWORD		0xa5a5
#define RSTMGR_WR_PASSWORD_SHIFT	8
#define RSTMGR_WR_ACCESS_ENABLE		1

static void bcm21664_restart(enum reboot_mode mode, const char *cmd)
{
	void __iomem *base;
	struct device_node *resetmgr;

	resetmgr = of_find_compatible_node(NULL, NULL, RSTMGR_DT_STRING);
	if (!resetmgr) {
		pr_emerg("Couldn't find " RSTMGR_DT_STRING "\n");
		return;
	}
	base = of_iomap(resetmgr, 0);
	if (!base) {
		pr_emerg("Couldn't map " RSTMGR_DT_STRING "\n");
		return;
	}

	/*
	 * A soft reset is triggered by writing a 0 to bit 0 of the soft reset
	 * register. To write to that register we must first write the password
	 * and the enable bit in the write access enable register.
	 */
	writel((RSTMGR_WR_PASSWORD << RSTMGR_WR_PASSWORD_SHIFT) |
		RSTMGR_WR_ACCESS_ENABLE,
		base + RSTMGR_REG_WR_ACCESS_OFFSET);
	writel(0, base + RSTMGR_REG_CHIP_SOFT_RST_OFFSET);

	/* Wait for reset */
	while (1);
}

static void __init bcm21664_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL,
		&platform_bus);
	kona_l2_cache_init();
}

static const char * const bcm21664_dt_compat[] = {
	"brcm,bcm21664",
	NULL,
};

DT_MACHINE_START(BCM21664_DT, "BCM21664 Broadcom Application Processor")
	.init_machine = bcm21664_init,
	.restart = bcm21664_restart,
	.dt_compat = bcm21664_dt_compat,
MACHINE_END
