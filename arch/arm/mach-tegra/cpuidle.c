/*
 * arch/arm/mach-tegra/cpuidle.c
 *
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
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/hrtimer.h>

#include <mach/iomap.h>

extern void tegra_cpu_wfi(void);

static int tegra_idle_enter_lp3(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int index);

struct cpuidle_driver tegra_idle_driver = {
	.name = "tegra_idle",
	.owner = THIS_MODULE,
	.state_count = 1,
	.states = {
		[0] = {
			.enter			= tegra_idle_enter_lp3,
			.exit_latency		= 10,
			.target_residency	= 10,
			.power_usage		= 600,
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "LP3",
			.desc			= "CPU flow-controlled",
		},
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, tegra_idle_device);

static int tegra_idle_enter_lp3(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index)
{
	ktime_t enter, exit;
	s64 us;

	local_irq_disable();
	local_fiq_disable();

	enter = ktime_get();

	tegra_cpu_wfi();

	exit = ktime_sub(ktime_get(), enter);
	us = ktime_to_us(exit);

	local_fiq_enable();
	local_irq_enable();

	dev->last_residency = us;

	return index;
}

static int __init tegra_cpuidle_init(void)
{
	int ret;
	unsigned int cpu;
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &tegra_idle_driver;

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
device_initcall(tegra_cpuidle_init);
