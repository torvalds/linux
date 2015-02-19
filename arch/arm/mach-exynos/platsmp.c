 /*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Cloned from linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/firmware.h>

#include <mach/map.h>

#include "common.h"
#include "regs-pmu.h"

extern void exynos4_secondary_startup(void);

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option, set on Exynos4 SoCs
 * during hot-(un)plugging CPUx.
 *
 * The feature can be cleared safely during first boot of secondary CPU.
 *
 * Exynos4 SoCs require setting USE_DELAYED_RESET_ASSERTION during powering
 * down a CPU so the CPU idle clock down feature could properly detect global
 * idle state when CPUx is off.
 */
static void exynos_set_delayed_reset_assertion(u32 core_id, bool enable)
{
	if (soc_is_exynos4()) {
		unsigned int tmp;

		tmp = pmu_raw_readl(EXYNOS_ARM_CORE_OPTION(core_id));
		if (enable)
			tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
		else
			tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
		pmu_raw_writel(tmp, EXYNOS_ARM_CORE_OPTION(core_id));
	}
}

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

	 exynos_set_delayed_reset_assertion(core_id, false);
}

static inline void platform_do_lowpower(unsigned int cpu, int *spurious)
{
	u32 mpidr = cpu_logical_map(cpu);
	u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	for (;;) {

		/* Turn the CPU off on next WFI instruction. */
		exynos_cpu_power_down(core_id);

		/*
		 * Exynos4 SoCs require setting
		 * USE_DELAYED_RESET_ASSERTION so the CPU idle
		 * clock down feature could properly detect
		 * global idle state when CPUx is off.
		 */
		exynos_set_delayed_reset_assertion(core_id, true);

		wfi();

		if (pen_release == core_id) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}

		/*
		 * Getting here, means that we have come out of WFI without
		 * having been woken up - this shouldn't happen
		 *
		 * Just note it happening - when we're woken, we can report
		 * its occurrence.
		 */
		(*spurious)++;
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

/**
 * exynos_core_power_down : power down the specified cpu
 * @cpu : the cpu to power down
 *
 * Power down the specified cpu. The sequence must be finished by a
 * call to cpu_do_idle()
 *
 */
void exynos_cpu_power_down(int cpu)
{
	if (cpu == 0 && (of_machine_is_compatible("samsung,exynos5420") ||
		of_machine_is_compatible("samsung,exynos5800"))) {
		/*
		 * Bypass power down for CPU0 during suspend. Check for
		 * the SYS_PWR_REG value to decide if we are suspending
		 * the system.
		 */
		int val = pmu_raw_readl(EXYNOS5_ARM_CORE0_SYS_PWR_REG);

		if (!(val & S5P_CORE_LOCAL_PWR_EN))
			return;
	}
	pmu_raw_writel(0, EXYNOS_ARM_CORE_CONFIGURATION(cpu));
}

/**
 * exynos_cpu_power_up : power up the specified cpu
 * @cpu : the cpu to power up
 *
 * Power up the specified cpu
 */
void exynos_cpu_power_up(int cpu)
{
	pmu_raw_writel(S5P_CORE_LOCAL_PWR_EN,
			EXYNOS_ARM_CORE_CONFIGURATION(cpu));
}

/**
 * exynos_cpu_power_state : returns the power state of the cpu
 * @cpu : the cpu to retrieve the power state from
 *
 */
int exynos_cpu_power_state(int cpu)
{
	return (pmu_raw_readl(EXYNOS_ARM_CORE_STATUS(cpu)) &
			S5P_CORE_LOCAL_PWR_EN);
}

/**
 * exynos_cluster_power_down : power down the specified cluster
 * @cluster : the cluster to power down
 */
void exynos_cluster_power_down(int cluster)
{
	pmu_raw_writel(0, EXYNOS_COMMON_CONFIGURATION(cluster));
}

/**
 * exynos_cluster_power_up : power up the specified cluster
 * @cluster : the cluster to power up
 */
void exynos_cluster_power_up(int cluster)
{
	pmu_raw_writel(S5P_CORE_LOCAL_PWR_EN,
			EXYNOS_COMMON_CONFIGURATION(cluster));
}

/**
 * exynos_cluster_power_state : returns the power state of the cluster
 * @cluster : the cluster to retrieve the power state from
 *
 */
int exynos_cluster_power_state(int cluster)
{
	return (pmu_raw_readl(EXYNOS_COMMON_STATUS(cluster)) &
		S5P_CORE_LOCAL_PWR_EN);
}

void __iomem *cpu_boot_reg_base(void)
{
	if (soc_is_exynos4210() && samsung_rev() == EXYNOS4210_REV_1_1)
		return pmu_base_addr + S5P_INFORM5;
	return sysram_base_addr;
}

static inline void __iomem *cpu_boot_reg(int cpu)
{
	void __iomem *boot_reg;

	boot_reg = cpu_boot_reg_base();
	if (!boot_reg)
		return ERR_PTR(-ENODEV);
	if (soc_is_exynos4412())
		boot_reg += 4*cpu;
	else if (soc_is_exynos5420() || soc_is_exynos5800())
		boot_reg += 4;
	return boot_reg;
}

/*
 * Set wake up by local power mode and execute software reset for given core.
 *
 * Currently this is needed only when booting secondary CPU on Exynos3250.
 */
static void exynos_core_restart(u32 core_id)
{
	u32 val;

	if (!of_machine_is_compatible("samsung,exynos3250"))
		return;

	val = pmu_raw_readl(EXYNOS_ARM_CORE_STATUS(core_id));
	val |= S5P_CORE_WAKEUP_FROM_LOCAL_CFG;
	pmu_raw_writel(val, EXYNOS_ARM_CORE_STATUS(core_id));

	pr_info("CPU%u: Software reset\n", core_id);
	pmu_raw_writel(EXYNOS_CORE_PO_RESET(core_id), EXYNOS_SWRESET);
}

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	sync_cache_w(&pen_release);
}

