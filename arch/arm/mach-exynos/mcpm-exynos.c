/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * arch/arm/mach-exynos/mcpm-exynos.c
 *
 * Based on arch/arm/mach-vexpress/dcscb.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/arm-cci.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/mcpm.h>
#include <asm/smp_plat.h>

#include "common.h"

#define EXYNOS5420_CPUS_PER_CLUSTER	4
#define EXYNOS5420_NR_CLUSTERS		2

#define EXYNOS5420_ENABLE_AUTOMATIC_CORE_DOWN	BIT(9)
#define EXYNOS5420_USE_ARM_CORE_DOWN_STATE	BIT(29)
#define EXYNOS5420_USE_L2_COMMON_UP_STATE	BIT(30)

static void __iomem *ns_sram_base_addr;

/*
 * The common v7_exit_coherency_flush API could not be used because of the
 * Erratum 799270 workaround. This macro is the same as the common one (in
 * arch/arm/include/asm/cacheflush.h) except for the erratum handling.
 */
#define exynos_v7_exit_coherency_flush(level) \
	asm volatile( \
	"stmfd	sp!, {fp, ip}\n\t"\
	"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR\n\t" \
	"bic	r0, r0, #"__stringify(CR_C)"\n\t" \
	"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR\n\t" \
	"isb\n\t"\
	"bl	v7_flush_dcache_"__stringify(level)"\n\t" \
	"mrc	p15, 0, r0, c1, c0, 1	@ get ACTLR\n\t" \
	"bic	r0, r0, #(1 << 6)	@ disable local coherency\n\t" \
	/* Dummy Load of a device register to avoid Erratum 799270 */ \
	"ldr	r4, [%0]\n\t" \
	"and	r4, r4, #0\n\t" \
	"orr	r0, r0, r4\n\t" \
	"mcr	p15, 0, r0, c1, c0, 1	@ set ACTLR\n\t" \
	"isb\n\t" \
	"dsb\n\t" \
	"ldmfd	sp!, {fp, ip}" \
	: \
	: "Ir" (pmu_base_addr + S5P_INFORM0) \
	: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", \
	  "r9", "r10", "lr", "memory")

static int exynos_cpu_powerup(unsigned int cpu, unsigned int cluster)
{
	unsigned int cpunr = cpu + (cluster * EXYNOS5420_CPUS_PER_CLUSTER);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cpu >= EXYNOS5420_CPUS_PER_CLUSTER ||
		cluster >= EXYNOS5420_NR_CLUSTERS)
		return -EINVAL;

	if (!exynos_cpu_power_state(cpunr)) {
		exynos_cpu_power_up(cpunr);

		/*
		 * This assumes the cluster number of the big cores(Cortex A15)
		 * is 0 and the Little cores(Cortex A7) is 1.
		 * When the system was booted from the Little core,
		 * they should be reset during power up cpu.
		 */
		if (cluster &&
		    cluster == MPIDR_AFFINITY_LEVEL(cpu_logical_map(0), 1)) {
			/*
			 * Before we reset the Little cores, we should wait
			 * the SPARE2 register is set to 1 because the init
			 * codes of the iROM will set the register after
			 * initialization.
			 */
			while (!pmu_raw_readl(S5P_PMU_SPARE2))
				udelay(10);

			pmu_raw_writel(EXYNOS5420_KFC_CORE_RESET(cpu),
					EXYNOS_SWRESET);
		}
	}

	return 0;
}

static int exynos_cluster_powerup(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= EXYNOS5420_NR_CLUSTERS)
		return -EINVAL;

	exynos_cluster_power_up(cluster);
	return 0;
}

static void exynos_cpu_powerdown_prepare(unsigned int cpu, unsigned int cluster)
{
	unsigned int cpunr = cpu + (cluster * EXYNOS5420_CPUS_PER_CLUSTER);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cpu >= EXYNOS5420_CPUS_PER_CLUSTER ||
			cluster >= EXYNOS5420_NR_CLUSTERS);
	exynos_cpu_power_down(cpunr);
}

static void exynos_cluster_powerdown_prepare(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	BUG_ON(cluster >= EXYNOS5420_NR_CLUSTERS);
	exynos_cluster_power_down(cluster);
}

static void exynos_cpu_cache_disable(void)
{
	/* Disable and flush the local CPU cache. */
	exynos_v7_exit_coherency_flush(louis);
}

static void exynos_cluster_cache_disable(void)
{
	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A15) {
		/*
		 * On the Cortex-A15 we need to disable
		 * L2 prefetching before flushing the cache.
		 */
		asm volatile(
		"mcr	p15, 1, %0, c15, c0, 3\n\t"
		"isb\n\t"
		"dsb"
		: : "r" (0x400));
	}

	/* Flush all cache levels for this cluster. */
	exynos_v7_exit_coherency_flush(all);

	/*
	 * Disable cluster-level coherency by masking
	 * incoming snoops and DVM messages:
	 */
	cci_disable_port_by_cpu(read_cpuid_mpidr());
}

