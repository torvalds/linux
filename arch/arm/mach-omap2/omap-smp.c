/*
 * OMAP4 SMP source file. It contains platform specific functions
 * needed for the linux smp kernel.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author:
 *      Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Platform file needed for the OMAP4 SMP. This file is based on arm
 * realview smp platform.
 * * Copyright (c) 2002 ARM Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/smp_scu.h>
#include <asm/virt.h>

#include "omap-secure.h"
#include "omap-wakeupgen.h"
#include <asm/cputype.h>

#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "clockdomain.h"
#include "pm.h"

#define CPU_MASK		0xff0ffff0
#define CPU_CORTEX_A9		0x410FC090
#define CPU_CORTEX_A15		0x410FC0F0

#define OMAP5_CORE_COUNT	0x2

struct omap_smp_config {
	unsigned long cpu1_rstctrl_pa;
	void __iomem *cpu1_rstctrl_va;
	void __iomem *scu_base;
	void *startup_addr;
};

static struct omap_smp_config cfg;

static const struct omap_smp_config omap443x_cfg __initconst = {
	.cpu1_rstctrl_pa = 0x4824380c,
	.startup_addr = omap4_secondary_startup,
};

static const struct omap_smp_config omap446x_cfg __initconst = {
	.cpu1_rstctrl_pa = 0x4824380c,
	.startup_addr = omap4460_secondary_startup,
};

static const struct omap_smp_config omap5_cfg __initconst = {
	.cpu1_rstctrl_pa = 0x48243810,
	.startup_addr = omap5_secondary_startup,
};

static DEFINE_SPINLOCK(boot_lock);

void __iomem *omap4_get_scu_base(void)
{
	return cfg.scu_base;
}

#ifdef CONFIG_OMAP5_ERRATA_801819
void omap5_erratum_workaround_801819(void)
{
	u32 acr, revidr;
	u32 acr_mask;

	/* REVIDR[3] indicates erratum fix available on silicon */
	asm volatile ("mrc p15, 0, %0, c0, c0, 6" : "=r" (revidr));
	if (revidr & (0x1 << 3))
		return;

	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (acr));
	/*
	 * BIT(27) - Disables streaming. All write-allocate lines allocate in
	 * the L1 or L2 cache.
	 * BIT(25) - Disables streaming. All write-allocate lines allocate in
	 * the L1 cache.
	 */
	acr_mask = (0x3 << 25) | (0x3 << 27);
	/* do we already have it done.. if yes, skip expensive smc */
	if ((acr & acr_mask) == acr_mask)
		return;

	acr |= acr_mask;
	omap_smc1(OMAP5_DRA7_MON_SET_ACR_INDEX, acr);

	pr_debug("%s: ARM erratum workaround 801819 applied on CPU%d\n",
		 __func__, smp_processor_id());
}
#else
static inline void omap5_erratum_workaround_801819(void) { }
#endif

