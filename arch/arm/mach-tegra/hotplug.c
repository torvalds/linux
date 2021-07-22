// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, 2012-2013, NVIDIA Corporation. All rights reserved.
 */

#include <linux/clk/tegra.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

#include <asm/smp_plat.h>

#include "common.h"
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
void tegra_cpu_die(unsigned int cpu)
{
	if (!tegra_hotplug_shutdown) {
		WARN(1, "hotplug is not yet initialized\n");
		return;
	}

	/* Clean L1 data cache */
	tegra_disable_clean_inv_dcache(TEGRA_FLUSH_CACHE_LOUIS);

	/* Shut down the current CPU. */
	tegra_hotplug_shutdown();

	/* Should never return here. */
	BUG();
}

static int __init tegra_hotplug_init(void)
{
	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return 0;

	if (!soc_is_tegra())
		return 0;

	if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC) && tegra_get_chip_id() == TEGRA20)
		tegra_hotplug_shutdown = tegra20_hotplug_shutdown;
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) && tegra_get_chip_id() == TEGRA30)
		tegra_hotplug_shutdown = tegra30_hotplug_shutdown;
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) && tegra_get_chip_id() == TEGRA114)
		tegra_hotplug_shutdown = tegra30_hotplug_shutdown;
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC) && tegra_get_chip_id() == TEGRA124)
		tegra_hotplug_shutdown = tegra30_hotplug_shutdown;

	return 0;
}
pure_initcall(tegra_hotplug_init);
