/*
 * This file contains Xilinx specific SMP code, used to start up
 * the second processor.
 *
 * Copyright (C) 2011-2013 Xilinx
 *
 * based on linux/arch/arm/mach-realview/platsmp.c
 *
 * Copyright (C) 2002 ARM Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <linux/irqchip/arm-gic.h>
#include "common.h"

/*
 * Store number of cores in the system
 * Because of scu_get_core_count() must be in __init section and can't
 * be called from zynq_cpun_start() because it is not in __init section.
 */
static int ncores;

int zynq_cpun_start(u32 address, int cpu)
{
	u32 trampoline_code_size = &zynq_secondary_trampoline_end -
						&zynq_secondary_trampoline;

	/* MS: Expectation that SLCR are directly map and accessible */
	/* Not possible to jump to non aligned address */
	if (!(address & 3) && (!address || (address >= trampoline_code_size))) {
		/* Store pointer to ioremap area which points to address 0x0 */
		static u8 __iomem *zero;
		u32 trampoline_size = &zynq_secondary_trampoline_jump -
						&zynq_secondary_trampoline;

		zynq_slcr_cpu_stop(cpu);
		if (address) {
			if (__pa(PAGE_OFFSET)) {
				zero = ioremap(0, trampoline_code_size);
				if (!zero) {
					pr_warn("BOOTUP jump vectors not accessible\n");
					return -1;
				}
			} else {
				zero = (__force u8 __iomem *)PAGE_OFFSET;
			}

			/*
			* This is elegant way how to jump to any address
			* 0x0: Load address at 0x8 to r0
			* 0x4: Jump by mov instruction
			* 0x8: Jumping address
			*/
			memcpy_toio(zero, &zynq_secondary_trampoline,
							trampoline_size);
			writel(address, zero + trampoline_size);

			flush_cache_all();
			outer_flush_range(0, trampoline_code_size);
			smp_wmb();

			if (__pa(PAGE_OFFSET))
				iounmap(zero);
		}
		zynq_slcr_cpu_start(cpu);

		return 0;
	}

	pr_warn("Can't start CPU%d: Wrong starting address %x\n", cpu, address);

	return -1;
}
EXPORT_SYMBOL(zynq_cpun_start);

static int zynq_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return zynq_cpun_start(__pa_symbol(secondary_startup), cpu);
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init zynq_smp_init_cpus(void)
{
	int i;

	ncores = scu_get_core_count(zynq_scu_base);

	for (i = 0; i < ncores && i < CONFIG_NR_CPUS; i++)
		set_cpu_possible(i, true);
}

static void __init zynq_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(zynq_scu_base);
}

/**
 * zynq_secondary_init - Initialize secondary CPU cores
 * @cpu:	CPU that is initialized
 *
 * This function is in the hotplug path. Don't move it into the
 * init section!!
 */
static void zynq_secondary_init(unsigned int cpu)
{
	zynq_core_pm_init();
}

#ifdef CONFIG_HOTPLUG_CPU
static int zynq_cpu_kill(unsigned cpu)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(50);

	while (zynq_slcr_cpu_state_read(cpu))
		if (time_after(jiffies, timeout))
			return 0;

	zynq_slcr_cpu_stop(cpu);
	return 1;
}

/**
 * zynq_cpu_die - Let a CPU core die
 * @cpu:	Dying CPU
 *
 * Platform-specific code to shutdown a CPU.
 * Called with IRQs disabled on the dying CPU.
 */
static void zynq_cpu_die(unsigned int cpu)
{
	zynq_slcr_cpu_state_write(cpu, true);

	/*
	 * there is no power-control hardware on this platform, so all
	 * we can do is put the core into WFI; this is safe as the calling
	 * code will have already disabled interrupts
	 */
	for (;;)
		cpu_do_idle();
}
#endif

const struct smp_operations zynq_smp_ops __initconst = {
	.smp_init_cpus		= zynq_smp_init_cpus,
	.smp_prepare_cpus	= zynq_smp_prepare_cpus,
	.smp_boot_secondary	= zynq_boot_secondary,
	.smp_secondary_init	= zynq_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= zynq_cpu_die,
	.cpu_kill		= zynq_cpu_kill,
#endif
};
