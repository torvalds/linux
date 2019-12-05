// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP MPUSS low power code
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * OMAP4430 MPUSS mainly consists of dual Cortex-A9 with per-CPU
 * Local timer and Watchdog, GIC, SCU, PL310 L2 cache controller,
 * CPU0 and CPU1 LPRM modules.
 * CPU0, CPU1 and MPUSS each have there own power domain and
 * hence multiple low power combinations of MPUSS are possible.
 *
 * The CPU0 and CPU1 can't support Closed switch Retention (CSWR)
 * because the mode is not supported by hw constraints of dormant
 * mode. While waking up from the dormant mode, a reset  signal
 * to the Cortex-A9 processor must be asserted by the external
 * power controller.
 *
 * With architectural inputs and hardware recommendations, only
 * below modes are supported from power gain vs latency point of view.
 *
 *	CPU0		CPU1		MPUSS
 *	----------------------------------------------
 *	ON		ON		ON
 *	ON(Inactive)	OFF		ON(Inactive)
 *	OFF		OFF		CSWR
 *	OFF		OFF		OSWR
 *	OFF		OFF		OFF(Device OFF *TBD)
 *	----------------------------------------------
 *
 * Note: CPU0 is the master core and it is the last CPU to go down
 * and first to wake-up when MPUSS low power states are excercised
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/smp_scu.h>
#include <asm/pgalloc.h>
#include <asm/suspend.h>
#include <asm/virt.h>
#include <asm/hardware/cache-l2x0.h>

#include "soc.h"
#include "common.h"
#include "omap44xx.h"
#include "omap4-sar-layout.h"
#include "pm.h"
#include "prcm_mpu44xx.h"
#include "prcm_mpu54xx.h"
#include "prminst44xx.h"
#include "prcm44xx.h"
#include "prm44xx.h"
#include "prm-regbits-44xx.h"

static void __iomem *sar_base;
static u32 old_cpu1_ns_pa_addr;

#if defined(CONFIG_PM) && defined(CONFIG_SMP)

struct omap4_cpu_pm_info {
	struct powerdomain *pwrdm;
	void __iomem *scu_sar_addr;
	void __iomem *wkup_sar_addr;
	void __iomem *l2x0_sar_addr;
};

/**
 * struct cpu_pm_ops - CPU pm operations
 * @finish_suspend:	CPU suspend finisher function pointer
 * @resume:		CPU resume function pointer
 * @scu_prepare:	CPU Snoop Control program function pointer
 * @hotplug_restart:	CPU restart function pointer
 *
 * Structure holds functions pointer for CPU low power operations like
 * suspend, resume and scu programming.
 */
struct cpu_pm_ops {
	int (*finish_suspend)(unsigned long cpu_state);
	void (*resume)(void);
	void (*scu_prepare)(unsigned int cpu_id, unsigned int cpu_state);
	void (*hotplug_restart)(void);
};

static DEFINE_PER_CPU(struct omap4_cpu_pm_info, omap4_pm_info);
static struct powerdomain *mpuss_pd;
static u32 cpu_context_offset;

static int default_finish_suspend(unsigned long cpu_state)
{
	omap_do_wfi();
	return 0;
}

static void dummy_cpu_resume(void)
{}

static void dummy_scu_prepare(unsigned int cpu_id, unsigned int cpu_state)
{}

static struct cpu_pm_ops omap_pm_ops = {
	.finish_suspend		= default_finish_suspend,
	.resume			= dummy_cpu_resume,
	.scu_prepare		= dummy_scu_prepare,
	.hotplug_restart	= dummy_cpu_resume,
};

/*
 * Program the wakeup routine address for the CPU0 and CPU1
 * used for OFF or DORMANT wakeup.
 */
static inline void set_cpu_wakeup_addr(unsigned int cpu_id, u32 addr)
{
	struct omap4_cpu_pm_info *pm_info = &per_cpu(omap4_pm_info, cpu_id);

	if (pm_info->wkup_sar_addr)
		writel_relaxed(addr, pm_info->wkup_sar_addr);
}

/*
 * Store the SCU power status value to scratchpad memory
 */
static void scu_pwrst_prepare(unsigned int cpu_id, unsigned int cpu_state)
{
	struct omap4_cpu_pm_info *pm_info = &per_cpu(omap4_pm_info, cpu_id);
	u32 scu_pwr_st;

	switch (cpu_state) {
	case PWRDM_POWER_RET:
		scu_pwr_st = SCU_PM_DORMANT;
		break;
	case PWRDM_POWER_OFF:
		scu_pwr_st = SCU_PM_POWEROFF;
		break;
	case PWRDM_POWER_ON:
	case PWRDM_POWER_INACTIVE:
	default:
		scu_pwr_st = SCU_PM_NORMAL;
		break;
	}

	if (pm_info->scu_sar_addr)
		writel_relaxed(scu_pwr_st, pm_info->scu_sar_addr);
}

