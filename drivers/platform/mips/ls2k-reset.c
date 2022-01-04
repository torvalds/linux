// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2021, Qing Zhang <zhangqing@loongson.cn>
 *  Loongson-2K1000 reset support
 */

#include <linux/of_address.h>
#include <linux/pm.h>
#include <asm/reboot.h>

#define	PM1_STS		0x0c /* Power Management 1 Status Register */
#define	PM1_CNT		0x14 /* Power Management 1 Control Register */
#define	RST_CNT		0x30 /* Reset Control Register */

static void __iomem *base;

static void ls2k_restart(char *command)
{
	writel(0x1, base + RST_CNT);
}

static void ls2k_poweroff(void)
{
	/* Clear */
	writel((readl(base + PM1_STS) & 0xffffffff), base + PM1_STS);
	/* Sleep Enable | Soft Off*/
	writel(GENMASK(12, 10) | BIT(13), base + PM1_CNT);
}

static int ls2k_reset_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "loongson,ls2k-pm");
	if (!np) {
		pr_info("Failed to get PM node\n");
		return -ENODEV;
	}

	base = of_iomap(np, 0);
	of_node_put(np);
	if (!base) {
		pr_info("Failed to map PM register base address\n");
		return -ENOMEM;
	}

	_machine_restart = ls2k_restart;
	pm_power_off = ls2k_poweroff;

	return 0;
}

arch_initcall(ls2k_reset_init);
