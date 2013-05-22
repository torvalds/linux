/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright 2012 Pavel Machek <pavel@denx.de>
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
 * Copyright (C) 2012-2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#include "core.h"

static void __cpuinit socfpga_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);
}

static int __cpuinit socfpga_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int trampoline_size = &secondary_trampoline_end - &secondary_trampoline;

	if (cpu1start_addr) {
		memcpy(phys_to_virt(0), &secondary_trampoline, trampoline_size);

		__raw_writel(virt_to_phys(socfpga_secondary_startup),
			(sys_manager_base_addr+(cpu1start_addr & 0x000000ff)));

		flush_cache_all();
		smp_wmb();
		outer_clean_range(0, trampoline_size);

		/* This will release CPU #1 out of reset.*/
		__raw_writel(0, rst_manager_base_addr + 0x10);
	}

	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init socfpga_smp_init_cpus(void)
{
	unsigned int i, ncores;

	ncores = scu_get_core_count(socfpga_scu_base_addr);

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	/* sanity check */
	if (ncores > num_possible_cpus()) {
		pr_warn("socfpga: no. of cores (%d) greater than configured"
			"maximum of %d - clipping\n", ncores, num_possible_cpus());
		ncores = num_possible_cpus();
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

static void __init socfpga_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(socfpga_scu_base_addr);
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
static void socfpga_cpu_die(unsigned int cpu)
{
	/* Flush the L1 data cache. */
	flush_cache_all();

	/* This will put CPU #1 into reset.*/
	__raw_writel(RSTMGR_MPUMODRST_CPU1, rst_manager_base_addr + 0x10);

	cpu_do_idle();

	/* We should have never returned from idle */
	panic("cpu %d unexpectedly exit from shutdown\n", cpu);
}

struct smp_operations socfpga_smp_ops __initdata = {
	.smp_init_cpus		= socfpga_smp_init_cpus,
	.smp_prepare_cpus	= socfpga_smp_prepare_cpus,
	.smp_boot_secondary	= socfpga_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= socfpga_cpu_die,
#endif
};
