// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keystone SOC SMP platform code
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Cyril Chemparathy <cyril@ti.com>
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 *
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/pgtable.h>

#include <asm/smp_plat.h>
#include <asm/prom.h>
#include <asm/tlbflush.h>

#include "keystone.h"

static int keystone_smp_boot_secondary(unsigned int cpu,
						struct task_struct *idle)
{
	unsigned long start = virt_to_idmap(&secondary_startup);
	int error;

	pr_debug("keystone-smp: booting cpu %d, vector %08lx\n",
		 cpu, start);

	error = keystone_cpu_smc(KEYSTONE_MON_CPU_UP_IDX, cpu, start);
	if (error)
		pr_err("CPU %d bringup failed with %d\n", cpu, error);

	return error;
}

const struct smp_operations keystone_smp_ops __initconst = {
	.smp_boot_secondary	= keystone_smp_boot_secondary,
};
