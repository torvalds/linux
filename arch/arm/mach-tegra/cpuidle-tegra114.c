/*
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <asm/firmware.h>
#include <linux/clockchips.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/cpuidle.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

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

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	call_firmware_op(prepare_idle);

	/* Do suspend by ourselves if the firmware does not implement it */
	if (call_firmware_op(do_idle, 0) == -ENOSYS)
		cpu_suspend(0, tegra30_sleep_cpu_secondary_finish);

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

	cpu_pm_exit();
	tegra_clear_cpu_in_lp2();

	local_fiq_enable();

	return index;
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
			.exit_latency		= 500,
			.target_residency	= 1000,
			.power_usage		= 0,
			.name			= "powered-down",
			.desc			= "CPU power gated",
		},
#endif
	},
};

int __init tegra114_cpuidle_init(void)
{
	return cpuidle_register(&tegra_idle_driver, NULL);
}
