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
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include "common.h"

extern unsigned long shmobile_smp_fn[];
extern unsigned long shmobile_smp_arg[];
extern unsigned long shmobile_smp_mpidr[];

void shmobile_smp_hook(unsigned int cpu, unsigned long fn, unsigned long arg)
{
	shmobile_smp_fn[cpu] = 0;
	flush_cache_all();

	shmobile_smp_mpidr[cpu] = cpu_logical_map(cpu);
	shmobile_smp_fn[cpu] = fn;
	shmobile_smp_arg[cpu] = arg;
	flush_cache_all();
}

#ifdef CONFIG_HOTPLUG_CPU
bool shmobile_smp_cpu_can_disable(unsigned int cpu)
{
	return true; /* Hotplug of any CPU is supported */
}
#endif
