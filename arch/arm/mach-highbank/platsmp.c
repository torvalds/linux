/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
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
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/smp_scu.h>

#include "core.h"

extern void secondary_startup(void);

static int __cpuinit highbank_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	highbank_set_cpu_jump(cpu, secondary_startup);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init highbank_smp_init_cpus(void)
{
	unsigned int i, ncores = 4;

	/* sanity check */
	if (ncores > NR_CPUS) {
		printk(KERN_WARNING
		       "highbank: no. of cores (%d) greater than configured "
		       "maximum of %d - clipping\n",
		       ncores, NR_CPUS);
		ncores = NR_CPUS;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

static void __init highbank_smp_prepare_cpus(unsigned int max_cpus)
{
	if (scu_base_addr)
		scu_enable(scu_base_addr);
}

struct smp_operations highbank_smp_ops __initdata = {
	.smp_init_cpus		= highbank_smp_init_cpus,
	.smp_prepare_cpus	= highbank_smp_prepare_cpus,
	.smp_boot_secondary	= highbank_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= highbank_cpu_die,
#endif
};
