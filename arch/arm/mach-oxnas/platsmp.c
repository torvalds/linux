/*
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2013 Ma Haijun <mahaijuns@gmail.com>
 * Copyright (C) 2002 ARM Ltd.
 * All Rights Reserved
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
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

extern void ox820_secondary_startup(void);
extern void ox820_cpu_die(unsigned int cpu);

static void __iomem *cpu_ctrl;
static void __iomem *gic_cpu_ctrl;

#define HOLDINGPEN_CPU_OFFSET		0xc8
#define HOLDINGPEN_LOCATION_OFFSET	0xc4

#define GIC_NCPU_OFFSET(cpu)		(0x100 + (cpu)*0x100)
#define GIC_CPU_CTRL			0x00
#define GIC_CPU_CTRL_ENABLE		1

int __init ox820_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The BootMonitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	writel(virt_to_phys(ox820_secondary_startup),
			cpu_ctrl + HOLDINGPEN_LOCATION_OFFSET);

	writel(cpu, cpu_ctrl + HOLDINGPEN_CPU_OFFSET);

	/*
	 * Enable GIC cpu interface in CPU Interface Control Register
	 */
	writel(GIC_CPU_CTRL_ENABLE,
		gic_cpu_ctrl + GIC_NCPU_OFFSET(cpu) + GIC_CPU_CTRL);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	return 0;
}

static void __init ox820_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *scu_base;

	np = of_find_compatible_node(NULL, NULL, "arm,arm11mp-scu");
	scu_base = of_iomap(np, 0);
	of_node_put(np);
	if (!scu_base)
		return;

	/* Remap CPU Interrupt Interface Registers */
	np = of_find_compatible_node(NULL, NULL, "arm,arm11mp-gic");
	gic_cpu_ctrl = of_iomap(np, 1);
	of_node_put(np);
	if (!gic_cpu_ctrl)
		goto unmap_scu;

	np = of_find_compatible_node(NULL, NULL, "oxsemi,ox820-sys-ctrl");
	cpu_ctrl = of_iomap(np, 0);
	of_node_put(np);
	if (!cpu_ctrl)
		goto unmap_scu;

	scu_enable(scu_base);
	flush_cache_all();

unmap_scu:
	iounmap(scu_base);
}

static const struct smp_operations ox820_smp_ops __initconst = {
	.smp_prepare_cpus	= ox820_smp_prepare_cpus,
	.smp_boot_secondary	= ox820_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= ox820_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(ox820_smp, "oxsemi,ox820-smp", &ox820_smp_ops);
