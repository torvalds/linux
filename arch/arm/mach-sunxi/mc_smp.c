// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 *
 * arch/arm/mach-sunxi/mc_smp.c
 *
 * Based on Allwinner code, arch/arm/mach-exynos/mcpm-exynos.c, and
 * arch/arm/mach-hisi/platmcpm.c
 * Cluster cache enable trampoline code adapted from MCPM framework
 */

#include <linux/arm-cci.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

#define SUNXI_CPUS_PER_CLUSTER		4
#define SUNXI_NR_CLUSTERS		2

#define POLL_USEC	100
#define TIMEOUT_USEC	100000

#define CPUCFG_CX_CTRL_REG0(c)		(0x10 * (c))
#define CPUCFG_CX_CTRL_REG0_L1_RST_DISABLE(n)	BIT(n)
#define CPUCFG_CX_CTRL_REG0_L1_RST_DISABLE_ALL	0xf
#define CPUCFG_CX_CTRL_REG0_L2_RST_DISABLE_A7	BIT(4)
#define CPUCFG_CX_CTRL_REG0_L2_RST_DISABLE_A15	BIT(0)
#define CPUCFG_CX_CTRL_REG1(c)		(0x10 * (c) + 0x4)
#define CPUCFG_CX_CTRL_REG1_ACINACTM	BIT(0)
#define CPUCFG_CX_STATUS(c)		(0x30 + 0x4 * (c))
#define CPUCFG_CX_STATUS_STANDBYWFI(n)	BIT(16 + (n))
#define CPUCFG_CX_STATUS_STANDBYWFIL2	BIT(0)
#define CPUCFG_CX_RST_CTRL(c)		(0x80 + 0x4 * (c))
#define CPUCFG_CX_RST_CTRL_DBG_SOC_RST	BIT(24)
#define CPUCFG_CX_RST_CTRL_ETM_RST(n)	BIT(20 + (n))
#define CPUCFG_CX_RST_CTRL_ETM_RST_ALL	(0xf << 20)
#define CPUCFG_CX_RST_CTRL_DBG_RST(n)	BIT(16 + (n))
#define CPUCFG_CX_RST_CTRL_DBG_RST_ALL	(0xf << 16)
#define CPUCFG_CX_RST_CTRL_H_RST	BIT(12)
#define CPUCFG_CX_RST_CTRL_L2_RST	BIT(8)
#define CPUCFG_CX_RST_CTRL_CX_RST(n)	BIT(4 + (n))
#define CPUCFG_CX_RST_CTRL_CORE_RST(n)	BIT(n)
#define CPUCFG_CX_RST_CTRL_CORE_RST_ALL	(0xf << 0)

#define PRCM_CPU_PO_RST_CTRL(c)		(0x4 + 0x4 * (c))
#define PRCM_CPU_PO_RST_CTRL_CORE(n)	BIT(n)
#define PRCM_CPU_PO_RST_CTRL_CORE_ALL	0xf
#define PRCM_PWROFF_GATING_REG(c)	(0x100 + 0x4 * (c))
/* The power off register for clusters are different from a80 and a83t */
#define PRCM_PWROFF_GATING_REG_CLUSTER_SUN8I	BIT(0)
#define PRCM_PWROFF_GATING_REG_CLUSTER_SUN9I	BIT(4)
#define PRCM_PWROFF_GATING_REG_CORE(n)	BIT(n)
#define PRCM_PWR_SWITCH_REG(c, cpu)	(0x140 + 0x10 * (c) + 0x4 * (cpu))
#define PRCM_CPU_SOFT_ENTRY_REG		0x164

/* R_CPUCFG registers, specific to sun8i-a83t */
#define R_CPUCFG_CLUSTER_PO_RST_CTRL(c)	(0x30 + (c) * 0x4)
#define R_CPUCFG_CLUSTER_PO_RST_CTRL_CORE(n)	BIT(n)
#define R_CPUCFG_CPU_SOFT_ENTRY_REG		0x01a4

