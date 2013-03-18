/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 *
 * Based on realview, Copyright (C) 2002 ARM Ltd, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <mach/common.h>
#include <mach/r8a7779.h>
#include <mach/emev2.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>

static cpumask_t dead_cpus;

void shmobile_cpu_die(unsigned int cpu)
{
	/* hardware shutdown code running on the CPU that is being offlined */
	flush_cache_all();
	dsb();

	/* notify platform_cpu_kill() that hardware shutdown is finished */
	cpumask_set_cpu(cpu, &dead_cpus);

	/* wait for SoC code in platform_cpu_kill() to shut off CPU core
	 * power. CPU bring up starts from the reset vector.
	 */
	while (1) {
		/*
		 * here's the WFI
		 */
		asm(".word	0xe320f003\n"
		    :
		    :
		    : "memory", "cc");
	}
}

int shmobile_cpu_disable(unsigned int cpu)
{
	cpumask_clear_cpu(cpu, &dead_cpus);
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

int shmobile_cpu_disable_any(unsigned int cpu)
{
	cpumask_clear_cpu(cpu, &dead_cpus);
	return 0;
}

int shmobile_cpu_is_dead(unsigned int cpu)
{
	return cpumask_test_cpu(cpu, &dead_cpus);
}
