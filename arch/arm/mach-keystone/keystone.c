/*
 * Keystone2 based boards and SOC related code.
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Cyril Chemparathy <cyril@ti.com>
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/smp_plat.h>

#include "keystone.h"

#define PLL_RESET_WRITE_KEY_MASK		0xffff0000
#define PLL_RESET_WRITE_KEY			0x5a69
#define PLL_RESET				BIT(16)

static void __iomem *keystone_rstctrl;

static void __init keystone_init(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "ti,keystone-reset");
	if (WARN_ON(!node))
		pr_warn("ti,keystone-reset node undefined\n");

	keystone_rstctrl = of_iomap(node, 0);
	if (WARN_ON(!keystone_rstctrl))
		pr_warn("ti,keystone-reset iomap error\n");

	keystone_pm_runtime_init();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *keystone_match[] __initconst = {
	"ti,keystone",
	NULL,
};

void keystone_restart(enum reboot_mode mode, const char *cmd)
{
	u32 val;

	BUG_ON(!keystone_rstctrl);

	/* Enable write access to RSTCTRL */
	val = readl(keystone_rstctrl);
	val &= PLL_RESET_WRITE_KEY_MASK;
	val |= PLL_RESET_WRITE_KEY;
	writel(val, keystone_rstctrl);

	/* Reset the SOC */
	val = readl(keystone_rstctrl);
	val &= ~PLL_RESET;
	writel(val, keystone_rstctrl);
}

DT_MACHINE_START(KEYSTONE, "Keystone")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.smp		= smp_ops(keystone_smp_ops),
	.init_machine	= keystone_init,
	.dt_compat	= keystone_match,
	.restart	= keystone_restart,
MACHINE_END
