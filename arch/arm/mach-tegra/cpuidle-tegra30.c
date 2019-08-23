// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 * Copyright (c) 2011 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * Rework for 3.3 by Peter De Schrijver <pdeschrijver@nvidia.com>
 */

#include <linux/clk/tegra.h>
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

#include "cpuidle.h"
#include "pm.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
static int tegra30_idle_lp2(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv,
			    int index);
#endif

static struct cpuidle_driver tegra_idle_driver = {
	.name = "tegra_idle",
	.owner = THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
	.state_count = 2,
#else
	.state_count = 1,
#endif
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE_PWR(600),
#ifdef CONFIG_PM_SLEEP
		[1] = {
			.enter			= tegra30_idle_lp2,
			.exit_latency		= 2000,
			.target_residency	= 2200,
			.power_usage		= 0,
			.flags			= CPUIDLE_FLAG_TIMER_STOP,
			.name			= "powered-down",
			.desc			= "CPU power gated",
		},
#endif
	},
};

#ifdef CONFIG_PM_SLEEP
static bool tegra30_cpu_cluster_power_down(struct cpuidle_device *dev,
					   struct cpuidle_driver *drv,
					   int index)
{
	/* All CPUs entering LP2 is not working.
	 * Don't let CPU0 enter LP2 when any secondary CPU is online.
	 */
	if (num_online_cpus() > 1 || !tegra_cpu_rail_off_ready()) {
		cpu_do_idle();
		return false;
	}

	tegra_idle_lp2_last();

	return true;
}

#ifdef CONFIG_SMP
static bool tegra30_cpu_core_power_down(struct cpuidle_device *dev,
					struct cpuidle_driver *drv,
					int index)
{
	smp_wmb();

	cpu_suspend(0, tegra30_sleep_cpu_secondary_finish);

	return true;
}
#else
static inline bool tegra30_cpu_core_power_down(struct cpuidle_device *dev,
					       struct cpuidle_driver *drv,
					       int index)
{
	return true;
}
#endif

static int tegra30_idle_lp2(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv,
			    int index)
{
	bool entered_lp2 = false;
	bool last_cpu;

	local_fiq_disable();

	last_cpu = tegra_set_cpu_in_lp2();
	cpu_pm_enter();

	if (dev->cpu == 0) {
		if (last_cpu)
			entered_lp2 = tegra30_cpu_cluster_power_down(dev, drv,
								     index);
		else
			cpu_do_idle();
	} else {
		entered_lp2 = tegra30_cpu_core_power_down(dev, drv, index);
	}

	cpu_pm_exit();
	tegra_clear_cpu_in_lp2();

	local_fiq_enable();

	smp_rmb();

	return (entered_lp2) ? index : 0;
}
#endif

int __init tegra30_cpuidle_init(void)
{
	return cpuidle_register(&tegra_idle_driver, NULL);
}
