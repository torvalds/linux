/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
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
#include <asm/localtimer.h>

static unsigned int __init shmobile_smp_get_core_count(void)
{
	return 1;
}

static void __init shmobile_smp_prepare_cpus(void)
{
	/* do nothing for now */
}


void __cpuinit platform_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return -ENOSYS;
}

void __init smp_init_cpus(void)
{
	unsigned int ncores = shmobile_smp_get_core_count();
	unsigned int i;

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int ncores = shmobile_smp_get_core_count();
	unsigned int cpu = smp_processor_id();
	int i;

	smp_store_cpu_info(cpu);

	if (max_cpus > ncores)
		max_cpus = ncores;

	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	if (max_cpus > 1) {
		shmobile_smp_prepare_cpus();

		/*
		 * Enable the local timer or broadcast device for the
		 * boot CPU, but only if we have more than one CPU.
		 */
		percpu_timer_setup();
	}
}
