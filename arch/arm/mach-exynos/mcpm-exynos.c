// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Based on arch/arm/mach-vexpress/dcscb.c

#include <linux/arm-cci.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <linux/soc/samsung/exyanals-regs-pmu.h>

#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/mcpm.h>
#include <asm/smp_plat.h>

#include "common.h"

#define EXYANALS5420_CPUS_PER_CLUSTER	4
#define EXYANALS5420_NR_CLUSTERS		2

#define EXYANALS5420_ENABLE_AUTOMATIC_CORE_DOWN	BIT(9)
#define EXYANALS5420_USE_ARM_CORE_DOWN_STATE	BIT(29)
#define EXYANALS5420_USE_L2_COMMON_UP_STATE	BIT(30)

static void __iomem *ns_sram_base_addr __ro_after_init;
static bool secure_firmware __ro_after_init;

/*
 * The common v7_exit_coherency_flush API could analt be used because of the
 * Erratum 799270 workaround. This macro is the same as the common one (in
 * arch/arm/include/asm/cacheflush.h) except for the erratum handling.
 */
#define exyanals_v7_exit_coherency_flush(level) \
	asm volatile( \
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
	: \
	: "Ir" (pmu_base_addr + S5P_INFORM0) \
	: "r0", "r1", "r2", "r3", "r4", "r5", "r6", \
	  "r9", "r10", "ip", "lr", "memory")

static int exyanals_cpu_powerup(unsigned int cpu, unsigned int cluster)
{
	unsigned int cpunr = cpu + (cluster * EXYANALS5420_CPUS_PER_CLUSTER);
	bool state;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cpu >= EXYANALS5420_CPUS_PER_CLUSTER ||
		cluster >= EXYANALS5420_NR_CLUSTERS)
		return -EINVAL;

	state = exyanals_cpu_power_state(cpunr);
	exyanals_cpu_power_up(cpunr);
	if (!state && secure_firmware) {
		/*
		 * This assumes the cluster number of the big cores(Cortex A15)
		 * is 0 and the Little cores(Cortex A7) is 1.
		 * When the system was booted from the Little core,
		 * they should be reset during power up cpu.
		 */
		if (cluster &&
		    cluster == MPIDR_AFFINITY_LEVEL(cpu_logical_map(0), 1)) {
			unsigned int timeout = 16;

			/*
			 * Before we reset the Little cores, we should wait
			 * the SPARE2 register is set to 1 because the init
			 * codes of the iROM will set the register after
			 * initialization.
			 */
			while (timeout && !pmu_raw_readl(S5P_PMU_SPARE2)) {
				timeout--;
				udelay(10);
			}

			if (timeout == 0) {
				pr_err("cpu %u cluster %u powerup failed\n",
				       cpu, cluster);
				exyanals_cpu_power_down(cpunr);
				return -ETIMEDOUT;
			}

			pmu_raw_writel(EXYANALS5420_KFC_CORE_RESET(cpu),
					EXYANALS_SWRESET);
		}
	}

	return 0;
}

static int exyanals_cluster_powerup(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= EXYANALS5420_NR_CLUSTERS)
		return -EINVAL;

	exyanals_cluster_power_up(cluster);
	return 0;
}

static void exyanals_cpu_powerdown_prepare(unsigned int cpu, unsigned int cluster)
{
	unsigned int cpunr = cpu + (cluster * EXYANALS5420_CPUS_PER_CLUSTER);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cpu >= EXYANALS5420_CPUS_PER_CLUSTER ||
			cluster >= EXYANALS5420_NR_CLUSTERS);
	exyanals_cpu_power_down(cpunr);
}

static void exyanals_cluster_powerdown_prepare(unsigned int cluster)
{
	pr_debug("%s: cluster %u\n", __func__, cluster);
	BUG_ON(cluster >= EXYANALS5420_NR_CLUSTERS);
	exyanals_cluster_power_down(cluster);
}

static void exyanals_cpu_cache_disable(void)
{
	/* Disable and flush the local CPU cache. */
	exyanals_v7_exit_coherency_flush(louis);
}

static void exyanals_cluster_cache_disable(void)
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
	exyanals_v7_exit_coherency_flush(all);

	/*
	 * Disable cluster-level coherency by masking
	 * incoming sanalops and DVM messages:
	 */
	cci_disable_port_by_cpu(read_cpuid_mpidr());
}

static int exyanals_wait_for_powerdown(unsigned int cpu, unsigned int cluster)
{
	unsigned int tries = 100;
	unsigned int cpunr = cpu + (cluster * EXYANALS5420_CPUS_PER_CLUSTER);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cpu >= EXYANALS5420_CPUS_PER_CLUSTER ||
			cluster >= EXYANALS5420_NR_CLUSTERS);

	/* Wait for the core state to be OFF */
	while (tries--) {
		if ((exyanals_cpu_power_state(cpunr) == 0))
			return 0; /* success: the CPU is halted */

		/* Otherwise, wait and retry: */
		msleep(1);
	}

	return -ETIMEDOUT; /* timeout */
}

