/*
 *  arch/arm/mach-sti/platsmp.c
 *
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 *		http://www.st.com
 *
 * Cloned from linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include "smp.h"

static u32 __iomem *cpu_strt_ptr;

static int sti_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long entry_pa = __pa_symbol(secondary_startup);

	/*
	 * Secondary CPU is initialised and started by a U-BOOTROM firmware.
	 * Secondary CPU is spinning and waiting for a write at cpu_strt_ptr.
	 * Writing secondary_startup address at cpu_strt_ptr makes it to
	 * jump directly to secondary_startup().
	 */
	__raw_writel(entry_pa, cpu_strt_ptr);

	/* wmb so that data is actually written before cache flush is done */
	smp_wmb();
	sync_cache_w(cpu_strt_ptr);

	return 0;
}

static void __init sti_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *scu_base;
	u32 release_phys;
	int cpu;

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");

	if (np) {
		scu_base = of_iomap(np, 0);
		scu_enable(scu_base);
		of_node_put(np);
	}

	if (max_cpus <= 1)
		return;

	for_each_possible_cpu(cpu) {

		np = of_get_cpu_node(cpu, NULL);

		if (!np)
			continue;

		if (of_property_read_u32(np, "cpu-release-addr",
						&release_phys)) {
			pr_err("CPU %d: missing or invalid cpu-release-addr "
				"property\n", cpu);
			continue;
		}

		/*
		 * cpu-release-addr is usually configured in SBC DMEM but can
		 * also be in RAM.
		 */

		if (!memblock_is_memory(release_phys))
			cpu_strt_ptr =
				ioremap(release_phys, sizeof(release_phys));
		else
			cpu_strt_ptr =
				(u32 __iomem *)phys_to_virt(release_phys);

		set_cpu_possible(cpu, true);
	}
}

const struct smp_operations sti_smp_ops __initconst = {
	.smp_prepare_cpus	= sti_smp_prepare_cpus,
	.smp_boot_secondary	= sti_boot_secondary,
};
