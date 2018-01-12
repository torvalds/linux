// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// EXYNOS - Power Management support
//
// Based on arch/arm/mach-s3c2410/pm.c
// Copyright (c) 2006 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include <asm/firmware.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>

#include <mach/map.h>

#include "common.h"

static inline void __iomem *exynos_boot_vector_addr(void)
{
	if (samsung_rev() == EXYNOS4210_REV_1_1)
		return pmu_base_addr + S5P_INFORM7;
	else if (samsung_rev() == EXYNOS4210_REV_1_0)
		return sysram_base_addr + 0x24;
	return pmu_base_addr + S5P_INFORM0;
}

static inline void __iomem *exynos_boot_vector_flag(void)
{
	if (samsung_rev() == EXYNOS4210_REV_1_1)
		return pmu_base_addr + S5P_INFORM6;
	else if (samsung_rev() == EXYNOS4210_REV_1_0)
		return sysram_base_addr + 0x20;
	return pmu_base_addr + S5P_INFORM1;
}

#define S5P_CHECK_AFTR  0xFCBA0D10

/* For Cortex-A9 Diagnostic and Power control register */
static unsigned int save_arm_register[2];

void exynos_cpu_save_register(void)
{
	unsigned long tmp;

	/* Save Power control register */
	asm ("mrc p15, 0, %0, c15, c0, 0"
	     : "=r" (tmp) : : "cc");

	save_arm_register[0] = tmp;

	/* Save Diagnostic register */
	asm ("mrc p15, 0, %0, c15, c0, 1"
	     : "=r" (tmp) : : "cc");

	save_arm_register[1] = tmp;
}

void exynos_cpu_restore_register(void)
{
	unsigned long tmp;

	/* Restore Power control register */
	tmp = save_arm_register[0];

	asm volatile ("mcr p15, 0, %0, c15, c0, 0"
		      : : "r" (tmp)
		      : "cc");

	/* Restore Diagnostic register */
	tmp = save_arm_register[1];

	asm volatile ("mcr p15, 0, %0, c15, c0, 1"
		      : : "r" (tmp)
		      : "cc");
}

void exynos_pm_central_suspend(void)
{
	unsigned long tmp;

	/* Setting Central Sequence Register for power down mode */
	tmp = pmu_raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~S5P_CENTRAL_LOWPWR_CFG;
	pmu_raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);
}

int exynos_pm_central_resume(void)
{
	unsigned long tmp;

	/*
	 * If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = pmu_raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & S5P_CENTRAL_LOWPWR_CFG)) {
		tmp |= S5P_CENTRAL_LOWPWR_CFG;
		pmu_raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);
		/* clear the wakeup state register */
		pmu_raw_writel(0x0, S5P_WAKEUP_STAT);
		/* No need to perform below restore code */
		return -1;
	}

	return 0;
}

/* Ext-GIC nIRQ/nFIQ is the only wakeup source in AFTR */
static void exynos_set_wakeupmask(long mask)
{
	pmu_raw_writel(mask, S5P_WAKEUP_MASK);
	if (soc_is_exynos3250())
		pmu_raw_writel(0x0, S5P_WAKEUP_MASK2);
}

static void exynos_cpu_set_boot_vector(long flags)
{
	writel_relaxed(__pa_symbol(exynos_cpu_resume),
		       exynos_boot_vector_addr());
	writel_relaxed(flags, exynos_boot_vector_flag());
}

static int exynos_aftr_finisher(unsigned long flags)
{
	int ret;

	exynos_set_wakeupmask(soc_is_exynos3250() ? 0x40003ffe : 0x0000ff3e);
	/* Set value of power down register for aftr mode */
	exynos_sys_powerdown_conf(SYS_AFTR);

	ret = call_firmware_op(do_idle, FW_DO_IDLE_AFTR);
	if (ret == -ENOSYS) {
		if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
			exynos_cpu_save_register();
		exynos_cpu_set_boot_vector(S5P_CHECK_AFTR);
		cpu_do_idle();
	}

	return 1;
}

