/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2011  Paul Mundt
 *
 * Based on vexpress, Copyright (C) 2002 ARM Ltd, All Rights Reserved
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
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>
#include <asm/mach-types.h>
#include <mach/common.h>

static unsigned int __init shmobile_smp_get_core_count(void)
{
	if (machine_is_ag5evm())
		return sh73a0_get_core_count();

	return 1;
}

static void __init shmobile_smp_prepare_cpus(void)
{
	if (machine_is_ag5evm())
		sh73a0_smp_prepare_cpus();
}

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();

	if (machine_is_ag5evm())
		sh73a0_secondary_init(cpu);
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (machine_is_ag5evm())
		return sh73a0_boot_secondary(cpu);

	return -ENOSYS;
}

void __init smp_init_cpus(void)
{
	unsigned int ncores = shmobile_smp_get_core_count();
	unsigned int i;

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	shmobile_smp_prepare_cpus();
}
