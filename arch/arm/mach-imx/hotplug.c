// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/errno.h>
#include <linux/jiffies.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/proc-fns.h>

#include "common.h"
#include "hardware.h"

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void imx_cpu_die(unsigned int cpu)
{
	v7_exit_coherency_flush(louis);
	/*
	 * We use the cpu jumping argument register to sync with
	 * imx_cpu_kill() which is running on cpu0 and waiting for
	 * the register being cleared to kill the cpu.
	 */
	imx_set_cpu_arg(cpu, ~0);

	while (1)
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
	if (cpu_is_imx7d())
		imx_gpcv2_set_core1_pdn_pup_by_software(true);
	return 1;
}
