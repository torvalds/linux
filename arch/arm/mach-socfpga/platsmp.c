/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright 2012 Pavel Machek <pavel@denx.de>
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
 * Copyright (C) 2012 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#include "core.h"

static int socfpga_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int trampoline_size = &secondary_trampoline_end - &secondary_trampoline;

	if (socfpga_cpu1start_addr) {
		/* This will put CPU #1 into reset. */
		writel(RSTMGR_MPUMODRST_CPU1,
		       rst_manager_base_addr + SOCFPGA_RSTMGR_MODMPURST);

		memcpy(phys_to_virt(0), &secondary_trampoline, trampoline_size);

		writel(virt_to_phys(secondary_startup),
		       sys_manager_base_addr + (socfpga_cpu1start_addr & 0x000000ff));

		flush_cache_all();
		smp_wmb();
		outer_clean_range(0, trampoline_size);

		/* This will release CPU #1 out of reset. */
		writel(0, rst_manager_base_addr + SOCFPGA_RSTMGR_MODMPURST);
	}

	return 0;
}

static int socfpga_a10_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int trampoline_size = &secondary_trampoline_end - &secondary_trampoline;

	if (socfpga_cpu1start_addr) {
		writel(RSTMGR_MPUMODRST_CPU1, rst_manager_base_addr +
		       SOCFPGA_A10_RSTMGR_MODMPURST);
		memcpy(phys_to_virt(0), &secondary_trampoline, trampoline_size);

		writel(virt_to_phys(secondary_startup),
		       sys_manager_base_addr + (socfpga_cpu1start_addr & 0x00000fff));

		flush_cache_all();
		smp_wmb();
		outer_clean_range(0, trampoline_size);

		/* This will release CPU #1 out of reset. */
		writel(0, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_MODMPURST);
	}

	return 0;
}

static void __init socfpga_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *socfpga_scu_base_addr;

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");
	if (!np) {
		pr_err("%s: missing scu\n", __func__);
		return;
	}

	socfpga_scu_base_addr = of_iomap(np, 0);
	if (!socfpga_scu_base_addr)
		return;
	scu_enable(socfpga_scu_base_addr);
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
static void socfpga_cpu_die(unsigned int cpu)
{
	/* Do WFI. If we wake up early, go back into WFI */
	while (1)
		cpu_do_idle();
}

static struct smp_operations socfpga_smp_ops __initdata = {
	.smp_prepare_cpus	= socfpga_smp_prepare_cpus,
	.smp_boot_secondary	= socfpga_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= socfpga_cpu_die,
#endif
};

static struct smp_operations socfpga_a10_smp_ops __initdata = {
	.smp_prepare_cpus	= socfpga_smp_prepare_cpus,
	.smp_boot_secondary	= socfpga_a10_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= socfpga_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(socfpga_smp, "altr,socfpga-smp", &socfpga_smp_ops);
CPU_METHOD_OF_DECLARE(socfpga_a10_smp, "altr,socfpga-a10-smp", &socfpga_a10_smp_ops);