static int exynos_wait_for_powerdown(unsigned int cpu, unsigned int cluster)
{
	unsigned int tries = 100;
	unsigned int cpunr = cpu + (cluster * EXYNOS5420_CPUS_PER_CLUSTER);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cpu >= EXYNOS5420_CPUS_PER_CLUSTER ||
			cluster >= EXYNOS5420_NR_CLUSTERS);

	/* Wait for the core state to be OFF */
	while (tries--) {
		if ((exynos_cpu_power_state(cpunr) == 0))
			return 0; /* success: the CPU is halted */

		/* Otherwise, wait and retry: */
		msleep(1);
	}

	return -ETIMEDOUT; /* timeout */
}

static void exynos_cpu_is_up(unsigned int cpu, unsigned int cluster)
{
	/* especially when resuming: make sure power control is set */
	exynos_cpu_powerup(cpu, cluster);
}

static const struct mcpm_platform_ops exynos_power_ops = {
	.cpu_powerup		= exynos_cpu_powerup,
	.cluster_powerup	= exynos_cluster_powerup,
	.cpu_powerdown_prepare	= exynos_cpu_powerdown_prepare,
	.cluster_powerdown_prepare = exynos_cluster_powerdown_prepare,
	.cpu_cache_disable	= exynos_cpu_cache_disable,
	.cluster_cache_disable	= exynos_cluster_cache_disable,
	.wait_for_powerdown	= exynos_wait_for_powerdown,
	.cpu_is_up		= exynos_cpu_is_up,
};

/*
 * Enable cluster-level coherency, in preparation for turning on the MMU.
 */
static void __naked exynos_pm_power_up_setup(unsigned int affinity_level)
{
	asm volatile ("\n"
	"cmp	r0, #1\n"
	"bxne	lr\n"
	"b	cci_enable_port_for_self");
}

static const struct of_device_id exynos_dt_mcpm_match[] = {
	{ .compatible = "samsung,exynos5420" },
	{ .compatible = "samsung,exynos5800" },
	{},
};

static void exynos_mcpm_setup_entry_point(void)
{
	/*
	 * U-Boot SPL is hardcoded to jump to the start of ns_sram_base_addr
	 * as part of secondary_cpu_start().  Let's redirect it to the
	 * mcpm_entry_point(). This is done during both secondary boot-up as
	 * well as system resume.
	 */
	__raw_writel(0xe59f0000, ns_sram_base_addr);     /* ldr r0, [pc, #0] */
	__raw_writel(0xe12fff10, ns_sram_base_addr + 4); /* bx  r0 */
	__raw_writel(virt_to_phys(mcpm_entry_point), ns_sram_base_addr + 8);
}

static struct syscore_ops exynos_mcpm_syscore_ops = {
	.resume	= exynos_mcpm_setup_entry_point,
};

static int __init exynos_mcpm_init(void)
{
	struct device_node *node;
	unsigned int value, i;
	int ret;

	node = of_find_matching_node(NULL, exynos_dt_mcpm_match);
	if (!node)
		return -ENODEV;
	of_node_put(node);

	if (!cci_probed())
		return -ENODEV;

	node = of_find_compatible_node(NULL, NULL,
			"samsung,exynos4210-sysram-ns");
	if (!node)
		return -ENODEV;

	ns_sram_base_addr = of_iomap(node, 0);
	of_node_put(node);
	if (!ns_sram_base_addr) {
		pr_err("failed to map non-secure iRAM base address\n");
		return -ENOMEM;
	}

	/*
	 * To increase the stability of KFC reset we need to program
	 * the PMU SPARE3 register
	 */
	pmu_raw_writel(EXYNOS5420_SWRESET_KFC_SEL, S5P_PMU_SPARE3);

	ret = mcpm_platform_register(&exynos_power_ops);
	if (!ret)
		ret = mcpm_sync_init(exynos_pm_power_up_setup);
	if (!ret)
		ret = mcpm_loopback(exynos_cluster_cache_disable); /* turn on the CCI */
	if (ret) {
		iounmap(ns_sram_base_addr);
		return ret;
	}

	mcpm_smp_set_ops();

	pr_info("Exynos MCPM support installed\n");

	/*
	 * On Exynos5420/5800 for the A15 and A7 clusters:
	 *
	 * EXYNOS5420_ENABLE_AUTOMATIC_CORE_DOWN ensures that all the cores
	 * in a cluster are turned off before turning off the cluster L2.
	 *
	 * EXYNOS5420_USE_ARM_CORE_DOWN_STATE ensures that a cores is powered
	 * off before waking it up.
	 *
	 * EXYNOS5420_USE_L2_COMMON_UP_STATE ensures that cluster L2 will be
	 * turned on before the first man is powered up.
	 */
	for (i = 0; i < EXYNOS5420_NR_CLUSTERS; i++) {
		value = pmu_raw_readl(EXYNOS_COMMON_OPTION(i));
		value |= EXYNOS5420_ENABLE_AUTOMATIC_CORE_DOWN |
			 EXYNOS5420_USE_ARM_CORE_DOWN_STATE    |
			 EXYNOS5420_USE_L2_COMMON_UP_STATE;
		pmu_raw_writel(value, EXYNOS_COMMON_OPTION(i));
	}

	exynos_mcpm_setup_entry_point();

	register_syscore_ops(&exynos_mcpm_syscore_ops);

	return ret;
}

early_initcall(exynos_mcpm_init);
