// SPDX-License-Identifier: GPL-2.0
/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2011  Paul Mundt
 *
 * Based on vexpress, Copyright (C) 2002 ARM Ltd, All Rights Reserved
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