#define CPU0_SUPPORT_HOTPLUG_MAGIC0	0xFA50392F
#define CPU0_SUPPORT_HOTPLUG_MAGIC1	0x790DCA3A

static void __iomem *cpucfg_base;
static void __iomem *prcm_base;
static void __iomem *sram_b_smp_base;
static void __iomem *r_cpucfg_base;

extern void sunxi_mc_smp_secondary_startup(void);
extern void sunxi_mc_smp_resume(void);
static bool is_a83t;

static bool sunxi_core_is_cortex_a15(unsigned int core, unsigned int cluster)
{
	struct device_node *node;
	int cpu = cluster * SUNXI_CPUS_PER_CLUSTER + core;
	bool is_compatible;

	node = of_cpu_device_node_get(cpu);

	/* In case of_cpu_device_node_get fails */
	if (!node)
		node = of_get_cpu_node(cpu, NULL);

	if (!node) {
		/*
		 * There's no point in returning an error, since we
		 * would be mid way in a core or cluster power sequence.
		 */
		pr_err("%s: Couldn't get CPU cluster %u core %u device node\n",
		       __func__, cluster, core);

		return false;
	}

	is_compatible = of_device_is_compatible(node, "arm,cortex-a15");
	of_node_put(node);
	return is_compatible;
}

