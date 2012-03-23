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
#include <mach/common.h>
#include <asm/cacheflush.h>

static cpumask_t dead_cpus;

int platform_cpu_kill(unsigned int cpu)
{
	int k;

	/* this function is running on another CPU than the offline target,
	 * here we need wait for shutdown code in platform_cpu_die() to
	 * finish before asking SoC-specific code to power off the CPU core.
	 */
	for (k = 0; k < 1000; k++) {
		if (cpumask_test_cpu(cpu, &dead_cpus))
			return shmobile_platform_cpu_kill(cpu);

		mdelay(1);
	}

	return 0;
}

void platform_cpu_die(unsigned int cpu)
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

int platform_cpu_disable(unsigned int cpu)
{
	cpumask_clear_cpu(cpu, &dead_cpus);
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
