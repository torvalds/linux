/*
 * Copyright (C) 2002 ARM Ltd.
 * Copyright (C) 2008 STMicroelctronics.
 * Copyright (C) 2009 ST-Ericsson.
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 *
 * This file is based on arm realview platform
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include "setup.h"

#include "db8500-regs.h"
#include "id.h"

/* Magic triggers in backup RAM */
#define UX500_CPU1_JUMPADDR_OFFSET 0x1FF4
#define UX500_CPU1_WAKEMAGIC_OFFSET 0x1FF0

static void wakeup_secondary(void)
{
	struct device_node *np;
	static void __iomem *backupram;

	np = of_find_compatible_node(NULL, NULL, "ste,dbx500-backupram");
	if (!np) {
		pr_err("No backupram base address\n");
		return;
	}
	backupram = of_iomap(np, 0);
	of_node_put(np);
	if (!backupram) {
		pr_err("No backupram remap\n");
		return;
	}

	/*
	 * write the address of secondary startup into the backup ram register
	 * at offset 0x1FF4, then write the magic number 0xA1FEED01 to the
	 * backup ram register at offset 0x1FF0, which is what boot rom code
	 * is waiting for. This will wake up the secondary core from WFE.
	 */
	writel(virt_to_phys(secondary_startup),
	       backupram + UX500_CPU1_JUMPADDR_OFFSET);
	writel(0xA1FEED01,
	       backupram + UX500_CPU1_WAKEMAGIC_OFFSET);

	/* make sure write buffer is drained */
	mb();
	iounmap(backupram);
}

static void __init ux500_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	static void __iomem *scu_base;
	unsigned int ncores;
	int i;

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");
	if (!np) {
		pr_err("No SCU base address\n");
		return;
	}
	scu_base = of_iomap(np, 0);
	of_node_put(np);
	if (!scu_base) {
		pr_err("No SCU remap\n");
		return;
	}

	scu_enable(scu_base);
	ncores = scu_get_core_count(scu_base);
	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
	iounmap(scu_base);
}

static int ux500_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	wakeup_secondary();
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	return 0;
}

struct smp_operations ux500_smp_ops __initdata = {
	.smp_prepare_cpus	= ux500_smp_prepare_cpus,
	.smp_boot_secondary	= ux500_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= ux500_cpu_die,
#endif
};
CPU_METHOD_OF_DECLARE(ux500_smp, "ste,dbx500-smp", &ux500_smp_ops);
