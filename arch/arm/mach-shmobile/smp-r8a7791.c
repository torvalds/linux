/*
 * SMP support for r8a7791
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/smp_plat.h>

#include "common.h"
#include "r8a7791.h"
#include "rcar-gen2.h"

static void __init r8a7791_smp_prepare_cpus(unsigned int max_cpus)
{
	/* let APMU code install data related to shmobile_boot_vector */
	shmobile_smp_apmu_prepare_cpus(max_cpus);

	r8a7791_pm_init();
	shmobile_smp_apmu_suspend_init();
}

static int r8a7791_smp_boot_secondary(unsigned int cpu,
				      struct task_struct *idle)
{
	/* Error out when hardware debug mode is enabled */
	if (rcar_gen2_read_mode_pins() & BIT(21)) {
		pr_warn("Unable to boot CPU%u when MD21 is set\n", cpu);
		return -ENOTSUPP;
	}

	return shmobile_smp_apmu_boot_secondary(cpu, idle);
}

struct smp_operations r8a7791_smp_ops __initdata = {
	.smp_prepare_cpus	= r8a7791_smp_prepare_cpus,
	.smp_boot_secondary	= r8a7791_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= shmobile_smp_cpu_disable,
	.cpu_die		= shmobile_smp_apmu_cpu_die,
	.cpu_kill		= shmobile_smp_apmu_cpu_kill,
#endif
};
