/*
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Coupled cpuidle support based on the work of:
 *	Colin Cross <ccross@android.com>
 *	Daniel Lezcano <daniel.lezcano@linaro.org>
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
#include <linux/of.h>
#include <linux/platform_data/cpuidle-exynos.h>

#include <asm/suspend.h>
#include <asm/cpuidle.h>

static atomic_t exynos_idle_barrier;

static struct cpuidle_exynos_data *exynos_cpuidle_pdata;
static void (*exynos_enter_aftr)(void);

static int exynos_enter_coupled_lowpower(struct cpuidle_device *dev,
					 struct cpuidle_driver *drv,
					 int index)
{
	int ret;

	exynos_cpuidle_pdata->pre_enter_aftr();

	/*
	 * Waiting all cpus to reach this point at the same moment
	 */
	cpuidle_coupled_parallel_barrier(dev, &exynos_idle_barrier);

	/*
	 * Both cpus will reach this point at the same time
	 */
	ret = dev->cpu ? exynos_cpuidle_pdata->cpu1_powerdown()
		       : exynos_cpuidle_pdata->cpu0_enter_aftr();
	if (ret)
		index = ret;

	/*
	 * Waiting all cpus to finish the power sequence before going further
	 */
	cpuidle_coupled_parallel_barrier(dev, &exynos_idle_barrier);

	exynos_cpuidle_pdata->post_enter_aftr();

	return index;
}

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

static struct cpuidle_driver exynos_coupled_idle_driver = {
	.name			= "exynos_coupled_idle",
	.owner			= THIS_MODULE,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE,
		[1] = {
			.enter			= exynos_enter_coupled_lowpower,
			.exit_latency		= 5000,
			.target_residency	= 10000,
			.flags			= CPUIDLE_FLAG_COUPLED |
						  CPUIDLE_FLAG_TIMER_STOP,
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

	if (IS_ENABLED(CONFIG_SMP) &&
	    of_machine_is_compatible("samsung,exynos4210")) {
		exynos_cpuidle_pdata = pdev->dev.platform_data;

		ret = cpuidle_register(&exynos_coupled_idle_driver,
				       cpu_possible_mask);
	} else {
		exynos_enter_aftr = (void *)(pdev->dev.platform_data);

		ret = cpuidle_register(&exynos_idle_driver, NULL);
	}

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
	},
};

module_platform_driver(exynos_cpuidle_driver);