static int sunxi_cpu_power_switch_set(unsigned int cpu, unsigned int cluster,
				      bool enable)
{
	u32 reg;

	/* control sequence from Allwinner A80 user manual v1.2 PRCM section */
	reg = readl(prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
	if (enable) {
		if (reg == 0x00) {
			pr_debug("power clamp for cluster %u cpu %u already open\n",
				 cluster, cpu);
			return 0;
		}

		writel(0xff, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
		writel(0xfe, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
		writel(0xf8, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
		writel(0xf0, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
		writel(0x00, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
	} else {
		writel(0xff, prcm_base + PRCM_PWR_SWITCH_REG(cluster, cpu));
		udelay(10);
	}

	return 0;
}

static void sunxi_cpu0_hotplug_support_set(bool enable)
{
	if (enable) {
		writel(CPU0_SUPPORT_HOTPLUG_MAGIC0, sram_b_smp_base);
		writel(CPU0_SUPPORT_HOTPLUG_MAGIC1, sram_b_smp_base + 0x4);
	} else {
		writel(0x0, sram_b_smp_base);
		writel(0x0, sram_b_smp_base + 0x4);
	}
}

static int sunxi_cpu_powerup(unsigned int cpu, unsigned int cluster)
{
	u32 reg;

	pr_debug("%s: cluster %u cpu %u\n", __func__, cluster, cpu);
	if (cpu >= SUNXI_CPUS_PER_CLUSTER || cluster >= SUNXI_NR_CLUSTERS)
		return -EINVAL;

	/* Set hotplug support magic flags for cpu0 */
	if (cluster == 0 && cpu == 0)
		sunxi_cpu0_hotplug_support_set(true);

	/* assert processor power-on reset */
	reg = readl(prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));
	reg &= ~PRCM_CPU_PO_RST_CTRL_CORE(cpu);
	writel(reg, prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));

	if (is_a83t) {
		/* assert cpu power-on reset */
		reg  = readl(r_cpucfg_base +
			     R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		reg &= ~(R_CPUCFG_CLUSTER_PO_RST_CTRL_CORE(cpu));
		writel(reg, r_cpucfg_base +
		       R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		udelay(10);
	}

	/* Cortex-A7: hold L1 reset disable signal low */
	if (!sunxi_core_is_cortex_a15(cpu, cluster)) {
		reg = readl(cpucfg_base + CPUCFG_CX_CTRL_REG0(cluster));
		reg &= ~CPUCFG_CX_CTRL_REG0_L1_RST_DISABLE(cpu);
		writel(reg, cpucfg_base + CPUCFG_CX_CTRL_REG0(cluster));
	}

	/* assert processor related resets */
	reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
	reg &= ~CPUCFG_CX_RST_CTRL_DBG_RST(cpu);

	/*
	 * Allwinner code also asserts resets for NEON on A15. According
	 * to ARM manuals, asserting power-on reset is sufficient.
	 */
	if (!sunxi_core_is_cortex_a15(cpu, cluster))
		reg &= ~CPUCFG_CX_RST_CTRL_ETM_RST(cpu);

	writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));

	/* open power switch */
	sunxi_cpu_power_switch_set(cpu, cluster, true);

	/* Handle A83T bit swap */
	if (is_a83t) {
		if (cpu == 0)
			cpu = 4;
	}

	/* clear processor power gate */
	reg = readl(prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	reg &= ~PRCM_PWROFF_GATING_REG_CORE(cpu);
	writel(reg, prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	udelay(20);

	/* Handle A83T bit swap */
	if (is_a83t) {
		if (cpu == 4)
			cpu = 0;
	}

	/* de-assert processor power-on reset */
	reg = readl(prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));
	reg |= PRCM_CPU_PO_RST_CTRL_CORE(cpu);
	writel(reg, prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));

	if (is_a83t) {
		reg  = readl(r_cpucfg_base +
			     R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		reg |= R_CPUCFG_CLUSTER_PO_RST_CTRL_CORE(cpu);
		writel(reg, r_cpucfg_base +
		       R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		udelay(10);
	}

	/* de-assert all processor resets */
	reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
	reg |= CPUCFG_CX_RST_CTRL_DBG_RST(cpu);
	reg |= CPUCFG_CX_RST_CTRL_CORE_RST(cpu);
	if (!sunxi_core_is_cortex_a15(cpu, cluster))
		reg |= CPUCFG_CX_RST_CTRL_ETM_RST(cpu);
	else
		reg |= CPUCFG_CX_RST_CTRL_CX_RST(cpu); /* NEON */
	writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));

	return 0;
}

static int sunxi_cluster_powerup(unsigned int cluster)
{
	u32 reg;

	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= SUNXI_NR_CLUSTERS)
		return -EINVAL;

	/* For A83T, assert cluster cores resets */
	if (is_a83t) {
		reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
		reg &= ~CPUCFG_CX_RST_CTRL_CORE_RST_ALL;   /* Core Reset    */
		writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
		udelay(10);
	}

	/* assert ACINACTM */
	reg = readl(cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));
	reg |= CPUCFG_CX_CTRL_REG1_ACINACTM;
	writel(reg, cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));

	/* assert cluster processor power-on resets */
	reg = readl(prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));
	reg &= ~PRCM_CPU_PO_RST_CTRL_CORE_ALL;
	writel(reg, prcm_base + PRCM_CPU_PO_RST_CTRL(cluster));

	/* assert cluster cores resets */
	if (is_a83t) {
		reg  = readl(r_cpucfg_base +
			     R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		reg &= ~CPUCFG_CX_RST_CTRL_CORE_RST_ALL;
		writel(reg, r_cpucfg_base +
		       R_CPUCFG_CLUSTER_PO_RST_CTRL(cluster));
		udelay(10);
	}

	/* assert cluster resets */
	reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
	reg &= ~CPUCFG_CX_RST_CTRL_DBG_SOC_RST;
	reg &= ~CPUCFG_CX_RST_CTRL_DBG_RST_ALL;
	reg &= ~CPUCFG_CX_RST_CTRL_H_RST;
	reg &= ~CPUCFG_CX_RST_CTRL_L2_RST;

	/*
	 * Allwinner code also asserts resets for NEON on A15. According
	 * to ARM manuals, asserting power-on reset is sufficient.
	 */
	if (!sunxi_core_is_cortex_a15(0, cluster))
		reg &= ~CPUCFG_CX_RST_CTRL_ETM_RST_ALL;

	writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));

	/* hold L1/L2 reset disable signals low */
	reg = readl(cpucfg_base + CPUCFG_CX_CTRL_REG0(cluster));
	if (sunxi_core_is_cortex_a15(0, cluster)) {
		/* Cortex-A15: hold L2RSTDISABLE low */
		reg &= ~CPUCFG_CX_CTRL_REG0_L2_RST_DISABLE_A15;
	} else {
		/* Cortex-A7: hold L1RSTDISABLE and L2RSTDISABLE low */
		reg &= ~CPUCFG_CX_CTRL_REG0_L1_RST_DISABLE_ALL;
		reg &= ~CPUCFG_CX_CTRL_REG0_L2_RST_DISABLE_A7;
	}
	writel(reg, cpucfg_base + CPUCFG_CX_CTRL_REG0(cluster));

	/* clear cluster power gate */
	reg = readl(prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	if (is_a83t)
		reg &= ~PRCM_PWROFF_GATING_REG_CLUSTER_SUN8I;
	else
		reg &= ~PRCM_PWROFF_GATING_REG_CLUSTER_SUN9I;
	writel(reg, prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	udelay(20);

	/* de-assert cluster resets */
	reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
	reg |= CPUCFG_CX_RST_CTRL_DBG_SOC_RST;
	reg |= CPUCFG_CX_RST_CTRL_H_RST;
	reg |= CPUCFG_CX_RST_CTRL_L2_RST;
	writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));

	/* de-assert ACINACTM */
	reg = readl(cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));
	reg &= ~CPUCFG_CX_CTRL_REG1_ACINACTM;
	writel(reg, cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));

	return 0;
}