void exynos_enter_aftr(void)
{
	unsigned int cpuid = smp_processor_id();

	cpu_pm_enter();

	if (soc_is_exynos3250())
		exynos_set_boot_flag(cpuid, C2_STATE);

	exynos_pm_central_suspend();

	if (of_machine_is_compatible("samsung,exynos4412")) {
		/* Setting SEQ_OPTION register */
		pmu_raw_writel(S5P_USE_STANDBY_WFI0 | S5P_USE_STANDBY_WFE0,
			       S5P_CENTRAL_SEQ_OPTION);
	}

	cpu_suspend(0, exynos_aftr_finisher);

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9) {
		scu_enable(S5P_VA_SCU);
		if (call_firmware_op(resume) == -ENOSYS)
			exynos_cpu_restore_register();
	}

	exynos_pm_central_resume();

	if (soc_is_exynos3250())
		exynos_clear_boot_flag(cpuid, C2_STATE);

	cpu_pm_exit();
}

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYNOS_CPUIDLE)
static atomic_t cpu1_wakeup = ATOMIC_INIT(0);

static int exynos_cpu0_enter_aftr(void)
{
	int ret = -1;

	/*
	 * If the other cpu is powered on, we have to power it off, because
	 * the AFTR state won't work otherwise
	 */
	if (cpu_online(1)) {
		/*
		 * We reach a sync point with the coupled idle state, we know
		 * the other cpu will power down itself or will abort the
		 * sequence, let's wait for one of these to happen
		 */
		while (exynos_cpu_power_state(1)) {
			unsigned long boot_addr;

			/*
			 * The other cpu may skip idle and boot back
			 * up again
			 */
			if (atomic_read(&cpu1_wakeup))
				goto abort;

			/*
			 * The other cpu may bounce through idle and
			 * boot back up again, getting stuck in the
			 * boot rom code
			 */
			ret = exynos_get_boot_addr(1, &boot_addr);
			if (ret)
				goto fail;
			ret = -1;
			if (boot_addr == 0)
				goto abort;

			cpu_relax();
		}
	}

	exynos_enter_aftr();
	ret = 0;

abort:
	if (cpu_online(1)) {
		unsigned long boot_addr = __pa_symbol(exynos_cpu_resume);

		/*
		 * Set the boot vector to something non-zero
		 */
		ret = exynos_set_boot_addr(1, boot_addr);
		if (ret)
			goto fail;
		dsb();

		/*
		 * Turn on cpu1 and wait for it to be on
		 */
		exynos_cpu_power_up(1);
		while (exynos_cpu_power_state(1) != S5P_CORE_LOCAL_PWR_EN)
			cpu_relax();

		if (soc_is_exynos3250()) {
			while (!pmu_raw_readl(S5P_PMU_SPARE2) &&
			       !atomic_read(&cpu1_wakeup))
				cpu_relax();

			if (!atomic_read(&cpu1_wakeup))
				exynos_core_restart(1);
		}

		while (!atomic_read(&cpu1_wakeup)) {
			smp_rmb();

			/*
			 * Poke cpu1 out of the boot rom
			 */

			ret = exynos_set_boot_addr(1, boot_addr);
			if (ret)
				goto fail;

			call_firmware_op(cpu_boot, 1);

			if (soc_is_exynos3250())
				dsb_sev();
			else
				arch_send_wakeup_ipi_mask(cpumask_of(1));
		}
	}
fail:
	return ret;
}

static int exynos_wfi_finisher(unsigned long flags)
{
	if (soc_is_exynos3250())
		flush_cache_all();
	cpu_do_idle();

	return -1;
}

static int exynos_cpu1_powerdown(void)
{
	int ret = -1;

	/*
	 * Idle sequence for cpu1
	 */
	if (cpu_pm_enter())
		goto cpu1_aborted;

	/*
	 * Turn off cpu 1
	 */
	exynos_cpu_power_down(1);

	if (soc_is_exynos3250())
		pmu_raw_writel(0, S5P_PMU_SPARE2);

	ret = cpu_suspend(0, exynos_wfi_finisher);

	cpu_pm_exit();

cpu1_aborted:
	dsb();
	/*
	 * Notify cpu 0 that cpu 1 is awake
	 */
	atomic_set(&cpu1_wakeup, 1);

	return ret;
}

static void exynos_pre_enter_aftr(void)
{
	unsigned long boot_addr = __pa_symbol(exynos_cpu_resume);

	(void)exynos_set_boot_addr(1, boot_addr);
}

static void exynos_post_enter_aftr(void)
{
	atomic_set(&cpu1_wakeup, 0);
}

struct cpuidle_exynos_data cpuidle_coupled_exynos_data = {
	.cpu0_enter_aftr		= exynos_cpu0_enter_aftr,
	.cpu1_powerdown		= exynos_cpu1_powerdown,
	.pre_enter_aftr		= exynos_pre_enter_aftr,
	.post_enter_aftr		= exynos_post_enter_aftr,
};
#endif /* CONFIG_SMP && CONFIG_ARM_EXYNOS_CPUIDLE */