/* Helper functions for MPUSS OSWR */
static inline void mpuss_clear_prev_logic_pwrst(void)
{
	u32 reg;

	reg = omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
		OMAP4430_PRM_MPU_INST, OMAP4_RM_MPU_MPU_CONTEXT_OFFSET);
	omap4_prminst_write_inst_reg(reg, OMAP4430_PRM_PARTITION,
		OMAP4430_PRM_MPU_INST, OMAP4_RM_MPU_MPU_CONTEXT_OFFSET);
}

static inline void cpu_clear_prev_logic_pwrst(unsigned int cpu_id)
{
	u32 reg;

	if (cpu_id) {
		reg = omap4_prcm_mpu_read_inst_reg(OMAP4430_PRCM_MPU_CPU1_INST,
					cpu_context_offset);
		omap4_prcm_mpu_write_inst_reg(reg, OMAP4430_PRCM_MPU_CPU1_INST,
					cpu_context_offset);
	} else {
		reg = omap4_prcm_mpu_read_inst_reg(OMAP4430_PRCM_MPU_CPU0_INST,
					cpu_context_offset);
		omap4_prcm_mpu_write_inst_reg(reg, OMAP4430_PRCM_MPU_CPU0_INST,
					cpu_context_offset);
	}
}

/*
 * Store the CPU cluster state for L2X0 low power operations.
 */
static void l2x0_pwrst_prepare(unsigned int cpu_id, unsigned int save_state)
{
	struct omap4_cpu_pm_info *pm_info = &per_cpu(omap4_pm_info, cpu_id);

	if (pm_info->l2x0_sar_addr)
		writel_relaxed(save_state, pm_info->l2x0_sar_addr);
}

/*
 * Save the L2X0 AUXCTRL and POR value to SAR memory. Its used to
 * in every restore MPUSS OFF path.
 */
#ifdef CONFIG_CACHE_L2X0
static void __init save_l2x0_context(void)
{
	void __iomem *l2x0_base = omap4_get_l2cache_base();

	if (l2x0_base && sar_base) {
		writel_relaxed(l2x0_saved_regs.aux_ctrl,
			       sar_base + L2X0_AUXCTRL_OFFSET);
		writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
			       sar_base + L2X0_PREFETCH_CTRL_OFFSET);
	}
}
#else
static void __init save_l2x0_context(void)
{}
#endif

/**
 * omap4_enter_lowpower: OMAP4 MPUSS Low Power Entry Function
 * The purpose of this function is to manage low power programming
 * of OMAP4 MPUSS subsystem
 * @cpu : CPU ID
 * @power_state: Low power state.
 *
 * MPUSS states for the context save:
 * save_state =
 *	0 - Nothing lost and no need to save: MPUSS INACTIVE
 *	1 - CPUx L1 and logic lost: MPUSS CSWR
 *	2 - CPUx L1 and logic lost + GIC lost: MPUSS OSWR
 *	3 - CPUx L1 and logic lost + GIC + L2 lost: DEVICE OFF
 */
int omap4_enter_lowpower(unsigned int cpu, unsigned int power_state)
{
	struct omap4_cpu_pm_info *pm_info = &per_cpu(omap4_pm_info, cpu);
	unsigned int save_state = 0, cpu_logic_state = PWRDM_POWER_RET;

	if (omap_rev() == OMAP4430_REV_ES1_0)
		return -ENXIO;

	switch (power_state) {
	case PWRDM_POWER_ON:
	case PWRDM_POWER_INACTIVE:
		save_state = 0;
		break;
	case PWRDM_POWER_OFF:
		cpu_logic_state = PWRDM_POWER_OFF;
		save_state = 1;
		break;
	case PWRDM_POWER_RET:
		if (IS_PM44XX_ERRATUM(PM_OMAP4_CPU_OSWR_DISABLE))
			save_state = 0;
		break;
	default:
		/*
		 * CPUx CSWR is invalid hardware state. Also CPUx OSWR
		 * doesn't make much scense, since logic is lost and $L1
		 * needs to be cleaned because of coherency. This makes
		 * CPUx OSWR equivalent to CPUX OFF and hence not supported
		 */
		WARN_ON(1);
		return -ENXIO;
	}

	pwrdm_pre_transition(NULL);

	/*
	 * Check MPUSS next state and save interrupt controller if needed.
	 * In MPUSS OSWR or device OFF, interrupt controller  contest is lost.
	 */
	mpuss_clear_prev_logic_pwrst();
	if ((pwrdm_read_next_pwrst(mpuss_pd) == PWRDM_POWER_RET) &&
		(pwrdm_read_logic_retst(mpuss_pd) == PWRDM_POWER_OFF))
		save_state = 2;

	cpu_clear_prev_logic_pwrst(cpu);
	pwrdm_set_next_pwrst(pm_info->pwrdm, power_state);
	pwrdm_set_logic_retst(pm_info->pwrdm, cpu_logic_state);
	set_cpu_wakeup_addr(cpu, __pa_symbol(omap_pm_ops.resume));
	omap_pm_ops.scu_prepare(cpu, power_state);
	l2x0_pwrst_prepare(cpu, save_state);

	/*
	 * Call low level function  with targeted low power state.
	 */
	if (save_state)
		cpu_suspend(save_state, omap_pm_ops.finish_suspend);
	else
		omap_pm_ops.finish_suspend(save_state);

	if (IS_PM44XX_ERRATUM(PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD) && cpu)
		gic_dist_enable();

	/*
	 * Restore the CPUx power state to ON otherwise CPUx
	 * power domain can transitions to programmed low power
	 * state while doing WFI outside the low powe code. On
	 * secure devices, CPUx does WFI which can result in
	 * domain transition
	 */
	pwrdm_set_next_pwrst(pm_info->pwrdm, PWRDM_POWER_ON);

	pwrdm_post_transition(NULL);

	return 0;
}

