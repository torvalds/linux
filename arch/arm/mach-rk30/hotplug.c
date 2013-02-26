/*
 * RK30 SMP cpu-hotplug support
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 * Copyright (C) 2002 ARM Ltd.
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/system.h>

#include <mach/pmu.h>

static cpumask_t dead_cpus;

int platform_cpu_kill(unsigned int cpu)
{
	int k;

	/* this function is running on another CPU than the offline target,
	 * here we need wait for shutdown code in platform_cpu_die() to
	 * finish before asking SoC-specific code to power off the CPU core.
	 */
	for (k = 0; k < 1000; k++) {
		if (cpumask_test_cpu(cpu, &dead_cpus)) {
			mdelay(1);
			pmu_set_power_domain(PD_A9_0 + cpu, false);
			return 1;
		}

		mdelay(1);
	}

	return 0;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void platform_cpu_die(unsigned int cpu)
{
	unsigned int v;

	/* hardware shutdown code running on the CPU that is being offlined */
	flush_cache_all();
	dsb();

	/* notify platform_cpu_kill() that hardware shutdown is finished */
	cpumask_set_cpu(cpu, &dead_cpus);
	clean_dcache_area(&dead_cpus, sizeof(dead_cpus));

	asm volatile(
	"       mcr     p15, 0, %1, c7, c5, 0\n"
	"       mcr     p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"       mrc     p15, 0, %0, c1, c0, 1\n"
	"       bic     %0, %0, %3\n"		// clear ACTLR.SMP | ACTLR.FW
	"       mcr     p15, 0, %0, c1, c0, 1\n"
	"       mrc     p15, 0, %0, c1, c0, 0\n"
	"       bic     %0, %0, %2\n"
	"       mcr     p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" ((1 << 6) | (1 << 0))
	  : "cc");

	/* wait for SoC code in platform_cpu_kill() to shut off CPU core
	 * power. CPU bring up starts from the reset vector.
	 */
	while (1) {
		dsb();
		wfi();
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
