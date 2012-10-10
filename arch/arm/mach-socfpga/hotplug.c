/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/errno.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>

static inline void cpu_enter_lowpower(void)
{
}

static inline void cpu_leave_lowpower(void)
{
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
void __ref platform_cpu_die(unsigned int cpu)
{
	cpu_enter_lowpower();

	cpu_do_idle();
	cpu_leave_lowpower();

	/* We should never return from idle */
	panic("cpu %d unexpectedly exit from shutdown\n", cpu);
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * CPU0 should not be shut down via hotplug.  cpu_idle can WFI
	 * or a proper shutdown or hibernate should be used.
	 */
	return cpu == 0 ? -EPERM : 0;
}
