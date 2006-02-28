/*
 *  linux/arch/arm/mach-realview/platsmp.c
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
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/hardware/arm_scu.h>
#include <asm/hardware.h>
#include <asm/io.h>

extern void realview_secondary_startup(void);

/*
 * control for which core is the next to come out of the secondary
 * boot "holding pen"
 */
volatile int __cpuinitdata pen_release = -1;

static unsigned int __init get_core_count(void)
{
	unsigned int ncores;

	ncores = __raw_readl(__io_address(REALVIEW_MPCORE_SCU_BASE) + SCU_CONFIG);

	return (ncores & 0x03) + 1;
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/*
	 * the primary core may have used a "cross call" soft interrupt
	 * to get this processor out of WFI in the BootMonitor - make
	 * sure that we are no longer being sent this soft interrupt
	 */
	smp_cross_call_done(cpumask_of_cpu(cpu));

	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_cpu_init(__io_address(REALVIEW_GIC_CPU_BASE));

	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	pen_release = -1;

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	pen_release = cpu;
	flush_cache_all();

	/*
	 * XXX
	 *
	 * This is a later addition to the booting protocol: the
	 * bootMonitor now puts secondary cores into WFI, so
	 * poke_milo() no longer gets the cores moving; we need
	 * to send a soft interrupt to wake the secondary core.
	 * Use smp_cross_call() for this, since there's little
	 * point duplicating the code here
	 */
	smp_cross_call(cpumask_of_cpu(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

static void __init poke_milo(void)
{
	extern void secondary_startup(void);

	/* nobody is to be released from the pen yet */
	pen_release = -1;

	/*
	 * write the address of secondary startup into the system-wide
	 * flags register, then clear the bottom two bits, which is what
	 * BootMonitor is waiting for
	 */
#if 1
#define REALVIEW_SYS_FLAGSS_OFFSET 0x30
	__raw_writel(virt_to_phys(realview_secondary_startup),
		     __io_address(REALVIEW_SYS_BASE) +
		     REALVIEW_SYS_FLAGSS_OFFSET);
#define REALVIEW_SYS_FLAGSC_OFFSET 0x34
	__raw_writel(3,
		     __io_address(REALVIEW_SYS_BASE) +
		     REALVIEW_SYS_FLAGSC_OFFSET);
#endif

	mb();
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores = get_core_count();

	for (i = 0; i < ncores; i++)
		cpu_set(i, cpu_possible_map);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int ncores = get_core_count();
	unsigned int cpu = smp_processor_id();
	int i;

	/* sanity check */
	if (ncores == 0) {
		printk(KERN_ERR
		       "Realview: strange CM count of 0? Default to 1\n");

		ncores = 1;
	}

	if (ncores > NR_CPUS) {
		printk(KERN_WARNING
		       "Realview: no. of cores (%d) greater than configured "
		       "maximum of %d - clipping\n",
		       ncores, NR_CPUS);
		ncores = NR_CPUS;
	}

	smp_store_cpu_info(cpu);

	/*
	 * are we trying to boot more cores than exist?
	 */
	if (max_cpus > ncores)
		max_cpus = ncores;

	/*
	 * Enable the local timer for primary CPU
	 */
	local_timer_setup(cpu);

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		cpu_set(i, cpu_present_map);

	/*
	 * Do we need any more CPUs? If so, then let them know where
	 * to start. Note that, on modern versions of MILO, the "poke"
	 * doesn't actually do anything until each individual core is
	 * sent a soft interrupt to get it out of WFI
	 */
	if (max_cpus > 1)
		poke_milo();
}