/**
 * omap4_hotplug_cpu: OMAP4 CPU hotplug entry
 * @cpu : CPU ID
 * @power_state: CPU low power state.
 */
int omap4_hotplug_cpu(unsigned int cpu, unsigned int power_state)
{
	struct omap4_cpu_pm_info *pm_info = &per_cpu(omap4_pm_info, cpu);
	unsigned int cpu_state = 0;

	if (omap_rev() == OMAP4430_REV_ES1_0)
		return -ENXIO;

	/* Use the achievable power state for the domain */
	power_state = pwrdm_get_valid_lp_state(pm_info->pwrdm,
					       false, power_state);

	if (power_state == PWRDM_POWER_OFF)
		cpu_state = 1;

	pwrdm_clear_all_prev_pwrst(pm_info->pwrdm);
	pwrdm_set_next_pwrst(pm_info->pwrdm, power_state);
	set_cpu_wakeup_addr(cpu, __pa_symbol(omap_pm_ops.hotplug_restart));
	omap_pm_ops.scu_prepare(cpu, power_state);

	/*
	 * CPU never retuns back if targeted power state is OFF mode.
	 * CPU ONLINE follows normal CPU ONLINE ptah via
	 * omap4_secondary_startup().
	 */
	omap_pm_ops.finish_suspend(cpu_state);

	pwrdm_set_next_pwrst(pm_info->pwrdm, PWRDM_POWER_ON);
	return 0;
}


/*
 * Enable Mercury Fast HG retention mode by default.
 */
static void enable_mercury_retention_mode(void)
{
	u32 reg;

	reg = omap4_prcm_mpu_read_inst_reg(OMAP54XX_PRCM_MPU_DEVICE_INST,
				  OMAP54XX_PRCM_MPU_PRM_PSCON_COUNT_OFFSET);
	/* Enable HG_EN, HG_RAMPUP = fast mode */
	reg |= BIT(24) | BIT(25);
	omap4_prcm_mpu_write_inst_reg(reg, OMAP54XX_PRCM_MPU_DEVICE_INST,
				      OMAP54XX_PRCM_MPU_PRM_PSCON_COUNT_OFFSET);
}

/*
 * Initialise OMAP4 MPUSS
 */
