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
#include <asm/hardware/gic.h>

#include "core.h"

extern void secondary_startup(void);

static void __cpuinit highbank_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

static int __cpuinit highbank_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	gic_raise_softirq(cpumask_of(cpu), 0);
	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init highbank_smp_init_cpus(void)
{
	unsigned int i, ncores;

	ncores = scu_get_core_count(scu_base_addr);

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

	set_smp_cross_call(gic_raise_softirq);
}

static void __init highbank_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	scu_enable(scu_base_addr);

	/*
	 * Write the address of secondary startup into the jump table
	 * The cores are in wfi and wait until they receive a soft interrupt
	 * and a non-zero value to jump to. Then the secondary CPU branches
	 * to this address.
	 */
	for (i = 1; i < max_cpus; i++)
		highbank_set_cpu_jump(i, secondary_startup);
}

struct smp_operations highbank_smp_ops __initdata = {
	.smp_init_cpus		= highbank_smp_init_cpus,
	.smp_prepare_cpus	= highbank_smp_prepare_cpus,
	.smp_secondary_init	= highbank_secondary_init,
	.smp_boot_secondary	= highbank_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= highbank_cpu_die,
#endif
};
