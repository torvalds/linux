/*
 * arch/arm/mach-vexpress/tc2_pm.c - TC2 power management support
 *
 * Created by:	Nicolas Pitre, October 2012
 * Copyright:	(C) 2012-2013  Linaro Limited
 *
 * Some portions of this file were originally written by Achin Gupta
 * Copyright:   (C) 2012  ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/errno.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cp15.h>

#include <linux/arm-cci.h>

#include "spc.h"

/* SCC conf registers */
#define RESET_CTRL		0x018
#define RESET_A15_NCORERESET(cpu)	(1 << (2 + (cpu)))
#define RESET_A7_NCORERESET(cpu)	(1 << (16 + (cpu)))

#define A15_CONF		0x400
#define A7_CONF			0x500
#define SYS_INFO		0x700
#define SPC_BASE		0xb00

static void __iomem *scc;

#define TC2_CLUSTERS			2
#define TC2_MAX_CPUS_PER_CLUSTER	3

static unsigned int tc2_nr_cpus[TC2_CLUSTERS];

static int tc2_pm_cpu_powerup(unsigned int cpu, unsigned int cluster)
{
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= TC2_CLUSTERS || cpu >= tc2_nr_cpus[cluster])
		return -EINVAL;
	ve_spc_set_resume_addr(cluster, cpu,
			       __pa_symbol(mcpm_entry_point));
	ve_spc_cpu_wakeup_irq(cluster, cpu, true);
	return 0;
}

static int tc2_pm_cluster_powerup(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= TC2_CLUSTERS)
		return -EINVAL;
	ve_spc_powerdown(cluster, false);
	return 0;
}

static void tc2_pm_cpu_powerdown_prepare(unsigned int cpu, unsigned int cluster)
{
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS || cpu >= TC2_MAX_CPUS_PER_CLUSTER);
	ve_spc_cpu_wakeup_irq(cluster, cpu, true);
	/*
	 * If the CPU is committed to power down, make sure
	 * the power controller will be in charge of waking it
	 * up upon IRQ, ie IRQ lines are cut from GIC CPU IF
	 * to the CPU by disabling the GIC CPU IF to prevent wfi
	 * from completing execution behind power controller back
	 */
	gic_cpu_if_down(0);
}

static void tc2_pm_cluster_powerdown_prepare(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS);
	ve_spc_powerdown(cluster, true);
	ve_spc_global_wakeup_irq(true);
}

static void tc2_pm_cpu_cache_disable(void)
{
	v7_exit_coherency_flush(louis);
}

static void tc2_pm_cluster_cache_disable(void)
{
	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A15) {
		/*
		 * On the Cortex-A15 we need to disable
		 * L2 prefetching before flushing the cache.
		 */
		asm volatile(
		"mcr	p15, 1, %0, c15, c0, 3 \n\t"
		"isb	\n\t"
		"dsb	"
		: : "r" (0x400) );
	}

	v7_exit_coherency_flush(all);
	cci_disable_port_by_cpu(read_cpuid_mpidr());
}

static int tc2_core_in_reset(unsigned int cpu, unsigned int cluster)
{
	u32 mask = cluster ?
		  RESET_A7_NCORERESET(cpu)
		: RESET_A15_NCORERESET(cpu);

	return !(readl_relaxed(scc + RESET_CTRL) & mask);
}

#define POLL_MSEC 10
#define TIMEOUT_MSEC 1000

static int tc2_pm_wait_for_powerdown(unsigned int cpu, unsigned int cluster)
{
	unsigned tries;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS || cpu >= TC2_MAX_CPUS_PER_CLUSTER);

	for (tries = 0; tries < TIMEOUT_MSEC / POLL_MSEC; ++tries) {
		pr_debug("%s(cpu=%u, cluster=%u): RESET_CTRL = 0x%08X\n",
			 __func__, cpu, cluster,
			 readl_relaxed(scc + RESET_CTRL));

		/*
		 * We need the CPU to reach WFI, but the power
		 * controller may put the cluster in reset and
		 * power it off as soon as that happens, before
		 * we have a chance to see STANDBYWFI.
		 *
		 * So we need to check for both conditions:
		 */
		if (tc2_core_in_reset(cpu, cluster) ||
		    ve_spc_cpu_in_wfi(cpu, cluster))
			return 0; /* success: the CPU is halted */

		/* Otherwise, wait and retry: */
		msleep(POLL_MSEC);
	}

	return -ETIMEDOUT; /* timeout */
}

