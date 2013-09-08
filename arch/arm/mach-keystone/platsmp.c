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

#include "keystone.h"

static int keystone_smp_boot_secondary(unsigned int cpu,
						struct task_struct *idle)
{
	unsigned long start = virt_to_phys(&secondary_startup);
	int error;

	pr_debug("keystone-smp: booting cpu %d, vector %08lx\n",
		 cpu, start);

	error = keystone_cpu_smc(KEYSTONE_MON_CPU_UP_IDX, cpu, start);
	if (error)
		pr_err("CPU %d bringup failed with %d\n", cpu, error);

	return error;
}

struct smp_operations keystone_smp_ops __initdata = {
	.smp_boot_secondary	= keystone_smp_boot_secondary,
};
