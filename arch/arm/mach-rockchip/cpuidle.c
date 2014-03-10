/*
 * Copyright (C) 2012-2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/rockchip/cpu.h>

static void __iomem *gic_cpu_base;

static int rockchip_ca9_cpuidle_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	do {
		cpu_do_idle();
	} while (readl_relaxed(gic_cpu_base + GIC_CPU_HIGHPRI) == 0x3FF);
	return 0;
}

static struct cpuidle_driver rockchip_ca9_cpuidle_driver = {
	.name = "rockchip_ca9_cpuidle",
	.owner = THIS_MODULE,
	.states[0] = ARM_CPUIDLE_WFI_STATE,
	.state_count = 1,
};

static int __init rockchip_ca9_cpuidle_init(void)
{
	struct device_node *np;
	int ret;

	if (!cpu_is_rockchip())
		return -ENODEV;
	if (read_cpuid_part_number() != ARM_CPU_PART_CORTEX_A9)
		return -ENODEV;
	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	if (!np)
		return -ENODEV;
	gic_cpu_base = of_iomap(np, 1);
	if (!gic_cpu_base) {
		pr_err("%s: failed to map gic cpu registers\n", __func__);
		return -EINVAL;
	}
	rockchip_ca9_cpuidle_driver.states[0].enter = rockchip_ca9_cpuidle_enter;
	ret = cpuidle_register(&rockchip_ca9_cpuidle_driver, NULL);
	if (ret)
		pr_err("%s: failed to register cpuidle driver: %d\n", __func__, ret);

	return ret;
}

device_initcall(rockchip_ca9_cpuidle_init);
