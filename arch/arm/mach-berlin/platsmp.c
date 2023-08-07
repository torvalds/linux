// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/page.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

/*
 * There are two reset registers, one with self-clearing (SC)
 * reset and one with non-self-clearing reset (NON_SC).
 */
#define CPU_RESET_SC		0x00
#define CPU_RESET_NON_SC	0x20

#define RESET_VECT		0x00
#define SW_RESET_ADDR		0x94

extern u32 boot_inst;

static void __iomem *cpu_ctrl;

static inline void berlin_perform_reset_cpu(unsigned int cpu)
{
	u32 val;

	val = readl(cpu_ctrl + CPU_RESET_NON_SC);
	val &= ~BIT(cpu_logical_map(cpu));
	writel(val, cpu_ctrl + CPU_RESET_NON_SC);
	val |= BIT(cpu_logical_map(cpu));
	writel(val, cpu_ctrl + CPU_RESET_NON_SC);
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

	vectors_base = ioremap(VECTORS_BASE, SZ_32K);
	if (!vectors_base)
		goto unmap_scu;

	scu_enable(scu_base);

	/*
	 * Write the first instruction the CPU will execute after being reset
	 * in the reset exception vector.
	 */
	writel(boot_inst, vectors_base + RESET_VECT);

	/*
	 * Write the secondary startup address into the SW reset address
	 * vector. This is used by boot_inst.
	 */
	writel(__pa_symbol(secondary_startup), vectors_base + SW_RESET_ADDR);

	iounmap(vectors_base);
unmap_scu:
	iounmap(scu_base);
}

#ifdef CONFIG_HOTPLUG_CPU
static void berlin_cpu_die(unsigned int cpu)
{
	v7_exit_coherency_flush(louis);
	while (1)
		cpu_do_idle();
}

static int berlin_cpu_kill(unsigned int cpu)
{
	u32 val;

	val = readl(cpu_ctrl + CPU_RESET_NON_SC);
	val &= ~BIT(cpu_logical_map(cpu));
	writel(val, cpu_ctrl + CPU_RESET_NON_SC);

	return 1;
}
#endif

static const struct smp_operations berlin_smp_ops __initconst = {
	.smp_prepare_cpus	= berlin_smp_prepare_cpus,
	.smp_boot_secondary	= berlin_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= berlin_cpu_die,
	.cpu_kill		= berlin_cpu_kill,
#endif
};
CPU_METHOD_OF_DECLARE(berlin_smp, "marvell,berlin-smp", &berlin_smp_ops);
