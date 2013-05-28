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
#include <linux/jiffies.h>
#include <asm/cp15.h>
#include <asm/proc-fns.h>

#include "common.h"

static inline void cpu_enter_lowpower(void)
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
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void imx_cpu_die(unsigned int cpu)
{
	cpu_enter_lowpower();
	/*
	 * We use the cpu jumping argument register to sync with
	 * imx_cpu_kill() which is running on cpu0 and waiting for
	 * the register being cleared to kill the cpu.
	 */
	imx_set_cpu_arg(cpu, ~0);
	cpu_do_idle();
}

int imx_cpu_kill(unsigned int cpu)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(50);

	while (imx_get_cpu_arg(cpu) == 0)
		if (time_after(jiffies, timeout))
			return 0;
	imx_enable_cpu(cpu, false);
	imx_set_cpu_arg(cpu, 0);
	return 1;
}
