/*
 * SMP support for SoCs with SCU covered by mach-shmobile
 *
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <mach/common.h>

void __init shmobile_smp_scu_prepare_cpus(unsigned int max_cpus)
{
	shmobile_boot_fn = virt_to_phys(shmobile_boot_scu);
	shmobile_boot_arg = (unsigned long)shmobile_scu_base;

	/* enable SCU and cache coherency on booting CPU */
	scu_enable(shmobile_scu_base);
	scu_power_mode(shmobile_scu_base, SCU_PM_NORMAL);
}

int shmobile_smp_scu_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	/* do nothing for now */
	return 0;
}
