/*
 *  linux/arch/arm/mach-tegra/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 *  Copyright (C) 2009 Palm
 *  All Rights Reserved
 *
 *  Copyright (C) 2010 NVIDIA Corporation
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
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/localtimer.h>
#include <asm/tlbflush.h>
#include <asm/smp_scu.h>
#include <asm/cpu.h>
#include <asm/mmu_context.h>

#include <mach/iomap.h>

#include "power.h"

extern void tegra_secondary_startup(void);

static DEFINE_SPINLOCK(boot_lock);
static void __iomem *scu_base = IO_ADDRESS(TEGRA_ARM_PERIF_BASE);

#ifdef CONFIG_HOTPLUG_CPU
static DEFINE_PER_CPU(struct completion, cpu_killed);
extern void tegra_hotplug_startup(void);
#endif

static DECLARE_BITMAP(cpu_init_bits, CONFIG_NR_CPUS) __read_mostly;
const struct cpumask *const cpu_init_mask = to_cpumask(cpu_init_bits);
#define cpu_init_map (*(cpumask_t *)cpu_init_mask)

#define EVP_CPU_RESET_VECTOR \
	(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + 0x100)
#define CLK_RST_CONTROLLER_CLK_CPU_CMPLX \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x4c)
#define CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x340)
#define CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x344)

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();
	gic_cpu_init(0, IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x100);
	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
#ifdef CONFIG_HOTPLUG_CPU
	cpu_set(cpu, cpu_init_map);
	INIT_COMPLETION(per_cpu(cpu_killed, cpu));
#endif
	spin_unlock(&boot_lock);
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long old_boot_vector;
	unsigned long boot_vector;
	unsigned long timeout;
	u32 reg;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/* set the reset vector to point to the secondary_startup routine */
#ifdef CONFIG_HOTPLUG_CPU
	if (cpumask_test_cpu(cpu, cpu_init_mask))
		boot_vector = virt_to_phys(tegra_hotplug_startup);
	else
#endif
		boot_vector = virt_to_phys(tegra_secondary_startup);

	smp_wmb();

	old_boot_vector = readl(EVP_CPU_RESET_VECTOR);
	writel(boot_vector, EVP_CPU_RESET_VECTOR);

	/* enable cpu clock on cpu */
	reg = readl(CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg & ~(1<<(8+cpu)), CLK_RST_CONTROLLER_CLK_CPU_CMPLX);

	reg = 0x1111<<cpu;
	writel(reg, CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR);

	/* unhalt the cpu */
	writel(0, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) + 0x14 + 0x8*(cpu-1));

	timeout = jiffies + HZ;
	while (time_before(jiffies, timeout)) {
		if (readl(EVP_CPU_RESET_VECTOR) != boot_vector)
			break;
		udelay(10);
	}

	/* put the old boot vector back */
	writel(old_boot_vector, EVP_CPU_RESET_VECTOR);

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores = scu_get_core_count(scu_base);

	for (i = 0; i < ncores; i++)
		cpu_set(i, cpu_possible_map);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int ncores = scu_get_core_count(scu_base);
	unsigned int cpu = smp_processor_id();
	int i;

	smp_store_cpu_info(cpu);

	/*
	 * are we trying to boot more cores than exist?
	 */
	if (max_cpus > ncores)
		max_cpus = ncores;

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

#ifdef CONFIG_HOTPLUG_CPU
	for_each_present_cpu(i) {
		init_completion(&per_cpu(cpu_killed, i));
	}
#endif

	/*
	 * Initialise the SCU if there are more than one CPU and let
	 * them know where to start. Note that, on modern versions of
	 * MILO, the "poke" doesn't actually do anything until each
	 * individual core is sent a soft interrupt to get it out of
	 * WFI
	 */
	if (max_cpus > 1) {
		percpu_timer_setup();
		scu_enable(scu_base);
	}
}

#ifdef CONFIG_HOTPLUG_CPU

extern void vfp_sync_state(struct thread_info *thread);

void __cpuinit secondary_start_kernel(void);

int platform_cpu_kill(unsigned int cpu)
{
	unsigned int reg;
	int e;

	e = wait_for_completion_timeout(&per_cpu(cpu_killed, cpu), 100);
	printk(KERN_NOTICE "CPU%u: %s shutdown\n", cpu, (e) ? "clean":"forced");

	if (e) {
		do {
			reg = readl(CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
			cpu_relax();
		} while (!(reg & (1<<cpu)));
	} else {
		writel(0x1111<<cpu, CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
		/* put flow controller in WAIT_EVENT mode */
		writel(2<<29, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE)+0x14 + 0x8*(cpu-1));
	}
	spin_lock(&boot_lock);
	reg = readl(CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg | (1<<(8+cpu)), CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	spin_unlock(&boot_lock);
	return e;
}

void platform_cpu_die(unsigned int cpu)
{
#ifdef DEBUG
	unsigned int this_cpu = hard_smp_processor_id();

	if (cpu != this_cpu) {
		printk(KERN_CRIT "Eek! platform_cpu_die running on %u, should be %u\n",
			   this_cpu, cpu);
		BUG();
	}
#endif

	gic_cpu_exit(0);
	barrier();
	complete(&per_cpu(cpu_killed, cpu));
	flush_cache_all();
	barrier();
	__cortex_a9_save(0);

	/* return happens from __cortex_a9_restore */
	barrier();
	writel(smp_processor_id(), EVP_CPU_RESET_VECTOR);
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	if (unlikely(!tegra_context_area))
		return -ENXIO;

	return cpu == 0 ? -EPERM : 0;
}
#endif