static void __iomem *scu_base_addr(void)
{
	return (void __iomem *)(S5P_VA_SCU);
}

static DEFINE_SPINLOCK(boot_lock);

static void exynos_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int exynos_boot_secondary(unsigned int cpu, struct task_struct *idle)
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
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU core ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	write_pen_release(core_id);

	if (!exynos_cpu_power_state(core_id)) {
		exynos_cpu_power_up(core_id);
		timeout = 10;

		/* wait max 10 ms until cpu1 is on */
		while (exynos_cpu_power_state(core_id)
		       != S5P_CORE_LOCAL_PWR_EN) {
			if (timeout-- == 0)
				break;

			mdelay(1);
		}

		if (timeout == 0) {
			printk(KERN_ERR "cpu1 power enable failed");
			spin_unlock(&boot_lock);
			return -ETIMEDOUT;
		}
	}

	exynos_core_restart(core_id);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		unsigned long boot_addr;

		smp_rmb();

		boot_addr = virt_to_phys(exynos4_secondary_startup);

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
			__raw_writel(boot_addr, boot_reg);
		}

		call_firmware_op(cpu_boot, core_id);

		arch_send_wakeup_ipi_mask(cpumask_of(cpu));

		if (pen_release == -1)
			break;

		udelay(10);
	}

	/* No harm if this is called during first boot of secondary CPU */
	exynos_set_delayed_reset_assertion(core_id, false);

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
fail:
	spin_unlock(&boot_lock);

	return pen_release != -1 ? ret : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */

static void __init exynos_smp_init_cpus(void)
{
	void __iomem *scu_base = scu_base_addr();
	unsigned int i, ncores;

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
		ncores = scu_base ? scu_get_core_count(scu_base) : 1;
	else
		/*
		 * CPU Nodes are passed thru DT and set_cpu_possible
		 * is set by "arm_dt_init_cpu_maps".
		 */
		return;

	/* sanity check */
	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

static void __init exynos_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	exynos_sysram_init();

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
		scu_enable(scu_base_addr());

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The boot monitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 *
	 * Try using firmware operation first and fall back to
	 * boot register if it fails.
	 */
	for (i = 1; i < max_cpus; ++i) {
		unsigned long boot_addr;
		u32 mpidr;
		u32 core_id;
		int ret;

		mpidr = cpu_logical_map(i);
		core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
		boot_addr = virt_to_phys(exynos4_secondary_startup);

		ret = call_firmware_op(set_cpu_boot_addr, core_id, boot_addr);
		if (ret && ret != -ENOSYS)
			break;
		if (ret == -ENOSYS) {
			void __iomem *boot_reg = cpu_boot_reg(core_id);

			if (IS_ERR(boot_reg))
				break;
			__raw_writel(boot_addr, boot_reg);
		}
	}
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
static void exynos_cpu_die(unsigned int cpu)
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

struct smp_operations exynos_smp_ops __initdata = {
	.smp_init_cpus		= exynos_smp_init_cpus,
	.smp_prepare_cpus	= exynos_smp_prepare_cpus,
	.smp_secondary_init	= exynos_secondary_init,
	.smp_boot_secondary	= exynos_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= exynos_cpu_die,
#endif
};