static void tc2_pm_cpu_suspend_prepare(unsigned int cpu, unsigned int cluster)
{
	ve_spc_set_resume_addr(cluster, cpu, __pa_symbol(mcpm_entry_point));
}

static void tc2_pm_cpu_is_up(unsigned int cpu, unsigned int cluster)
{
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS || cpu >= TC2_MAX_CPUS_PER_CLUSTER);
	ve_spc_cpu_wakeup_irq(cluster, cpu, false);
	ve_spc_set_resume_addr(cluster, cpu, 0);
}

static void tc2_pm_cluster_is_up(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS);
	ve_spc_powerdown(cluster, false);
	ve_spc_global_wakeup_irq(false);
}

static const struct mcpm_platform_ops tc2_pm_power_ops = {
	.cpu_powerup		= tc2_pm_cpu_powerup,
	.cluster_powerup	= tc2_pm_cluster_powerup,
	.cpu_suspend_prepare	= tc2_pm_cpu_suspend_prepare,
	.cpu_powerdown_prepare	= tc2_pm_cpu_powerdown_prepare,
	.cluster_powerdown_prepare = tc2_pm_cluster_powerdown_prepare,
	.cpu_cache_disable	= tc2_pm_cpu_cache_disable,
	.cluster_cache_disable	= tc2_pm_cluster_cache_disable,
	.wait_for_powerdown	= tc2_pm_wait_for_powerdown,
	.cpu_is_up		= tc2_pm_cpu_is_up,
	.cluster_is_up		= tc2_pm_cluster_is_up,
};

/*
 * Enable cluster-level coherency, in preparation for turning on the MMU.
 */
static void __naked tc2_pm_power_up_setup(unsigned int affinity_level)
{
	asm volatile (" \n"
"	cmp	r0, #1 \n"
"	bxne	lr \n"
"	b	cci_enable_port_for_self ");
}

static int __init tc2_pm_init(void)
{
	unsigned int mpidr, cpu, cluster;
	int ret, irq;
	u32 a15_cluster_id, a7_cluster_id, sys_info;
	struct device_node *np;

	/*
	 * The power management-related features are hidden behind
	 * SCC registers. We need to extract runtime information like
	 * cluster ids and number of CPUs really available in clusters.
	 */
	np = of_find_compatible_node(NULL, NULL,
			"arm,vexpress-scc,v2p-ca15_a7");
	scc = of_iomap(np, 0);
	if (!scc)
		return -ENODEV;

	a15_cluster_id = readl_relaxed(scc + A15_CONF) & 0xf;
	a7_cluster_id = readl_relaxed(scc + A7_CONF) & 0xf;
	if (a15_cluster_id >= TC2_CLUSTERS || a7_cluster_id >= TC2_CLUSTERS)
		return -EINVAL;

	sys_info = readl_relaxed(scc + SYS_INFO);
	tc2_nr_cpus[a15_cluster_id] = (sys_info >> 16) & 0xf;
	tc2_nr_cpus[a7_cluster_id] = (sys_info >> 20) & 0xf;

	irq = irq_of_parse_and_map(np, 0);

	/*
	 * A subset of the SCC registers is also used to communicate
	 * with the SPC (power controller). We need to be able to
	 * drive it very early in the boot process to power up
	 * processors, so we initialize the SPC driver here.
	 */
	ret = ve_spc_init(scc + SPC_BASE, a15_cluster_id, irq);
	if (ret)
		return ret;

	if (!cci_probed())
		return -ENODEV;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= TC2_CLUSTERS || cpu >= tc2_nr_cpus[cluster]) {
		pr_err("%s: boot CPU is out of bound!\n", __func__);
		return -EINVAL;
	}

	ret = mcpm_platform_register(&tc2_pm_power_ops);
	if (!ret) {
		mcpm_sync_init(tc2_pm_power_up_setup);
		/* test if we can (re)enable the CCI on our own */
		BUG_ON(mcpm_loopback(tc2_pm_cluster_cache_disable) != 0);
		pr_info("TC2 power management initialized\n");
	}
	return ret;
}

early_initcall(tc2_pm_init);
