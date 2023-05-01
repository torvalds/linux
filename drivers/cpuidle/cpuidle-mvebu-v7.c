/*
 * Marvell Armada 370, 38x and XP SoC cpuidle driver
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

static __cpuidle int mvebu_v7_enter_idle(struct cpuidle_device *dev,
					 struct cpuidle_driver *drv,
					 int index)
{
	int ret;
	bool deepidle = false;
	cpu_pm_enter();

	if (drv->states[index].flags & MVEBU_V7_FLAG_DEEP_IDLE)
		deepidle = true;

	ct_cpuidle_enter();
	ret = mvebu_v7_cpu_suspend(deepidle);
	ct_cpuidle_exit();

	cpu_pm_exit();

	if (ret)
		return ret;

	return index;
}

static struct cpuidle_driver armadaxp_idle_driver = {
	.name			= "armada_xp_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 100,
		.power_usage		= 50,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_RCU_IDLE,
		.name			= "MV CPU IDLE",
		.desc			= "CPU power down",
	},
	.states[2]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 1000,
		.power_usage		= 5,
		.target_residency	= 10000,
		.flags			= MVEBU_V7_FLAG_DEEP_IDLE | CPUIDLE_FLAG_RCU_IDLE,
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
		.flags			= MVEBU_V7_FLAG_DEEP_IDLE | CPUIDLE_FLAG_RCU_IDLE,
		.name			= "Deep Idle",
		.desc			= "CPU and L2 Fabric power down",
	},
	.state_count = 2,
};

static struct cpuidle_driver armada38x_idle_driver = {
	.name			= "armada_38x_idle",
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= mvebu_v7_enter_idle,
		.exit_latency		= 10,
		.power_usage		= 5,
		.target_residency	= 100,
		.flags			= CPUIDLE_FLAG_RCU_IDLE,
		.name			= "Idle",
		.desc			= "CPU and SCU power down",
	},
	.state_count = 2,
};

static int mvebu_v7_cpuidle_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = pdev->id_entry;

	if (!id)
		return -EINVAL;

	mvebu_v7_cpu_suspend = pdev->dev.platform_data;

	return cpuidle_register((struct cpuidle_driver *)id->driver_data, NULL);
}

static const struct platform_device_id mvebu_cpuidle_ids[] = {
	{
		.name = "cpuidle-armada-xp",
		.driver_data = (unsigned long)&armadaxp_idle_driver,
	}, {
		.name = "cpuidle-armada-370",
		.driver_data = (unsigned long)&armada370_idle_driver,
	}, {
		.name = "cpuidle-armada-38x",
		.driver_data = (unsigned long)&armada38x_idle_driver,
	},
	{}
};

static struct platform_driver mvebu_cpuidle_driver = {
	.probe = mvebu_v7_cpuidle_probe,
	.driver = {
		.name = "cpuidle-mbevu",
		.suppress_bind_attrs = true,
	},
	.id_table = mvebu_cpuidle_ids,
};

builtin_platform_driver(mvebu_cpuidle_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_DESCRIPTION("Marvell EBU v7 cpuidle driver");
MODULE_LICENSE("GPL");
