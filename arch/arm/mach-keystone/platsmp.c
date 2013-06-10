/*
 * Keystone SOC SMP platform code
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Cyril Chemparathy <cyril@ti.com>
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 *
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/smp_plat.h>
#include <asm/prom.h>

#include "keystone.h"

static int __cpuinit keystone_smp_boot_secondary(unsigned int cpu,
						struct task_struct *idle)
{
	unsigned long start = virt_to_phys(&secondary_startup);
	int error;

	pr_debug("keystone-smp: booting cpu %d, vector %08lx\n",
		 cpu, start);

	asm volatile (
		"mov    r0, #0\n"	/* power on cmd	*/
		"mov    r1, %1\n"	/* cpu		*/
		"mov    r2, %2\n"	/* start	*/
		".inst  0xe1600070\n"	/* smc #0	*/
		"mov    %0, r0\n"
		: "=r" (error)
		: "r"(cpu), "r"(start)
		: "cc", "r0", "r1", "r2", "memory"
	);

	pr_debug("keystone-smp: monitor returned %d\n", error);

	return error;
}

struct smp_operations keystone_smp_ops __initdata = {
	.smp_init_cpus		= arm_dt_init_cpu_maps,
	.smp_boot_secondary	= keystone_smp_boot_secondary,
};
