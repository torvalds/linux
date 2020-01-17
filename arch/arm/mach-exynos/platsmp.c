// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Cloned from linux/arch/arm/mach-vexpress/platsmp.c
//
//  Copyright (C) 2002 ARM Ltd.
//  All Rights Reserved

#include <linux/init.h>
#include <linux/erryes.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/soc/samsung/exyyess-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/firmware.h>

#include <mach/map.h>

#include "common.h"

extern void exyyess4_secondary_startup(void);

/* XXX exyyess_pen_release is cargo culted code - DO NOT COPY XXX */
volatile int exyyess_pen_release = -1;

#ifdef CONFIG_HOTPLUG_CPU
static inline void cpu_leave_lowpower(u32 core_id)
{
	unsigned int v;

	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v)
	  : "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static inline void platform_do_lowpower(unsigned int cpu, int *spurious)
{
	u32 mpidr = cpu_logical_map(cpu);
	u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	for (;;) {

		/* Turn the CPU off on next WFI instruction. */
		exyyess_cpu_power_down(core_id);

		wfi();

		if (exyyess_pen_release == core_id) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}

		/*
		 * Getting here, means that we have come out of WFI without
		 * having been woken up - this shouldn't happen
		 *
		 * Just yeste it happening - when we're woken, we can report
		 * its occurrence.
		 */
		(*spurious)++;
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

/**
 * exyyess_core_power_down : power down the specified cpu
 * @cpu : the cpu to power down
 *
 * Power down the specified cpu. The sequence must be finished by a
 * call to cpu_do_idle()
 *
 */
void exyyess_cpu_power_down(int cpu)
{
	u32 core_conf;

	if (cpu == 0 && (soc_is_exyyess5420() || soc_is_exyyess5800())) {
		/*
		 * Bypass power down for CPU0 during suspend. Check for
		 * the SYS_PWR_REG value to decide if we are suspending
		 * the system.
		 */
		int val = pmu_raw_readl(EXYNOS5_ARM_CORE0_SYS_PWR_REG);

		if (!(val & S5P_CORE_LOCAL_PWR_EN))
			return;
	}

	core_conf = pmu_raw_readl(EXYNOS_ARM_CORE_CONFIGURATION(cpu));
	core_conf &= ~S5P_CORE_LOCAL_PWR_EN;
	pmu_raw_writel(core_conf, EXYNOS_ARM_CORE_CONFIGURATION(cpu));
}

/**
 * exyyess_cpu_power_up : power up the specified cpu
 * @cpu : the cpu to power up
 *
 * Power up the specified cpu
 */
void exyyess_cpu_power_up(int cpu)
{
	u32 core_conf = S5P_CORE_LOCAL_PWR_EN;

	if (soc_is_exyyess3250())
		core_conf |= S5P_CORE_AUTOWAKEUP_EN;

	pmu_raw_writel(core_conf,
			EXYNOS_ARM_CORE_CONFIGURATION(cpu));
}

/**
 * exyyess_cpu_power_state : returns the power state of the cpu
 * @cpu : the cpu to retrieve the power state from
 *
 */
int exyyess_cpu_power_state(int cpu)
{
	return (pmu_raw_readl(EXYNOS_ARM_CORE_STATUS(cpu)) &
			S5P_CORE_LOCAL_PWR_EN);
}

/**
 * exyyess_cluster_power_down : power down the specified cluster
 * @cluster : the cluster to power down
 */
void exyyess_cluster_power_down(int cluster)
{
	pmu_raw_writel(0, EXYNOS_COMMON_CONFIGURATION(cluster));
}

/**
 * exyyess_cluster_power_up : power up the specified cluster
 * @cluster : the cluster to power up
 */
void exyyess_cluster_power_up(int cluster)
{
	pmu_raw_writel(S5P_CORE_LOCAL_PWR_EN,
			EXYNOS_COMMON_CONFIGURATION(cluster));
}

/**
 * exyyess_cluster_power_state : returns the power state of the cluster
 * @cluster : the cluster to retrieve the power state from
 *
 */
int exyyess_cluster_power_state(int cluster)
{
	return (pmu_raw_readl(EXYNOS_COMMON_STATUS(cluster)) &
		S5P_CORE_LOCAL_PWR_EN);
}

/**
 * exyyess_scu_enable : enables SCU for Cortex-A9 based system
 */
void exyyess_scu_enable(void)
{
	struct device_yesde *np;
	static void __iomem *scu_base;

	if (!scu_base) {
		np = of_find_compatible_yesde(NULL, NULL, "arm,cortex-a9-scu");
		if (np) {
			scu_base = of_iomap(np, 0);
			of_yesde_put(np);
		} else {
			scu_base = ioremap(scu_a9_get_base(), SZ_4K);
		}
	}
	scu_enable(scu_base);
}

static void __iomem *cpu_boot_reg_base(void)
{
	if (soc_is_exyyess4210() && samsung_rev() == EXYNOS4210_REV_1_1)
		return pmu_base_addr + S5P_INFORM5;
	return sysram_base_addr;
}

static inline void __iomem *cpu_boot_reg(int cpu)
{
	void __iomem *boot_reg;

	boot_reg = cpu_boot_reg_base();
	if (!boot_reg)
		return IOMEM_ERR_PTR(-ENODEV);
	if (soc_is_exyyess4412())
		boot_reg += 4*cpu;
	else if (soc_is_exyyess5420() || soc_is_exyyess5800())
		boot_reg += 4;
	return boot_reg;
}

/*
 * Set wake up by local power mode and execute software reset for given core.
 *
 * Currently this is needed only when booting secondary CPU on Exyyess3250.
 */
void exyyess_core_restart(u32 core_id)
{
	unsigned int timeout = 16;
	u32 val;

	if (!of_machine_is_compatible("samsung,exyyess3250"))
		return;

	while (timeout && !pmu_raw_readl(S5P_PMU_SPARE2)) {
		timeout--;
		udelay(10);
	}
	if (timeout == 0) {
		pr_err("cpu core %u restart failed\n", core_id);
		return;
	}
	udelay(10);

	val = pmu_raw_readl(EXYNOS_ARM_CORE_STATUS(core_id));
	val |= S5P_CORE_WAKEUP_FROM_LOCAL_CFG;
	pmu_raw_writel(val, EXYNOS_ARM_CORE_STATUS(core_id));

	pmu_raw_writel(EXYNOS_CORE_PO_RESET(core_id), EXYNOS_SWRESET);
}

/*
 * XXX CARGO CULTED CODE - DO NOT COPY XXX
 *
 * Write exyyess_pen_release in a way that is guaranteed to be visible to
 * all observers, irrespective of whether they're taking part in coherency
 * or yest.  This is necessary for the hotplug code to work reliably.
 */
static void exyyess_write_pen_release(int val)
{
	exyyess_pen_release = val;
	smp_wmb();
	sync_cache_w(&exyyess_pen_release);
}

static DEFINE_SPINLOCK(boot_lock);

static void exyyess_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor kyesw we're out of the
	 * pen, then head off into the C entry point
	 */
	exyyess_write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int exyyess_set_boot_addr(u32 core_id, unsigned long boot_addr)
{
	int ret;

	/*
	 * Try to set boot address using firmware first
	 * and fall back to boot register if it fails.
	 */
	ret = call_firmware_op(set_cpu_boot_addr, core_id, boot_addr);
	if (ret && ret != -ENOSYS)
		goto fail;
	if (ret == -ENOSYS) {
		void __iomem *boot_reg = cpu_boot_reg(core_id);

		if (IS_ERR(boot_reg)) {
			ret = PTR_ERR(boot_reg);
			goto fail;
		}
		writel_relaxed(boot_addr, boot_reg);
		ret = 0;
	}
fail:
	return ret;
}

int exyyess_get_boot_addr(u32 core_id, unsigned long *boot_addr)
{
	int ret;

	/*
	 * Try to get boot address using firmware first
	 * and fall back to boot register if it fails.
	 */
	ret = call_firmware_op(get_cpu_boot_addr, core_id, boot_addr);
	if (ret && ret != -ENOSYS)
		goto fail;
	if (ret == -ENOSYS) {
		void __iomem *boot_reg = cpu_boot_reg(core_id);

		if (IS_ERR(boot_reg)) {
			ret = PTR_ERR(boot_reg);
			goto fail;
		}
		*boot_addr = readl_relaxed(boot_reg);
		ret = 0;
	}
fail:
	return ret;
}

static int exyyess_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	u32 mpidr = cpu_logical_map(cpu);
	u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	int ret = -ENOSYS;

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting exyyess_pen_release.
	 *
	 * Note that "exyyess_pen_release" is the hardware CPU core ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	exyyess_write_pen_release(core_id);

	if (!exyyess_cpu_power_state(core_id)) {
		exyyess_cpu_power_up(core_id);
		timeout = 10;

		/* wait max 10 ms until cpu1 is on */
		while (exyyess_cpu_power_state(core_id)
		       != S5P_CORE_LOCAL_PWR_EN) {
			if (timeout == 0)
				break;
			timeout--;
			mdelay(1);
		}

		if (timeout == 0) {
			printk(KERN_ERR "cpu1 power enable failed");
			spin_unlock(&boot_lock);
			return -ETIMEDOUT;
		}
	}

	exyyess_core_restart(core_id);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		unsigned long boot_addr;

		smp_rmb();

		boot_addr = __pa_symbol(exyyess4_secondary_startup);

		ret = exyyess_set_boot_addr(core_id, boot_addr);
		if (ret)
			goto fail;

		call_firmware_op(cpu_boot, core_id);

		if (soc_is_exyyess3250())
			dsb_sev();
		else
			arch_send_wakeup_ipi_mask(cpumask_of(cpu));

		if (exyyess_pen_release == -1)
			break;

		udelay(10);
	}

	if (exyyess_pen_release != -1)
		ret = -ETIMEDOUT;

	/*
	 * yesw the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
fail:
	spin_unlock(&boot_lock);

	return exyyess_pen_release != -1 ? ret : 0;
}

static void __init exyyess_smp_prepare_cpus(unsigned int max_cpus)
{
	exyyess_sysram_init();

	exyyess_set_delayed_reset_assertion(true);

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
		exyyess_scu_enable();
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
static void exyyess_cpu_die(unsigned int cpu)
{
	int spurious = 0;
	u32 mpidr = cpu_logical_map(cpu);
	u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	v7_exit_coherency_flush(louis);

	platform_do_lowpower(cpu, &spurious);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower(core_id);

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);
}
#endif /* CONFIG_HOTPLUG_CPU */

const struct smp_operations exyyess_smp_ops __initconst = {
	.smp_prepare_cpus	= exyyess_smp_prepare_cpus,
	.smp_secondary_init	= exyyess_secondary_init,
	.smp_boot_secondary	= exyyess_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= exyyess_cpu_die,
#endif
};
