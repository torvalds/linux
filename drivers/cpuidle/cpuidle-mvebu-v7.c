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

#define MVEBU_V7_FLAG_DEEP_IDLE	0x10000

static int (*mvebu_v7_cpu_suspend)(int);

static int mvebu_v7_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int ret;
	bool deepidle = false;
	cpu_pm_enter();

	if (drv->states[index].flags & MVEBU_V7_FLAG_DEEP_IDLE)
		deepidle = true;

	ret = mvebu_v7_cpu_suspend(deepidle);
	if (ret)
		return ret;

	cpu_pm_exit();

	return index;
}

static struct cpuidle_driver armadaxp_idle_driver = {
	.name			= "armada_xp_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 10,
		.power_usage		= 50,
		.target_residency	= 100,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "MV CPU IDLE",
		.desc			= "CPU power down",
	},
	.states[2]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 5,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
						MVEBU_V7_FLAG_DEEP_IDLE,
		.name			= "MV CPU DEEP IDLE",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = 3,
};

static struct cpuidle_driver armada370_idle_driver = {
	.name			= "armada_370_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 5,
		.target_residency	= 1000,
		.flags			= (CPUIDLE_FLAG_TIME_VALID |
					   MVEBU_V7_FLAG_DEEP_IDLE),
		.name			= "Deep Idle",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = 2,
};

static int mvebu_v7_cpuidle_probe(struct platform_device *pdev)
{
	mvebu_v7_cpu_suspend = pdev->dev.platform_data;

	if (!strcmp(pdev->dev.driver->name, "cpuidle-armada-xp"))
		return cpuidle_register(&armadaxp_idle_driver, NULL);
	else if (!strcmp(pdev->dev.driver->name, "cpuidle-armada-370"))
		return cpuidle_register(&armada370_idle_driver, NULL);
	else
		return -EINVAL;
}

static struct platform_driver armadaxp_cpuidle_plat_driver = {
	.driver = {
		.name = "cpuidle-armada-xp",
		.owner = THIS_MODULE,
	},
	.probe = mvebu_v7_cpuidle_probe,
};

module_platform_driver(armadaxp_cpuidle_plat_driver);

static struct platform_driver armada370_cpuidle_plat_driver = {
	.driver = {
		.name = "cpuidle-armada-370",
		.owner = THIS_MODULE,
	},
	.probe = mvebu_v7_cpuidle_probe,
};

module_platform_driver(armada370_cpuidle_plat_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Marvell EBU v7 cpuidle driver");
MODULE_LICENSE("GPL");