/*
 * This bit is shared between the initial nocache_trampoline call to
 * enable CCI-400 and proper cluster cache disable before power down.
 */
static void sunxi_cluster_cache_disable_without_axi(void)
{
	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A15) {
		/*
		 * On the Cortex-A15 we need to disable
		 * L2 prefetching before flushing the cache.
		 */
		asm volatile(
		"mcr	p15, 1, %0, c15, c0, 3\n"
		"isb\n"
		"dsb"
		: : "r" (0x400));
	}

	/* Flush all cache levels for this cluster. */
	v7_exit_coherency_flush(all);

	/*
	 * Disable cluster-level coherency by masking
	 * incoming snoops and DVM messages:
	 */
	cci_disable_port_by_cpu(read_cpuid_mpidr());
}

static int sunxi_mc_smp_cpu_table[SUNXI_NR_CLUSTERS][SUNXI_CPUS_PER_CLUSTER];
int sunxi_mc_smp_first_comer;

static DEFINE_SPINLOCK(boot_lock);

static bool sunxi_mc_smp_cluster_is_down(unsigned int cluster)
{
	int i;

	for (i = 0; i < SUNXI_CPUS_PER_CLUSTER; i++)
		if (sunxi_mc_smp_cpu_table[cluster][i])
			return false;
	return true;
}

static void sunxi_mc_smp_secondary_init(unsigned int cpu)
{
	/* Clear hotplug support magic flags for cpu0 */
	if (cpu == 0)
		sunxi_cpu0_hotplug_support_set(false);
}

static int sunxi_mc_smp_boot_secondary(unsigned int l_cpu, struct task_struct *idle)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	if (!cpucfg_base)
		return -ENODEV;
	if (cluster >= SUNXI_NR_CLUSTERS || cpu >= SUNXI_CPUS_PER_CLUSTER)
		return -EINVAL;

	spin_lock_irq(&boot_lock);

	if (sunxi_mc_smp_cpu_table[cluster][cpu])
		goto out;

	if (sunxi_mc_smp_cluster_is_down(cluster)) {
		sunxi_mc_smp_first_comer = true;
		sunxi_cluster_powerup(cluster);
	} else {
		sunxi_mc_smp_first_comer = false;
	}

	/* This is read by incoming CPUs with their cache and MMU disabled */
	sync_cache_w(&sunxi_mc_smp_first_comer);
	sunxi_cpu_powerup(cpu, cluster);

