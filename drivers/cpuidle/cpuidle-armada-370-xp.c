/*
 * Marvell Armada 370 and Armada XP SoC cpuidle driver
 *
 * Copyright (C) 2014 Marvell
 *
 * Nadav Haklai <nadavh@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Maintainer: Gregory CLEMENT <gregory.clement@free-electrons.com>
 */

#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <asm/cpuidle.h>

#define ARMADA_370_XP_MAX_STATES	3
#define ARMADA_370_XP_FLAG_DEEP_IDLE	0x10000

static int (*armada_370_xp_cpu_suspend)(int);

static int armada_370_xp_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int ret;
	bool deepidle = false;
	cpu_pm_enter();

	if (drv->states[index].flags & ARMADA_370_XP_FLAG_DEEP_IDLE)
		deepidle = true;

	ret = armada_370_xp_cpu_suspend(deepidle);
	if (ret)
		return ret;

	cpu_pm_exit();

	return index;
}

static struct cpuidle_driver armada_370_xp_idle_driver = {
	.name			= "armada_370_xp_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= armada_370_xp_enter_idle,
		.exit_latency		= 10,
		.power_usage		= 50,
		.target_residency	= 100,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "Idle",
		.desc			= "CPU power down",
	},
	.states[2]		= {
		.enter			= armada_370_xp_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 5,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
						ARMADA_370_XP_FLAG_DEEP_IDLE,
		.name			= "Deep idle",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = ARMADA_370_XP_MAX_STATES,
};

static int armada_370_xp_cpuidle_probe(struct platform_device *pdev)
{

	armada_370_xp_cpu_suspend = (void *)(pdev->dev.platform_data);
	return cpuidle_register(&armada_370_xp_idle_driver, NULL);
}

static struct platform_driver armada_370_xp_cpuidle_plat_driver = {
	.driver = {
		.name = "cpuidle-armada-370-xp",
		.owner = THIS_MODULE,
	},
	.probe = armada_370_xp_cpuidle_probe,
};

module_platform_driver(armada_370_xp_cpuidle_plat_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Armada 370/XP cpu idle driver");
MODULE_LICENSE("GPL");
