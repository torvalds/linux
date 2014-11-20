/*
 * Copyright (C) 2012 Samsung Electronics.
 * Kyungmin Park <kyungmin.park@samsung.com>
 * Tomasz Figa <t.figa@samsung.com>
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/firmware.h>

#include <mach/map.h>

#include "common.h"
#include "smc.h"

static int exynos_do_idle(void)
{
	exynos_smc(SMC_CMD_SLEEP, 0, 0, 0);
	return 0;
}

static int exynos_cpu_boot(int cpu)
{
	/*
	 * Exynos3250 doesn't need to send smc command for secondary CPU boot
	 * because Exynos3250 removes WFE in secure mode.
	 */
	if (soc_is_exynos3250())
		return 0;

	/*
	 * The second parameter of SMC_CMD_CPU1BOOT command means CPU id.
	 * But, Exynos4212 has only one secondary CPU so second parameter
	 * isn't used for informing secure firmware about CPU id.
	 */
	if (soc_is_exynos4212())
		cpu = 0;

	exynos_smc(SMC_CMD_CPU1BOOT, cpu, 0, 0);
	return 0;
}

static int exynos_set_cpu_boot_addr(int cpu, unsigned long boot_addr)
{
	void __iomem *boot_reg;

	if (!sysram_ns_base_addr)
		return -ENODEV;

	boot_reg = sysram_ns_base_addr + 0x1c;

	/*
	 * Almost all Exynos-series of SoCs that run in secure mode don't need
	 * additional offset for every CPU, with Exynos4412 being the only
	 * exception.
	 */
	if (soc_is_exynos4412())
		boot_reg += 4 * cpu;

	__raw_writel(boot_addr, boot_reg);
	return 0;
}

static const struct firmware_ops exynos_firmware_ops = {
	.do_idle		= exynos_do_idle,
	.set_cpu_boot_addr	= exynos_set_cpu_boot_addr,
	.cpu_boot		= exynos_cpu_boot,
};

void __init exynos_firmware_init(void)
{
	struct device_node *nd;
	const __be32 *addr;

	nd = of_find_compatible_node(NULL, NULL,
					"samsung,secure-firmware");
	if (!nd)
		return;

	addr = of_get_address(nd, 0, NULL, NULL);
	if (!addr) {
		pr_err("%s: No address specified.\n", __func__);
		return;
	}

	pr_info("Running under secure firmware.\n");

	register_firmware_ops(&exynos_firmware_ops);
}
