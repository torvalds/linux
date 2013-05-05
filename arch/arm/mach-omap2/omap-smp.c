/*
 * OMAP4 SMP source file. It contains platform specific fucntions
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

u16 pm44xx_errata;

/* SCU base address */
static void __iomem *scu_base;

static DEFINE_SPINLOCK(boot_lock);

void __iomem *omap4_get_scu_base(void)
{
	return scu_base;
}

static void __cpuinit omap4_secondary_init(unsigned int cpu)
{
	/*
	 * Configure ACTRL and enable NS SMP bit access on CPU1 on HS device.
	 * OMAP44XX EMU/HS devices - CPU0 SMP bit access is enabled in PPA
	 * init and for CPU1, a secure PPA API provided. CPU0 must be ON
	 * while executing NS_SMP API on CPU1 and PPA version must be 1.4.0+.
	 * OMAP443X GP devices- SMP bit isn't accessible.
	 * OMAP446X GP devices - SMP bit access is enabled on both CPUs.
	 */
	if (cpu_is_omap443x() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		omap_secure_dispatcher(OMAP4_PPA_CPU_ACTRL_SMP_INDEX,
							4, 0, 0, 0, 0, 0);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int __cpuinit omap4_boot_secondary(unsigned int cpu, struct task_struct *idle)
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
	 * omap_secondary_startup() routine will hold the secondary core till
	 * the AuxCoreBoot1 register is updated with cpu state
	 * A barrier is added to ensure that write buffer is drained
	 */
	if (omap_secure_apis_support())
		omap_modify_auxcoreboot0(0x200, 0xfffffdff);
	else
		__raw_writel(0x20, base + OMAP_AUX_CORE_BOOT_0);

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
		clkdm_wakeup(cpu1_clkdm);
		omap_set_pwrdm_state(cpu1_pwrdm, PWRDM_POWER_ON);
		clkdm_allow_idle(cpu1_clkdm);

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
	cpu_id = read_cpuid(CPUID_ID) & CPU_MASK;
	if (cpu_id == CPU_CORTEX_A9) {
		/*
		 * Currently we can't call ioremap here because
		 * SoC detection won't work until after init_early.
		 */
		scu_base =  OMAP2_L4_IO_ADDRESS(scu_a9_get_base());
		BUG_ON(!scu_base);
		ncores = scu_get_core_count(scu_base);
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
	void *startup_addr = omap_secondary_startup;
	void __iomem *base = omap_get_wakeupgen_base();

	/*
	 * Initialise the SCU and wake up the secondary core using
	 * wakeup_secondary().
	 */
	if (scu_base)
		scu_enable(scu_base);

	if (cpu_is_omap446x()) {
		startup_addr = omap_secondary_startup_4460;
		pm44xx_errata |= PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD;
	}

	/*
	 * Write the address of secondary startup routine into the
	 * AuxCoreBoot1 where ROM code will jump and start executing
	 * on secondary core once out of WFE
	 * A barrier is added to ensure that write buffer is drained
	 */
	if (omap_secure_apis_support())
		omap_auxcoreboot_addr(virt_to_phys(startup_addr));
	else
		__raw_writel(virt_to_phys(omap5_secondary_startup),
						base + OMAP_AUX_CORE_BOOT_1);

}

struct smp_operations omap4_smp_ops __initdata = {
	.smp_init_cpus		= omap4_smp_init_cpus,
	.smp_prepare_cpus	= omap4_smp_prepare_cpus,
	.smp_secondary_init	= omap4_secondary_init,
	.smp_boot_secondary	= omap4_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= omap4_cpu_die,
#endif
};
