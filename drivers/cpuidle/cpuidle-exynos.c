/* linux/arch/arm/mach-exynos/cpuidle.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/cpuidle.h>

static void (*exynos_enter_aftr)(void);

static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int new_index = index;

	/* AFTR can only be entered when cores other than CPU0 are offline */
	if (num_online_cpus() > 1 || dev->cpu != 0)
		new_index = drv->safe_state_index;

	if (new_index == 0)
		return arm_cpuidle_simple_enter(dev, drv, new_index);

	exynos_enter_aftr();

	return new_index;
}

static struct cpuidle_driver exynos_idle_driver = {
	.name			= "exynos_idle",
	.owner			= THIS_MODULE,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE,
		[1] = {
			.enter			= exynos_enter_lowpower,
			.exit_latency		= 300,
			.target_residency	= 100000,
			.name			= "C1",
			.desc			= "ARM power down",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

static int exynos_cpuidle_probe(struct platform_device *pdev)
{
	int ret;

	exynos_enter_aftr = (void *)(pdev->dev.platform_data);

	ret = cpuidle_register(&exynos_idle_driver, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register cpuidle driver\n");
		return ret;
	}

	return 0;
}

static struct platform_driver exynos_cpuidle_driver = {
	.probe	= exynos_cpuidle_probe,
	.driver = {
		.name = "exynos_cpuidle",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(exynos_cpuidle_driver);
