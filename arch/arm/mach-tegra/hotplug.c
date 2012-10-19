/*
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, 2012 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

#include "sleep.h"
#include "tegra_cpu_car.h"

static void (*tegra_hotplug_shutdown)(void);

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void __ref tegra_cpu_die(unsigned int cpu)
{
	cpu = cpu_logical_map(cpu);

	/* Flush the L1 data cache. */
	flush_cache_all();

	/* Shut down the current CPU. */
	tegra_hotplug_shutdown();

	/* Clock gate the CPU */
	tegra_wait_cpu_in_reset(cpu);
	tegra_disable_cpu_clock(cpu);

	/* Should never return here. */
	BUG();
}

int tegra_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
extern void tegra20_hotplug_shutdown(void);
void __init tegra20_hotplug_init(void)
{
	tegra_hotplug_shutdown = tegra20_hotplug_shutdown;
}
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
extern void tegra30_hotplug_shutdown(void);
void __init tegra30_hotplug_init(void)
{
	tegra_hotplug_shutdown = tegra30_hotplug_shutdown;
}
#endif
