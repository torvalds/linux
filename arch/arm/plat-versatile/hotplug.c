/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This hotplug implementation is _specific_ to the situation found on
 * ARM development platforms where there is _no_ possibility of actually
 * taking a CPU offline, resetting it, or otherwise.  Real platforms must
 * NOT copy this code.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <asm/smp_plat.h>
#include <asm/cp15.h>

#include <plat/platsmp.h>

static inline void versatile_immitation_enter_lowpower(unsigned int actrl_mask)
{
	unsigned int v;

	asm volatile(
		"mcr	p15, 0, %1, c7, c5, 0\n"
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
	  : "r" (0), "Ir" (CR_C), "Ir" (actrl_mask)
	  : "cc");
}

static inline void versatile_immitation_leave_lowpower(unsigned int actrl_mask)
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
	  : "Ir" (CR_C), "Ir" (actrl_mask)
	  : "cc");
}

static inline void versatile_immitation_do_lowpower(unsigned int cpu, int *spurious)
{
	/*
	 * there is no power-control hardware on this platform, so all
	 * we can do is put the core into WFI; this is safe as the calling
	 * code will have already disabled interrupts.
	 *
	 * This code should not be used outside Versatile platforms.
	 */
	for (;;) {
		wfi();

		if (versatile_cpu_release == cpu_logical_map(cpu)) {
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

/*
 * platform-specific code to shutdown a CPU.
 * This code supports immitation-style CPU hotplug for Versatile/Realview/
 * Versatile Express platforms that are unable to do real CPU hotplug.
 */
void versatile_immitation_cpu_die(unsigned int cpu, unsigned int actrl_mask)
{
	int spurious = 0;

	versatile_immitation_enter_lowpower(actrl_mask);
	versatile_immitation_do_lowpower(cpu, &spurious);
	versatile_immitation_leave_lowpower(actrl_mask);

	if (spurious)
		pr_warn("CPU%u: %u spurious wakeup calls\n", cpu, spurious);
}
