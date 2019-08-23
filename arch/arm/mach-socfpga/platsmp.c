// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright 2012 Pavel Machek <pavel@denx.de>
 * Based on platsmp.c, Copyright (C) 2002 ARM Ltd.
 * Copyright (C) 2012 Altera Corporation
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

		writel(__pa_symbol(secondary_startup),
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

		writel(__pa_symbol(secondary_startup),
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

#ifdef CONFIG_HOTPLUG_CPU
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

/*
 * We need a dummy function so that platform_can_cpu_hotplug() knows
 * we support CPU hotplug. However, the function does not need to do
 * anything, because CPUs going offline just do WFI. We could reset
 * the CPUs but it would increase power consumption.
 */
static int socfpga_cpu_kill(unsigned int cpu)
{
	return 1;
}
#endif

static const struct smp_operations socfpga_smp_ops __initconst = {
	.smp_prepare_cpus	= socfpga_smp_prepare_cpus,
	.smp_boot_secondary	= socfpga_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= socfpga_cpu_die,
	.cpu_kill		= socfpga_cpu_kill,
#endif
};

static const struct smp_operations socfpga_a10_smp_ops __initconst = {
	.smp_prepare_cpus	= socfpga_smp_prepare_cpus,
	.smp_boot_secondary	= socfpga_a10_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= socfpga_cpu_die,
	.cpu_kill		= socfpga_cpu_kill,
#endif
};

CPU_METHOD_OF_DECLARE(socfpga_smp, "altr,socfpga-smp", &socfpga_smp_ops);
CPU_METHOD_OF_DECLARE(socfpga_a10_smp, "altr,socfpga-a10-smp", &socfpga_a10_smp_ops);