int __init omap4_mpuss_init(void)
{
	struct omap4_cpu_pm_info *pm_info;

	if (omap_rev() == OMAP4430_REV_ES1_0) {
		WARN(1, "Power Management not supported on OMAP4430 ES1.0\n");
		return -ENODEV;
	}

	/* Initilaise per CPU PM information */
	pm_info = &per_cpu(omap4_pm_info, 0x0);
	if (sar_base) {
		pm_info->scu_sar_addr = sar_base + SCU_OFFSET0;
		if (cpu_is_omap44xx())
			pm_info->wkup_sar_addr = sar_base +
				CPU0_WAKEUP_NS_PA_ADDR_OFFSET;
		else
			pm_info->wkup_sar_addr = sar_base +
				OMAP5_CPU0_WAKEUP_NS_PA_ADDR_OFFSET;
		pm_info->l2x0_sar_addr = sar_base + L2X0_SAVE_OFFSET0;
	}
	pm_info->pwrdm = pwrdm_lookup("cpu0_pwrdm");
	if (!pm_info->pwrdm) {
		pr_err("Lookup failed for CPU0 pwrdm\n");
		return -ENODEV;
	}

	/* Clear CPU previous power domain state */
	pwrdm_clear_all_prev_pwrst(pm_info->pwrdm);
	cpu_clear_prev_logic_pwrst(0);

	/* Initialise CPU0 power domain state to ON */
	pwrdm_set_next_pwrst(pm_info->pwrdm, PWRDM_POWER_ON);

	pm_info = &per_cpu(omap4_pm_info, 0x1);
	if (sar_base) {
		pm_info->scu_sar_addr = sar_base + SCU_OFFSET1;
		if (cpu_is_omap44xx())
			pm_info->wkup_sar_addr = sar_base +
				CPU1_WAKEUP_NS_PA_ADDR_OFFSET;
		else
			pm_info->wkup_sar_addr = sar_base +
				OMAP5_CPU1_WAKEUP_NS_PA_ADDR_OFFSET;
		pm_info->l2x0_sar_addr = sar_base + L2X0_SAVE_OFFSET1;
	}

	pm_info->pwrdm = pwrdm_lookup("cpu1_pwrdm");
	if (!pm_info->pwrdm) {
		pr_err("Lookup failed for CPU1 pwrdm\n");
		return -ENODEV;
	}

	/* Clear CPU previous power domain state */
	pwrdm_clear_all_prev_pwrst(pm_info->pwrdm);
	cpu_clear_prev_logic_pwrst(1);

	/* Initialise CPU1 power domain state to ON */
	pwrdm_set_next_pwrst(pm_info->pwrdm, PWRDM_POWER_ON);

	mpuss_pd = pwrdm_lookup("mpu_pwrdm");
	if (!mpuss_pd) {
		pr_err("Failed to lookup MPUSS power domain\n");
		return -ENODEV;
	}
	pwrdm_clear_all_prev_pwrst(mpuss_pd);
	mpuss_clear_prev_logic_pwrst();

	if (sar_base) {
		/* Save device type on scratchpad for low level code to use */
		writel_relaxed((omap_type() != OMAP2_DEVICE_TYPE_GP) ? 1 : 0,
			       sar_base + OMAP_TYPE_OFFSET);
		save_l2x0_context();
	}

	if (cpu_is_omap44xx()) {
		omap_pm_ops.finish_suspend = omap4_finish_suspend;
		omap_pm_ops.resume = omap4_cpu_resume;
		omap_pm_ops.scu_prepare = scu_pwrst_prepare;
		omap_pm_ops.hotplug_restart = omap4_secondary_startup;
		cpu_context_offset = OMAP4_RM_CPU0_CPU0_CONTEXT_OFFSET;
	} else if (soc_is_omap54xx() || soc_is_dra7xx()) {
		cpu_context_offset = OMAP54XX_RM_CPU0_CPU0_CONTEXT_OFFSET;
		enable_mercury_retention_mode();
	}

	if (cpu_is_omap446x())
		omap_pm_ops.hotplug_restart = omap4460_secondary_startup;

	return 0;
}

#endif

u32 omap4_get_cpu1_ns_pa_addr(void)
{
	return old_cpu1_ns_pa_addr;
}

/*
 * For kexec, we must set CPU1_WAKEUP_NS_PA_ADDR to point to
 * current kernel's secondary_startup() early before
 * clockdomains_init(). Otherwise clockdomain_init() can
 * wake CPU1 and cause a hang.
 */
void __init omap4_mpuss_early_init(void)
{
	unsigned long startup_pa;
	void __iomem *ns_pa_addr;

	if (!(soc_is_omap44xx() || soc_is_omap54xx()))
		return;

	sar_base = omap4_get_sar_ram_base();

	/* Save old NS_PA_ADDR for validity checks later on */
	if (soc_is_omap44xx())
		ns_pa_addr = sar_base + CPU1_WAKEUP_NS_PA_ADDR_OFFSET;
	else
		ns_pa_addr = sar_base + OMAP5_CPU1_WAKEUP_NS_PA_ADDR_OFFSET;
	old_cpu1_ns_pa_addr = readl_relaxed(ns_pa_addr);

	if (soc_is_omap443x())
		startup_pa = __pa_symbol(omap4_secondary_startup);
	else if (soc_is_omap446x())
		startup_pa = __pa_symbol(omap4460_secondary_startup);
	else if ((__boot_cpu_mode & MODE_MASK) == HYP_MODE)
		startup_pa = __pa_symbol(omap5_secondary_hyp_startup);
	else
		startup_pa = __pa_symbol(omap5_secondary_startup);

	if (soc_is_omap44xx())
		writel_relaxed(startup_pa, sar_base +
			       CPU1_WAKEUP_NS_PA_ADDR_OFFSET);
	else
		writel_relaxed(startup_pa, sar_base +
			       OMAP5_CPU1_WAKEUP_NS_PA_ADDR_OFFSET);
}
