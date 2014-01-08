/*
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2013 Hisilicon Limited.
 * Based on arch/arm/mach-vexpress/platsmp.c, Copyright (C) 2002 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include "core.h"

static void __iomem *ctrl_base;

void hi3xxx_set_cpu_jump(int cpu, void *jump_addr)
{
	cpu = cpu_logical_map(cpu);
	if (!cpu || !ctrl_base)
		return;
	writel_relaxed(virt_to_phys(jump_addr), ctrl_base + ((cpu - 1) << 2));
}

int hi3xxx_get_cpu_jump(int cpu)
{
	cpu = cpu_logical_map(cpu);
	if (!cpu || !ctrl_base)
		return 0;
	return readl_relaxed(ctrl_base + ((cpu - 1) << 2));
}

static void __init hi3xxx_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np = NULL;
	unsigned long base = 0;
	u32 offset = 0;
	void __iomem *scu_base = NULL;

	if (scu_a9_has_base()) {
		base = scu_a9_get_base();
		scu_base = ioremap(base, SZ_4K);
		if (!scu_base) {
			pr_err("ioremap(scu_base) failed\n");
			return;
		}
		scu_enable(scu_base);
		iounmap(scu_base);
	}
	if (!ctrl_base) {
		np = of_find_compatible_node(NULL, NULL, "hisilicon,sysctrl");
		if (!np) {
			pr_err("failed to find hisilicon,sysctrl node\n");
			return;
		}
		ctrl_base = of_iomap(np, 0);
		if (!ctrl_base) {
			pr_err("failed to map address\n");
			return;
		}
		if (of_property_read_u32(np, "smp-offset", &offset) < 0) {
			pr_err("failed to find smp-offset property\n");
			return;
		}
		ctrl_base += offset;
	}
}

static int hi3xxx_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	hi3xxx_set_cpu(cpu, true);
	hi3xxx_set_cpu_jump(cpu, secondary_startup);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	return 0;
}

struct smp_operations hi3xxx_smp_ops __initdata = {
	.smp_prepare_cpus	= hi3xxx_smp_prepare_cpus,
	.smp_boot_secondary	= hi3xxx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= hi3xxx_cpu_die,
	.cpu_kill		= hi3xxx_cpu_kill,
#endif
};