static void omap4_secondary_init(unsigned int cpu)
{
	/*
	 * Configure ACTRL and enable NS SMP bit access on CPU1 on HS device.
	 * OMAP44XX EMU/HS devices - CPU0 SMP bit access is enabled in PPA
	 * init and for CPU1, a secure PPA API provided. CPU0 must be ON
	 * while executing NS_SMP API on CPU1 and PPA version must be 1.4.0+.
	 * OMAP443X GP devices- SMP bit isn't accessible.
	 * OMAP446X GP devices - SMP bit access is enabled on both CPUs.
	 */
	if (soc_is_omap443x() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		omap_secure_dispatcher(OMAP4_PPA_CPU_ACTRL_SMP_INDEX,
							4, 0, 0, 0, 0, 0);

	if (soc_is_omap54xx() || soc_is_dra7xx()) {
		/*
		 * Configure the CNTFRQ register for the secondary cpu's which
		 * indicates the frequency of the cpu local timers.
		 */
		set_cntfreq();
		/* Configure ACR to disable streaming WA for 801819 */
		omap5_erratum_workaround_801819();
	}

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int omap4_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	static struct clockdomain *cpu1_clkdm;
	static bool booted;
	static struct powerdomain *cpu1_pwrdm;
	void __iomem *base = omap_get_wakeupgen_base();

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * Update the AuxCoreBoot0 with boot state for secondary core.
	 * omap4_secondary_startup() routine will hold the secondary core till
	 * the AuxCoreBoot1 register is updated with cpu state
	 * A barrier is added to ensure that write buffer is drained
	 */
	if (omap_secure_apis_support())
		omap_modify_auxcoreboot0(0x200, 0xfffffdff);
	else
		writel_relaxed(0x20, base + OMAP_AUX_CORE_BOOT_0);

	if (!cpu1_clkdm && !cpu1_pwrdm) {
		cpu1_clkdm = clkdm_lookup("mpu1_clkdm");
		cpu1_pwrdm = pwrdm_lookup("cpu1_pwrdm");
	}

	/*
	 * The SGI(Software Generated Interrupts) are not wakeup capable
	 * from low power states. This is known limitation on OMAP4 and
	 * needs to be worked around by using software forced clockdomain
	 * wake-up. To wakeup CPU1, CPU0 forces the CPU1 clockdomain to
	 * software force wakeup. The clockdomain is then put back to
	 * hardware supervised mode.
	 * More details can be found in OMAP4430 TRM - Version J
	 * Section :
	 *	4.3.4.2 Power States of CPU0 and CPU1
	 */
	if (booted && cpu1_pwrdm && cpu1_clkdm) {
		/*
		 * GIC distributor control register has changed between
		 * CortexA9 r1pX and r2pX. The Control Register secure
		 * banked version is now composed of 2 bits:
		 * bit 0 == Secure Enable
		 * bit 1 == Non-Secure Enable
		 * The Non-Secure banked register has not changed
		 * Because the ROM Code is based on the r1pX GIC, the CPU1
		 * GIC restoration will cause a problem to CPU0 Non-Secure SW.
		 * The workaround must be:
		 * 1) Before doing the CPU1 wakeup, CPU0 must disable
		 * the GIC distributor
		 * 2) CPU1 must re-enable the GIC distributor on
		 * it's wakeup path.
		 */
		if (IS_PM44XX_ERRATUM(PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD)) {
			local_irq_disable();
			gic_dist_disable();
		}

		/*
		 * Ensure that CPU power state is set to ON to avoid CPU
		 * powerdomain transition on wfi
		 */
		clkdm_deny_idle_nolock(cpu1_clkdm);
		pwrdm_set_next_pwrst(cpu1_pwrdm, PWRDM_POWER_ON);
		clkdm_allow_idle_nolock(cpu1_clkdm);

		if (IS_PM44XX_ERRATUM(PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD)) {
			while (gic_dist_disabled()) {
				udelay(1);
				cpu_relax();
			}
			gic_timer_retrigger();
			local_irq_enable();
		}
	} else {
		dsb_sev();
		booted = true;
	}

	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	/*
	 * Now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init omap4_smp_init_cpus(void)
{
	unsigned int i = 0, ncores = 1, cpu_id;

	/* Use ARM cpuid check here, as SoC detection will not work so early */
	cpu_id = read_cpuid_id() & CPU_MASK;
	if (cpu_id == CPU_CORTEX_A9) {
		/*
		 * Currently we can't call ioremap here because
		 * SoC detection won't work until after init_early.
		 */
		cfg.scu_base =  OMAP2_L4_IO_ADDRESS(scu_a9_get_base());
		BUG_ON(!cfg.scu_base);
		ncores = scu_get_core_count(cfg.scu_base);
	} else if (cpu_id == CPU_CORTEX_A15) {
		ncores = OMAP5_CORE_COUNT;
	}

	/* sanity check */
	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

static void __init omap4_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *base = omap_get_wakeupgen_base();
	const struct omap_smp_config *c = NULL;

	if (soc_is_omap443x())
		c = &omap443x_cfg;
	else if (soc_is_omap446x())
		c = &omap446x_cfg;
	else if (soc_is_dra74x() || soc_is_omap54xx())
		c = &omap5_cfg;

	if (!c) {
		pr_err("%s Unknown SMP SoC?\n", __func__);
		return;
	}

	/* Must preserve cfg.scu_base set earlier */
	cfg.cpu1_rstctrl_pa = c->cpu1_rstctrl_pa;
	cfg.startup_addr = c->startup_addr;

	if (soc_is_dra74x() || soc_is_omap54xx()) {
		if ((__boot_cpu_mode & MODE_MASK) == HYP_MODE)
			cfg.startup_addr = omap5_secondary_hyp_startup;
		omap5_erratum_workaround_801819();
	}

	cfg.cpu1_rstctrl_va = ioremap(cfg.cpu1_rstctrl_pa, 4);
	if (!cfg.cpu1_rstctrl_va)
		return;

	/*
	 * Initialise the SCU and wake up the secondary core using
	 * wakeup_secondary().
	 */
	if (cfg.scu_base)
		scu_enable(cfg.scu_base);

	/*
	 * Reset CPU1 before configuring, otherwise kexec will
	 * end up trying to use old kernel startup address.
	 */
	if (cfg.cpu1_rstctrl_va) {
		writel_relaxed(1, cfg.cpu1_rstctrl_va);
		readl_relaxed(cfg.cpu1_rstctrl_va);
		writel_relaxed(0, cfg.cpu1_rstctrl_va);
	}

	/*
	 * Write the address of secondary startup routine into the
	 * AuxCoreBoot1 where ROM code will jump and start executing
	 * on secondary core once out of WFE
	 * A barrier is added to ensure that write buffer is drained
	 */
	if (omap_secure_apis_support())
		omap_auxcoreboot_addr(virt_to_phys(cfg.startup_addr));
	else
		writel_relaxed(virt_to_phys(cfg.startup_addr),
			       base + OMAP_AUX_CORE_BOOT_1);
}

const struct smp_operations omap4_smp_ops __initconst = {
	.smp_init_cpus		= omap4_smp_init_cpus,
	.smp_prepare_cpus	= omap4_smp_prepare_cpus,
	.smp_secondary_init	= omap4_secondary_init,
	.smp_boot_secondary	= omap4_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= omap4_cpu_die,
	.cpu_kill		= omap4_cpu_kill,
#endif
};
