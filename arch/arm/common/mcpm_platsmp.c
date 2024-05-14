// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-vexpress/mcpm_platsmp.c
 *
 * Created by:  Nicolas Pitre, November 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * Code to handle secondary CPU bringup and hotplug for the cluster power API.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

#include <asm/mcpm.h>
#include <asm/smp.h>
#include <asm/smp_plat.h>

static void cpu_to_pcpu(unsigned int cpu,
			unsigned int *pcpu, unsigned int *pcluster)
{
	unsigned int mpidr;

	mpidr = cpu_logical_map(cpu);
	*pcpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	*pcluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
}

static int mcpm_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned int pcpu, pcluster, ret;
	extern void secondary_startup(void);

	cpu_to_pcpu(cpu, &pcpu, &pcluster);

	pr_debug("%s: logical CPU %d is physical CPU %d cluster %d\n",
		 __func__, cpu, pcpu, pcluster);

	mcpm_set_entry_vector(pcpu, pcluster, NULL);
	ret = mcpm_cpu_power_up(pcpu, pcluster);
	if (ret)
		return ret;
	mcpm_set_entry_vector(pcpu, pcluster, secondary_startup);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	dsb_sev();
	return 0;
}

static void mcpm_secondary_init(unsigned int cpu)
{
	mcpm_cpu_powered_up();
}

#ifdef CONFIG_HOTPLUG_CPU

static int mcpm_cpu_kill(unsigned int cpu)
{
	unsigned int pcpu, pcluster;

	cpu_to_pcpu(cpu, &pcpu, &pcluster);

	return !mcpm_wait_for_cpu_powerdown(pcpu, pcluster);
}

static bool mcpm_cpu_can_disable(unsigned int cpu)
{
	/* We assume all CPUs may be shut down. */
	return true;
}

static void mcpm_cpu_die(unsigned int cpu)
{
	unsigned int mpidr, pcpu, pcluster;
	mpidr = read_cpuid_mpidr();
	pcpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	pcluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	mcpm_set_entry_vector(pcpu, pcluster, NULL);
	mcpm_cpu_power_down();
}

#endif

static const struct smp_operations mcpm_smp_ops __initconst = {
	.smp_boot_secondary	= mcpm_boot_secondary,
	.smp_secondary_init	= mcpm_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= mcpm_cpu_kill,
	.cpu_can_disable	= mcpm_cpu_can_disable,
	.cpu_die		= mcpm_cpu_die,
#endif
};

void __init mcpm_smp_set_ops(void)
{
	smp_set_ops(&mcpm_smp_ops);
}
