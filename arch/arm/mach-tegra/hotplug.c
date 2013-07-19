/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, 2012-2013, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/clk/tegra.h>

#include <asm/smp_plat.h>

#include "fuse.h"
#include "sleep.h"

static void (*tegra_hotplug_shutdown)(void);

int tegra_cpu_kill(unsigned cpu)
{
	cpu = cpu_logical_map(cpu);

	/* Clock gate the CPU */
	tegra_wait_cpu_in_reset(cpu);
	tegra_disable_cpu_clock(cpu);

	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void __ref tegra_cpu_die(unsigned int cpu)
{
	/* Clean L1 data cache */
	tegra_disable_clean_inv_dcache();

	/* Shut down the current CPU. */
	tegra_hotplug_shutdown();

	/* Should never return here. */
	BUG();
}

void __init tegra_hotplug_init(void)
{
	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return;

	if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC) && tegra_chip_id == TEGRA20)
		tegra_hotplug_shutdown = tegra20_hotplug_shutdown;
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) && tegra_chip_id == TEGRA30)
		tegra_hotplug_shutdown = tegra30_hotplug_shutdown;
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) && tegra_chip_id == TEGRA114)
		tegra_hotplug_shutdown = tegra30_hotplug_shutdown;
}