out:
	sunxi_mc_smp_cpu_table[cluster][cpu]++;
	spin_unlock_irq(&boot_lock);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void sunxi_cluster_cache_disable(void)
{
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(read_cpuid_mpidr(), 1);
	u32 reg;

	pr_debug("%s: cluster %u\n", __func__, cluster);

	sunxi_cluster_cache_disable_without_axi();

	/* last man standing, assert ACINACTM */
	reg = readl(cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));
	reg |= CPUCFG_CX_CTRL_REG1_ACINACTM;
	writel(reg, cpucfg_base + CPUCFG_CX_CTRL_REG1(cluster));
}

static void sunxi_mc_smp_cpu_die(unsigned int l_cpu)
{
	unsigned int mpidr, cpu, cluster;
	bool last_man;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_debug("%s: cluster %u cpu %u\n", __func__, cluster, cpu);

	spin_lock(&boot_lock);
	sunxi_mc_smp_cpu_table[cluster][cpu]--;
	if (sunxi_mc_smp_cpu_table[cluster][cpu] == 1) {
		/* A power_up request went ahead of us. */
		pr_debug("%s: aborting due to a power up request\n",
			 __func__);
		spin_unlock(&boot_lock);
		return;
	} else if (sunxi_mc_smp_cpu_table[cluster][cpu] > 1) {
		pr_err("Cluster %d CPU%d boots multiple times\n",
		       cluster, cpu);
		BUG();
	}

	last_man = sunxi_mc_smp_cluster_is_down(cluster);
	spin_unlock(&boot_lock);

	gic_cpu_if_down(0);
	if (last_man)
		sunxi_cluster_cache_disable();
	else
		v7_exit_coherency_flush(louis);

	for (;;)
		wfi();
}

