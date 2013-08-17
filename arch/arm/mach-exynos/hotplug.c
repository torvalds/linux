/* linux arch/arm/mach-exynos4/hotplug.c
 *
 *  Cloned from linux/arch/arm/mach-realview/hotplug.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>

#include <plat/cpu.h>
#include <mach/regs-pmu.h>

extern volatile int pen_release;
extern void change_power_base(unsigned int cpu, void __iomem *base);

static inline void cpu_enter_lowpower_a9(void)
{
	unsigned int v;

	flush_cache_all();
	asm volatile(
	"	mcr	p15, 0, %1, c7, c5, 0\n"
	"	mcr	p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %3\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static inline void cpu_enter_lowpower_a15(void)
{
	unsigned int v, u;

	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 0\n"
	"       bic     %0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "Ir" (CR_C)
	  : "cc");

	flush_dcache_level(flush_cache_level_cpu());

	asm volatile(
	/*
	* Turn off coherency
	*/
	"       mrc     p15, 0, %0, c1, c0, 1\n"
	"       bic     %0, %0, %2\n"
	"	ldr	%1, [%3]\n"
	"	and	%1, %1, #0\n"
	"	orr	%0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 1\n"
	: "=&r" (v), "=&r" (u)
	: "Ir" (0x40), "Ir" (EXYNOS_INFORM0)
	: "cc");

	isb();
	dsb();
}

static inline void cpu_leave_lowpower(void)
{
	unsigned int v, u;

	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %3\n"
	"	ldr	%1, [%4]\n"
	"	and	%1, %1, #0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v), "=&r" (u)
	  : "Ir" (CR_C), "Ir" (0x40), "Ir" (EXYNOS_INFORM0)
	  : "cc");
}

static void exynos_power_down_cpu(unsigned int cpu)
{
	void __iomem *power_base;
	unsigned int pwr_offset = 0;

	set_boot_flag(cpu, HOTPLUG);

	if (soc_is_exynos5410()) {
		int cluster_id = read_cpuid_mpidr() & 0x100;
		if (samsung_rev() < EXYNOS5410_REV_1_0) {
			if (cluster_id == 0)
				pwr_offset = 4;
		} else {
			if (cluster_id != 0)
				pwr_offset = 4;
		}
	}

	power_base = EXYNOS_ARM_CORE_CONFIGURATION(cpu + pwr_offset);
#ifdef CONFIG_EXYNOS5_CCI
	change_power_base(cpu, power_base);
#endif
	__raw_writel(0, power_base);

	return;
}

static inline void platform_do_lowpower(unsigned int cpu, int *spurious)
{
	for (;;) {

		/* make secondary cpus to be turned off at next WFI command */
		exynos_power_down_cpu(cpu_logical_map(cpu));

		/*
		 * here's the WFI
		 */
		asm(".word	0xe320f003\n"
		    :
		    :
		    : "memory", "cc");

		if (pen_release == cpu_logical_map(cpu)) {
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

int platform_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void platform_cpu_die(unsigned int cpu)
{
	int spurious = 0;

	/*
	 * we're ready for shutdown now, so do it
	 */
	if (soc_is_exynos5250() || soc_is_exynos5410())
		cpu_enter_lowpower_a15();
	else
		cpu_enter_lowpower_a9();
	platform_do_lowpower(cpu, &spurious);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower();

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
