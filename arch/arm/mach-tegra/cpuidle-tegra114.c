// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 */

#include <asm/firmware.h>
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/firmware/trusted_foundations.h>

#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/psci.h>

#include "cpuidle.h"
#include "pm.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
#define TEGRA114_MAX_STATES 2
#else
#define TEGRA114_MAX_STATES 1
#endif

#ifdef CONFIG_PM_SLEEP
static int tegra114_idle_power_down(struct cpuidle_device *dev,
				    struct cpuidle_driver *drv,
				    int index)
{
	local_fiq_disable();

	tegra_set_cpu_in_lp2();
	cpu_pm_enter();

	call_firmware_op(prepare_idle, TF_PM_MODE_LP2_NOFLUSH_L2);

	/* Do suspend by ourselves if the firmware does not implement it */
	if (call_firmware_op(do_idle, 0) == -ENOSYS)
		cpu_suspend(0, tegra30_sleep_cpu_secondary_finish);

	cpu_pm_exit();
	tegra_clear_cpu_in_lp2();

	local_fiq_enable();

	return index;
}

static void tegra114_idle_enter_s2idle(struct cpuidle_device *dev,
				       struct cpuidle_driver *drv,
				       int index)
{
       tegra114_idle_power_down(dev, drv, index);
}
#endif

static struct cpuidle_driver tegra_idle_driver = {
	.name = "tegra_idle",
	.owner = THIS_MODULE,
	.state_count = TEGRA114_MAX_STATES,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE_PWR(600),
#ifdef CONFIG_PM_SLEEP
		[1] = {
			.enter			= tegra114_idle_power_down,
			.enter_s2idle		= tegra114_idle_enter_s2idle,
			.exit_latency		= 500,
			.target_residency	= 1000,
			.flags			= CPUIDLE_FLAG_TIMER_STOP,
			.power_usage		= 0,
			.name			= "powered-down",
			.desc			= "CPU power gated",
		},
#endif
	},
};

int __init tegra114_cpuidle_init(void)
{
	if (!psci_smp_available())
		return cpuidle_register(&tegra_idle_driver, NULL);

	return 0;
}
