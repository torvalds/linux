/*
 * arch/arm/mach-meson6tv/hotplug.c
 *
 * Copyright (C) 2012-2013 Amlogic, Inc.
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


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <plat/regops.h>

#include <mach/io.h>
#include "common.h"

int meson6tv_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void meson6tv_cpu_die(unsigned int cpu)
{
	//aml_write_reg32((IO_AHB_BASE + 0x1ff80),0);
	meson_set_cpu_ctrl_reg(0);
	flush_cache_all();
	dsb();
	dmb();

	for (;;) {
		/*
		 * Execute WFI
		 */
		pr_debug("CPU%u: Enter WFI\n", cpu);
	        __asm__ __volatile__ ("wfi" : : : "memory");


		if (smp_processor_id() == cpu) {
			/*
			 * OK, proper wakeup, we're done
			 */
			if((aml_read_reg32(IO_AHB_BASE + 0x1ff80)&0x3) == ((1 << cpu) | 1))
			{
#ifdef CONFIG_MESON6_SMP_HOTPLUG
				/*
				 * Need invalidate data cache because of disable cpu0 fw
				 */
				extern  void v7_invalidate_dcache_all(void);
				v7_invalidate_dcache_all();
#endif
				break;
			}
		}
		pr_debug("CPU%u: spurious wakeup call\n", cpu);
	}
}

int meson6tv_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

