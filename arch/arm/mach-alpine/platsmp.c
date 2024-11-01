// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMP operations for Alpine platform.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/smp_plat.h>

#include "alpine_cpu_pm.h"

static int alpine_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	phys_addr_t addr;

	addr = __pa_symbol(secondary_startup);

	if (addr > (phys_addr_t)(uint32_t)(-1)) {
		pr_err("FAIL: resume address over 32bit (%pa)", &addr);
		return -EINVAL;
	}

	return alpine_cpu_wakeup(cpu_logical_map(cpu), (uint32_t)addr);
}

static void __init alpine_smp_prepare_cpus(unsigned int max_cpus)
{
	alpine_cpu_pm_init();
}

static const struct smp_operations alpine_smp_ops __initconst = {
	.smp_prepare_cpus	= alpine_smp_prepare_cpus,
	.smp_boot_secondary	= alpine_boot_secondary,
};
CPU_METHOD_OF_DECLARE(alpine_smp, "al,alpine-smp", &alpine_smp_ops);