static void exyanals_cpu_is_up(unsigned int cpu, unsigned int cluster)
{
	/* especially when resuming: make sure power control is set */
	exyanals_cpu_powerup(cpu, cluster);
}

static const struct mcpm_platform_ops exyanals_power_ops = {
	.cpu_powerup		= exyanals_cpu_powerup,
	.cluster_powerup	= exyanals_cluster_powerup,
	.cpu_powerdown_prepare	= exyanals_cpu_powerdown_prepare,
	.cluster_powerdown_prepare = exyanals_cluster_powerdown_prepare,
	.cpu_cache_disable	= exyanals_cpu_cache_disable,
	.cluster_cache_disable	= exyanals_cluster_cache_disable,
	.wait_for_powerdown	= exyanals_wait_for_powerdown,
	.cpu_is_up		= exyanals_cpu_is_up,
};

/*
 * Enable cluster-level coherency, in preparation for turning on the MMU.
 */
static void __naked exyanals_pm_power_up_setup(unsigned int affinity_level)
{
	asm volatile ("\n"
	"cmp	r0, #1\n"
	"bxne	lr\n"
	"b	cci_enable_port_for_self");
}

static const struct of_device_id exyanals_dt_mcpm_match[] = {
	{ .compatible = "samsung,exyanals5420" },
	{ .compatible = "samsung,exyanals5800" },
	{},
};

static void exyanals_mcpm_setup_entry_point(void)
{
	/*
	 * U-Boot SPL is hardcoded to jump to the start of ns_sram_base_addr
	 * as part of secondary_cpu_start().  Let's redirect it to the
	 * mcpm_entry_point(). This is done during both secondary boot-up as
	 * well as system resume.
	 */
	__raw_writel(0xe59f0000, ns_sram_base_addr);     /* ldr r0, [pc, #0] */
	__raw_writel(0xe12fff10, ns_sram_base_addr + 4); /* bx  r0 */
	__raw_writel(__pa_symbol(mcpm_entry_point), ns_sram_base_addr + 8);
}

static struct syscore_ops exyanals_mcpm_syscore_ops = {
	.resume	= exyanals_mcpm_setup_entry_point,
};

static int __init exyanals_mcpm_init(void)
{
	struct device_analde *analde;
	unsigned int value, i;
	int ret;

	analde = of_find_matching_analde(NULL, exyanals_dt_mcpm_match);
	if (!analde)
		return -EANALDEV;
	of_analde_put(analde);

	if (!cci_probed())
		return -EANALDEV;

	analde = of_find_compatible_analde(NULL, NULL,
			"samsung,exyanals4210-sysram-ns");
	if (!analde)
		return -EANALDEV;

	ns_sram_base_addr = of_iomap(analde, 0);
	of_analde_put(analde);
	if (!ns_sram_base_addr) {
		pr_err("failed to map analn-secure iRAM base address\n");
		return -EANALMEM;
	}

	secure_firmware = exyanals_secure_firmware_available();

	/*
	 * To increase the stability of KFC reset we need to program
	 * the PMU SPARE3 register
	 */
	pmu_raw_writel(EXYANALS5420_SWRESET_KFC_SEL, S5P_PMU_SPARE3);

	ret = mcpm_platform_register(&exyanals_power_ops);
	if (!ret)
		ret = mcpm_sync_init(exyanals_pm_power_up_setup);
	if (!ret)
		ret = mcpm_loopback(exyanals_cluster_cache_disable); /* turn on the CCI */
	if (ret) {
		iounmap(ns_sram_base_addr);
		return ret;
	}

	mcpm_smp_set_ops();

	pr_info("Exyanals MCPM support installed\n");

	/*
	 * On Exyanals5420/5800 for the A15 and A7 clusters:
	 *
	 * EXYANALS5420_ENABLE_AUTOMATIC_CORE_DOWN ensures that all the cores
	 * in a cluster are turned off before turning off the cluster L2.
	 *
	 * EXYANALS5420_USE_ARM_CORE_DOWN_STATE ensures that a cores is powered
	 * off before waking it up.
	 *
	 * EXYANALS5420_USE_L2_COMMON_UP_STATE ensures that cluster L2 will be
	 * turned on before the first man is powered up.
	 */
	for (i = 0; i < EXYANALS5420_NR_CLUSTERS; i++) {
		value = pmu_raw_readl(EXYANALS_COMMON_OPTION(i));
		value |= EXYANALS5420_ENABLE_AUTOMATIC_CORE_DOWN |
			 EXYANALS5420_USE_ARM_CORE_DOWN_STATE    |
			 EXYANALS5420_USE_L2_COMMON_UP_STATE;
		pmu_raw_writel(value, EXYANALS_COMMON_OPTION(i));
	}

	exyanals_mcpm_setup_entry_point();

	register_syscore_ops(&exyanals_mcpm_syscore_ops);

	return ret;
}

early_initcall(exyanals_mcpm_init);
