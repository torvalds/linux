/*
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 * Copyright (c) 2011 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * Rework for 3.3 by Peter De Schrijver <pdeschrijver@nvidia.com>
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/clk/tegra.h>

#include <asm/cpuidle.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/smp_plat.h>

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
	.en_core_tk_irqen = 1,
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
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "powered-down",
			.desc			= "CPU power gated",
		},
#endif
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, tegra_idle_device);

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

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	tegra_idle_lp2_last();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

	return true;
}

#ifdef CONFIG_SMP
static bool tegra30_cpu_core_power_down(struct cpuidle_device *dev,
					struct cpuidle_driver *drv,
					int index)
{
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	smp_wmb();

	cpu_suspend(0, tegra30_sleep_cpu_secondary_finish);

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

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
	u32 cpu = is_smp() ? cpu_logical_map(dev->cpu) : dev->cpu;
	bool entered_lp2 = false;
	bool last_cpu;

	local_fiq_disable();

	last_cpu = tegra_set_cpu_in_lp2(cpu);
	cpu_pm_enter();

	if (cpu == 0) {
		if (last_cpu)
			entered_lp2 = tegra30_cpu_cluster_power_down(dev, drv,
								     index);
		else
			cpu_do_idle();
	} else {
		entered_lp2 = tegra30_cpu_core_power_down(dev, drv, index);
	}

	cpu_pm_exit();
	tegra_clear_cpu_in_lp2(cpu);

	local_fiq_enable();

	smp_rmb();

	return (entered_lp2) ? index : 0;
}
#endif

int __init tegra30_cpuidle_init(void)
{
	int ret;
	unsigned int cpu;
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &tegra_idle_driver;

#ifdef CONFIG_PM_SLEEP
	tegra_tear_down_cpu = tegra30_tear_down_cpu;
#endif

	ret = cpuidle_register_driver(&tegra_idle_driver);
	if (ret) {
		pr_err("CPUidle driver registration failed\n");
		return ret;
	}

	for_each_possible_cpu(cpu) {
		dev = &per_cpu(tegra_idle_device, cpu);
		dev->cpu = cpu;

		dev->state_count = drv->state_count;
		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("CPU%u: CPUidle device registration failed\n",
				cpu);
			return ret;
		}
	}
	return 0;
}
