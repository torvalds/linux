/*
 * OMAP4 SMP cpu-hotplug support
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author:
 *      Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Platform file needed for the OMAP4 SMP. This file is based on arm
 * realview smp platform.
 * Copyright (c) 2002 ARM Limited.
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
#include "omap-wakeupgen.h"

#include "common.h"

#include "powerdomain.h"

int platform_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 * Called with IRQs disabled
 */
void __ref platform_cpu_die(unsigned int cpu)
{
	unsigned int boot_cpu = 0;
	void __iomem *base = omap_get_wakeupgen_base();

	flush_cache_all();
	dsb();

	/*
	 * we're ready for shutdown now, so do it
	 */
	if (omap_secure_apis_support()) {
		if (omap_modify_auxcoreboot0(0x0, 0x200) != 0x0)
			pr_err("Secure clear status failed\n");
	} else {
		__raw_writel(0, base + OMAP_AUX_CORE_BOOT_0);
	}


	for (;;) {
		/*
		 * Enter into low power state
		 */
		omap4_hotplug_cpu(cpu, PWRDM_POWER_OFF);

		if (omap_secure_apis_support())
			boot_cpu = omap_read_auxcoreboot0();
		else
			boot_cpu =
				__raw_readl(base + OMAP_AUX_CORE_BOOT_0) >> 5;

		if (boot_cpu == smp_processor_id()) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}
		pr_debug("CPU%u: spurious wakeup call\n", cpu);
	}
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
