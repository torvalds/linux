/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#define CPU_RESET		0x00

#define RESET_VECT		0x00
#define SW_RESET_ADDR		0x94

extern u32 boot_inst;

static void __iomem *cpu_ctrl;

static inline void berlin_perform_reset_cpu(unsigned int cpu)
{
	u32 val;

	val = readl(cpu_ctrl + CPU_RESET);
	val |= BIT(cpu_logical_map(cpu));
	writel(val, cpu_ctrl + CPU_RESET);
}

static int berlin_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (!cpu_ctrl)
		return -EFAULT;

	/*
	 * Reset the CPU, making it to execute the instruction in the reset
	 * exception vector.
	 */
	berlin_perform_reset_cpu(cpu);

	return 0;
}

static void __init berlin_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *scu_base;
	void __iomem *vectors_base;

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");
	scu_base = of_iomap(np, 0);
	of_node_put(np);
	if (!scu_base)
		return;

	np = of_find_compatible_node(NULL, NULL, "marvell,berlin-cpu-ctrl");
	cpu_ctrl = of_iomap(np, 0);
	of_node_put(np);
	if (!cpu_ctrl)
		goto unmap_scu;

	vectors_base = ioremap(CONFIG_VECTORS_BASE, SZ_32K);
	if (!vectors_base)
		goto unmap_scu;

	scu_enable(scu_base);
	flush_cache_all();

	/*
	 * Write the first instruction the CPU will execute after being reset
	 * in the reset exception vector.
	 */
	writel(boot_inst, vectors_base + RESET_VECT);

	/*
	 * Write the secondary startup address into the SW reset address
	 * vector. This is used by boot_inst.
	 */
	writel(virt_to_phys(secondary_startup), vectors_base + SW_RESET_ADDR);

	iounmap(vectors_base);
unmap_scu:
	iounmap(scu_base);
}

static struct smp_operations berlin_smp_ops __initdata = {
	.smp_prepare_cpus	= berlin_smp_prepare_cpus,
	.smp_boot_secondary	= berlin_boot_secondary,
};
CPU_METHOD_OF_DECLARE(berlin_smp, "marvell,berlin-smp", &berlin_smp_ops);