static int sunxi_cpu_powerdown(unsigned int cpu, unsigned int cluster)
{
	u32 reg;

	pr_debug("%s: cluster %u cpu %u\n", __func__, cluster, cpu);
	if (cpu >= SUNXI_CPUS_PER_CLUSTER || cluster >= SUNXI_NR_CLUSTERS)
		return -EINVAL;

	/* gate processor power */
	reg = readl(prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	reg |= PRCM_PWROFF_GATING_REG_CORE(cpu);
	writel(reg, prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	udelay(20);

	/* close power switch */
	sunxi_cpu_power_switch_set(cpu, cluster, false);

	return 0;
}

static int sunxi_cluster_powerdown(unsigned int cluster)
{
	u32 reg;

	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= SUNXI_NR_CLUSTERS)
		return -EINVAL;

	/* assert cluster resets or system will hang */
	pr_debug("%s: assert cluster reset\n", __func__);
	reg = readl(cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));
	reg &= ~CPUCFG_CX_RST_CTRL_DBG_SOC_RST;
	reg &= ~CPUCFG_CX_RST_CTRL_H_RST;
	reg &= ~CPUCFG_CX_RST_CTRL_L2_RST;
	writel(reg, cpucfg_base + CPUCFG_CX_RST_CTRL(cluster));

	/* gate cluster power */
	pr_debug("%s: gate cluster power\n", __func__);
	reg = readl(prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	if (is_a83t)
		reg |= PRCM_PWROFF_GATING_REG_CLUSTER_SUN8I;
	else
		reg |= PRCM_PWROFF_GATING_REG_CLUSTER_SUN9I;
	writel(reg, prcm_base + PRCM_PWROFF_GATING_REG(cluster));
	udelay(20);

	return 0;
}

static int sunxi_mc_smp_cpu_kill(unsigned int l_cpu)
{
	unsigned int mpidr, cpu, cluster;
	unsigned int tries, count;
	int ret = 0;
	u32 reg;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	/* This should never happen */
	if (WARN_ON(cluster >= SUNXI_NR_CLUSTERS ||
		    cpu >= SUNXI_CPUS_PER_CLUSTER))
		return 0;

	/* wait for CPU core to die and enter WFI */
	count = TIMEOUT_USEC / POLL_USEC;
	spin_lock_irq(&boot_lock);
	for (tries = 0; tries < count; tries++) {
		spin_unlock_irq(&boot_lock);
		usleep_range(POLL_USEC / 2, POLL_USEC);
		spin_lock_irq(&boot_lock);

		/*
		 * If the user turns off a bunch of cores at the same
		 * time, the kernel might call cpu_kill before some of
		 * them are ready. This is because boot_lock serializes
		 * both cpu_die and cpu_kill callbacks. Either one could
		 * run first. We should wait for cpu_die to complete.
		 */
		if (sunxi_mc_smp_cpu_table[cluster][cpu])
			continue;

		reg = readl(cpucfg_base + CPUCFG_CX_STATUS(cluster));
		if (reg & CPUCFG_CX_STATUS_STANDBYWFI(cpu))
			break;
	}

	if (tries >= count) {
		ret = ETIMEDOUT;
		goto out;
	}

	/* power down CPU core */
	sunxi_cpu_powerdown(cpu, cluster);

	if (!sunxi_mc_smp_cluster_is_down(cluster))
		goto out;

	/* wait for cluster L2 WFI */
	ret = readl_poll_timeout(cpucfg_base + CPUCFG_CX_STATUS(cluster), reg,
				 reg & CPUCFG_CX_STATUS_STANDBYWFIL2,
				 POLL_USEC, TIMEOUT_USEC);
	if (ret) {
		/*
		 * Ignore timeout on the cluster. Leaving the cluster on
		 * will not affect system execution, just use a bit more
		 * power. But returning an error here will only confuse
		 * the user as the CPU has already been shutdown.
		 */
		ret = 0;
		goto out;
	}

	/* Power down cluster */
	sunxi_cluster_powerdown(cluster);

out:
	spin_unlock_irq(&boot_lock);
	pr_debug("%s: cluster %u cpu %u powerdown: %d\n",
		 __func__, cluster, cpu, ret);
	return !ret;
}

static bool sunxi_mc_smp_cpu_can_disable(unsigned int cpu)
{
	/* CPU0 hotplug not handled for sun8i-a83t */
	if (is_a83t)
		if (cpu == 0)
			return false;
	return true;
}
#endif

static const struct smp_operations sunxi_mc_smp_smp_ops __initconst = {
	.smp_secondary_init	= sunxi_mc_smp_secondary_init,
	.smp_boot_secondary	= sunxi_mc_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= sunxi_mc_smp_cpu_die,
	.cpu_kill		= sunxi_mc_smp_cpu_kill,
	.cpu_can_disable	= sunxi_mc_smp_cpu_can_disable,
#endif
};

static bool __init sunxi_mc_smp_cpu_table_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	if (cluster >= SUNXI_NR_CLUSTERS || cpu >= SUNXI_CPUS_PER_CLUSTER) {
		pr_err("%s: boot CPU is out of bounds!\n", __func__);
		return false;
	}
	sunxi_mc_smp_cpu_table[cluster][cpu] = 1;
	return true;
}

/*
 * Adapted from arch/arm/common/mc_smp_entry.c
 *
 * We need the trampoline code to enable CCI-400 on the first cluster
 */
typedef typeof(cpu_reset) phys_reset_t;

static int __init nocache_trampoline(unsigned long __unused)
{
	phys_reset_t phys_reset;

	setup_mm_for_reboot();
	sunxi_cluster_cache_disable_without_axi();

	phys_reset = (phys_reset_t)(unsigned long)__pa_symbol(cpu_reset);
	phys_reset(__pa_symbol(sunxi_mc_smp_resume), false);
	BUG();
}

static int __init sunxi_mc_smp_loopback(void)
{
	int ret;

	/*
	 * We're going to soft-restart the current CPU through the
	 * low-level MCPM code by leveraging the suspend/resume
	 * infrastructure. Let's play it safe by using cpu_pm_enter()
	 * in case the CPU init code path resets the VFP or similar.
	 */
	sunxi_mc_smp_first_comer = true;
	local_irq_disable();
	local_fiq_disable();
	ret = cpu_pm_enter();
	if (!ret) {
		ret = cpu_suspend(0, nocache_trampoline);
		cpu_pm_exit();
	}
	local_fiq_enable();
	local_irq_enable();
	sunxi_mc_smp_first_comer = false;

	return ret;
}

