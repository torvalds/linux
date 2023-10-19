// SPDX-License-Identifier: GPL-2.0
/*
 * SMP support for SoCs with SCU covered by mach-shmobile
 *
 * Copyright (C) 2013  Magnus Damm
 */
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include "common.h"


static phys_addr_t shmobile_scu_base_phys;
static void __iomem *shmobile_scu_base;

static int shmobile_scu_cpu_prepare(unsigned int cpu)
{
	/* For this particular CPU register SCU SMP boot vector */
	shmobile_smp_hook(cpu, __pa_symbol(shmobile_boot_scu),
			  shmobile_scu_base_phys);
	return 0;
}

void __init shmobile_smp_scu_prepare_cpus(phys_addr_t scu_base_phys,
					  unsigned int max_cpus)
{
	/* install boot code shared by all CPUs */
	shmobile_boot_fn = __pa_symbol(shmobile_smp_boot);

	/* enable SCU and cache coherency on booting CPU */
	shmobile_scu_base_phys = scu_base_phys;
	shmobile_scu_base = ioremap(scu_base_phys, PAGE_SIZE);
	scu_enable(shmobile_scu_base);
	scu_power_mode(shmobile_scu_base, SCU_PM_NORMAL);

	/* Use CPU notifier for reset vector control */
	cpuhp_setup_state_nocalls(CPUHP_ARM_SHMOBILE_SCU_PREPARE,
				  "arm/shmobile-scu:prepare",
				  shmobile_scu_cpu_prepare, NULL);
}

#ifdef CONFIG_HOTPLUG_CPU
void shmobile_smp_scu_cpu_die(unsigned int cpu)
{
	/* For this particular CPU deregister boot vector */
	shmobile_smp_hook(cpu, 0, 0);

	dsb();
	flush_cache_all();

	/* disable cache coherency */
	scu_power_mode(shmobile_scu_base, SCU_PM_POWEROFF);

	/* jump to shared mach-shmobile sleep / reset code */
	shmobile_smp_sleep();
}

static int shmobile_smp_scu_psr_core_disabled(int cpu)
{
	unsigned long mask = SCU_PM_POWEROFF << (cpu * 8);

	if ((readl(shmobile_scu_base + 8) & mask) == mask)
		return 1;

	return 0;
}

int shmobile_smp_scu_cpu_kill(unsigned int cpu)
{
	int k;

	/* this function is running on another CPU than the offline target,
	 * here we need wait for shutdown code in platform_cpu_die() to
	 * finish before asking SoC-specific code to power off the CPU core.
	 */
	for (k = 0; k < 1000; k++) {
		if (shmobile_smp_scu_psr_core_disabled(cpu))
			return 1;

		mdelay(1);
	}

	return 0;
}
#endif
