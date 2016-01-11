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

static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	sync_cache_w(&pen_release);
}

static DEFINE_SPINLOCK(boot_lock);

static void sti_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int sti_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	write_pen_release(cpu_logical_map(cpu));

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * it to jump to the secondary entrypoint.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

static void __init sti_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *scu_base;
	u32 __iomem *cpu_strt_ptr;
	u32 release_phys;
	int cpu;
	unsigned long entry_pa = virt_to_phys(sti_secondary_startup);

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
		 * holding pen is usually configured in SBC DMEM but can also be
		 * in RAM.
		 */

		if (!memblock_is_memory(release_phys))
			cpu_strt_ptr =
				ioremap(release_phys, sizeof(release_phys));
		else
			cpu_strt_ptr =
				(u32 __iomem *)phys_to_virt(release_phys);

		__raw_writel(entry_pa, cpu_strt_ptr);

		/*
		 * wmb so that data is actually written
		 * before cache flush is done
		 */
		smp_wmb();
		sync_cache_w(cpu_strt_ptr);

		if (!memblock_is_memory(release_phys))
			iounmap(cpu_strt_ptr);
	}
}

struct smp_operations __initdata sti_smp_ops = {
	.smp_prepare_cpus	= sti_smp_prepare_cpus,
	.smp_secondary_init	= sti_secondary_init,
	.smp_boot_secondary	= sti_boot_secondary,
};
