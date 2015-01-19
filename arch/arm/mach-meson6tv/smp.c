/*
* arch/arm/mach-meson6tv/smp.c
*
* Copyright (C) 2013 Amlogic, Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <asm/smp.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/hardware/gic.h>
#include <plat/io.h>
#include <mach/cpu.h>
#include <mach/am_regs.h>

#include "common.h"

static DEFINE_SPINLOCK(boot_lock);

static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static void __cpuinit meson6tv_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
//	gic_secondary_init(0);
#ifdef CONFIG_MESON_ARM_GIC_FIQ
extern void  init_fiq(void);
	init_fiq();
#endif

	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static void __init wakeup_secondary(unsigned int cpu)
{
	/*
	* Write the address of secondary startup routine into the
	* AuxCoreBoot1 where ROM code will jump and start executing
	* on secondary core once out of WFE
	* A barrier is added to ensure that write buffer is drained
	*/
	aml_write_reg32((uint32_t)(IO_AHB_BASE + 0x1ff84),
	    (const uint32_t)virt_to_phys(meson_secondary_startup));

	//aml_write_reg32(IO_AHB_BASE + 0x1ff80, (1 << cpu) | 1);
	meson_set_cpu_ctrl_reg((1 << cpu) | 1);

	smp_wmb();
	/*
	 * Send a 'sev' to wake the secondary core from WFE.
	 * Drain the outstanding writes to memory
	 */
	 mb();
#ifndef CONFIG_MESON6_SMP_HOTPLUG
	dsb_sev();
#else
	gic_raise_softirq(cpumask_of(cpu), 0);
#endif

}

static int __cpuinit meson6tv_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	wakeup_secondary(1);
	/*
	* Set synchronisation state between this boot processor
	* and the secondary one
	*/
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 */
	write_pen_release(cpu_logical_map(cpu));

	//smp_send_reschedule(cpu);

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		if (pen_release == -1)
			break;
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);
	return pen_release != -1 ? -ENOSYS : 0;
}

static void __init meson6tv_smp_init_cpus(void)
{
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++)
		set_cpu_possible(i, true);

	 set_smp_cross_call(gic_raise_softirq);
}

static void __init meson6tv_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	* Initialise the present map, which describes the set of CPUs
	* actually populated at the present time.
	*/
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	/*
	* Initialise the SCU and wake up the secondary core using
	* wakeup_secondary().
	*/
	scu_enable((void __iomem *) IO_PERIPH_BASE);
	//wakeup_secondary(1);
	///    dsb_sev();
}


struct smp_operations __initdata meson6tv_smp_ops = {
	.smp_init_cpus		= meson6tv_smp_init_cpus,
	.smp_prepare_cpus	= meson6tv_smp_prepare_cpus,
	.smp_secondary_init	= meson6tv_secondary_init,
	.smp_boot_secondary	= meson6tv_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= meson6tv_cpu_die,
	.cpu_kill		= meson6tv_cpu_kill,
	.cpu_disable		= meson6tv_cpu_disable,
#endif
};