/*
 * This holds any device nodes that we requested resources for,
 * so that we may easily release resources in the error path.
 */
struct sunxi_mc_smp_nodes {
	struct device_node *prcm_node;
	struct device_node *cpucfg_node;
	struct device_node *sram_node;
	struct device_node *r_cpucfg_node;
};

/* This structure holds SoC-specific bits tied to an enable-method string. */
struct sunxi_mc_smp_data {
	const char *enable_method;
	int (*get_smp_nodes)(struct sunxi_mc_smp_nodes *nodes);
	bool is_a83t;
};

static void __init sunxi_mc_smp_put_nodes(struct sunxi_mc_smp_nodes *nodes)
{
	of_node_put(nodes->prcm_node);
	of_node_put(nodes->cpucfg_node);
	of_node_put(nodes->sram_node);
	of_node_put(nodes->r_cpucfg_node);
	memset(nodes, 0, sizeof(*nodes));
}

static int __init sun9i_a80_get_smp_nodes(struct sunxi_mc_smp_nodes *nodes)
{
	nodes->prcm_node = of_find_compatible_node(NULL, NULL,
						   "allwinner,sun9i-a80-prcm");
	if (!nodes->prcm_node) {
		pr_err("%s: PRCM not available\n", __func__);
		return -ENODEV;
	}

	nodes->cpucfg_node = of_find_compatible_node(NULL, NULL,
						     "allwinner,sun9i-a80-cpucfg");
	if (!nodes->cpucfg_node) {
		pr_err("%s: CPUCFG not available\n", __func__);
		return -ENODEV;
	}

	nodes->sram_node = of_find_compatible_node(NULL, NULL,
						   "allwinner,sun9i-a80-smp-sram");
	if (!nodes->sram_node) {
		pr_err("%s: Secure SRAM not available\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static int __init sun8i_a83t_get_smp_nodes(struct sunxi_mc_smp_nodes *nodes)
{
	nodes->prcm_node = of_find_compatible_node(NULL, NULL,
						   "allwinner,sun8i-a83t-r-ccu");
	if (!nodes->prcm_node) {
		pr_err("%s: PRCM not available\n", __func__);
		return -ENODEV;
	}

	nodes->cpucfg_node = of_find_compatible_node(NULL, NULL,
						     "allwinner,sun8i-a83t-cpucfg");
	if (!nodes->cpucfg_node) {
		pr_err("%s: CPUCFG not available\n", __func__);
		return -ENODEV;
	}

	nodes->r_cpucfg_node = of_find_compatible_node(NULL, NULL,
						       "allwinner,sun8i-a83t-r-cpucfg");
	if (!nodes->r_cpucfg_node) {
		pr_err("%s: RCPUCFG not available\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static const struct sunxi_mc_smp_data sunxi_mc_smp_data[] __initconst = {
	{
		.enable_method	= "allwinner,sun9i-a80-smp",
		.get_smp_nodes	= sun9i_a80_get_smp_nodes,
	},
	{
		.enable_method	= "allwinner,sun8i-a83t-smp",
		.get_smp_nodes	= sun8i_a83t_get_smp_nodes,
		.is_a83t	= true,
	},
};

static int __init sunxi_mc_smp_init(void)
{
	struct sunxi_mc_smp_nodes nodes = { 0 };
	struct device_node *node;
	struct resource res;
	void __iomem *addr;
	int i, ret;

	/*
	 * Don't bother checking the "cpus" node, as an enable-method
	 * property in that node is undocumented.
	 */
	node = of_cpu_device_node_get(0);
	if (!node)
		return -ENODEV;

	/*
	 * We can't actually use the enable-method magic in the kernel.
	 * Our loopback / trampoline code uses the CPU suspend framework,
	 * which requires the identity mapping be available. It would not
	 * yet be available if we used the .init_cpus or .prepare_cpus
	 * callbacks in smp_operations, which we would use if we were to
	 * use CPU_METHOD_OF_DECLARE
	 */
	for (i = 0; i < ARRAY_SIZE(sunxi_mc_smp_data); i++) {
		ret = of_property_match_string(node, "enable-method",
					       sunxi_mc_smp_data[i].enable_method);
		if (!ret)
			break;
	}

	is_a83t = sunxi_mc_smp_data[i].is_a83t;

	of_node_put(node);
	if (ret)
		return -ENODEV;

	if (!sunxi_mc_smp_cpu_table_init())
		return -EINVAL;

	if (!cci_probed()) {
		pr_err("%s: CCI-400 not available\n", __func__);
		return -ENODEV;
	}

	/* Get needed device tree nodes */
	ret = sunxi_mc_smp_data[i].get_smp_nodes(&nodes);
	if (ret)
		goto err_put_nodes;

	/*
	 * Unfortunately we can not request the I/O region for the PRCM.
	 * It is shared with the PRCM clock.
	 */
	prcm_base = of_iomap(nodes.prcm_node, 0);
	if (!prcm_base) {
		pr_err("%s: failed to map PRCM registers\n", __func__);
		ret = -ENOMEM;
		goto err_put_nodes;
	}

	cpucfg_base = of_io_request_and_map(nodes.cpucfg_node, 0,
					    "sunxi-mc-smp");
	if (IS_ERR(cpucfg_base)) {
		ret = PTR_ERR(cpucfg_base);
		pr_err("%s: failed to map CPUCFG registers: %d\n",
		       __func__, ret);
		goto err_unmap_prcm;
	}

	if (is_a83t) {
		r_cpucfg_base = of_io_request_and_map(nodes.r_cpucfg_node,
						      0, "sunxi-mc-smp");
		if (IS_ERR(r_cpucfg_base)) {
			ret = PTR_ERR(r_cpucfg_base);
			pr_err("%s: failed to map R-CPUCFG registers\n",
			       __func__);
			goto err_unmap_release_cpucfg;
		}
	} else {
		sram_b_smp_base = of_io_request_and_map(nodes.sram_node, 0,
							"sunxi-mc-smp");
		if (IS_ERR(sram_b_smp_base)) {
			ret = PTR_ERR(sram_b_smp_base);
			pr_err("%s: failed to map secure SRAM\n", __func__);
			goto err_unmap_release_cpucfg;
		}
	}

	/* Configure CCI-400 for boot cluster */
	ret = sunxi_mc_smp_loopback();
	if (ret) {
		pr_err("%s: failed to configure boot cluster: %d\n",
		       __func__, ret);
		goto err_unmap_release_sram_rcpucfg;
	}

	/* We don't need the device nodes anymore */
	sunxi_mc_smp_put_nodes(&nodes);

	/* Set the hardware entry point address */
	if (is_a83t)
		addr = r_cpucfg_base + R_CPUCFG_CPU_SOFT_ENTRY_REG;
	else
		addr = prcm_base + PRCM_CPU_SOFT_ENTRY_REG;
	writel(__pa_symbol(sunxi_mc_smp_secondary_startup), addr);

	/* Actually enable multi cluster SMP */
	smp_set_ops(&sunxi_mc_smp_smp_ops);

	pr_info("sunxi multi cluster SMP support installed\n");

	return 0;

err_unmap_release_sram_rcpucfg:
	if (is_a83t) {
		iounmap(r_cpucfg_base);
		of_address_to_resource(nodes.r_cpucfg_node, 0, &res);
	} else {
		iounmap(sram_b_smp_base);
		of_address_to_resource(nodes.sram_node, 0, &res);
	}
	release_mem_region(res.start, resource_size(&res));
err_unmap_release_cpucfg:
	iounmap(cpucfg_base);
	of_address_to_resource(nodes.cpucfg_node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
err_unmap_prcm:
	iounmap(prcm_base);
err_put_nodes:
	sunxi_mc_smp_put_nodes(&nodes);
	return ret;
}

early_initcall(sunxi_mc_smp_init);
