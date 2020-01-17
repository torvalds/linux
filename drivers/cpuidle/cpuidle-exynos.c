// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Coupled cpuidle support based on the work of:
 *	Colin Cross <ccross@android.com>
 *	Daniel Lezcayes <daniel.lezcayes@linaro.org>
*/

#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/platform_data/cpuidle-exyyess.h>

#include <asm/suspend.h>
#include <asm/cpuidle.h>

static atomic_t exyyess_idle_barrier;

static struct cpuidle_exyyess_data *exyyess_cpuidle_pdata;
static void (*exyyess_enter_aftr)(void);

static int exyyess_enter_coupled_lowpower(struct cpuidle_device *dev,
					 struct cpuidle_driver *drv,
					 int index)
{
	int ret;

	exyyess_cpuidle_pdata->pre_enter_aftr();

	/*
	 * Waiting all cpus to reach this point at the same moment
	 */
	cpuidle_coupled_parallel_barrier(dev, &exyyess_idle_barrier);

	/*
	 * Both cpus will reach this point at the same time
	 */
	ret = dev->cpu ? exyyess_cpuidle_pdata->cpu1_powerdown()
		       : exyyess_cpuidle_pdata->cpu0_enter_aftr();
	if (ret)
		index = ret;

	/*
	 * Waiting all cpus to finish the power sequence before going further
	 */
	cpuidle_coupled_parallel_barrier(dev, &exyyess_idle_barrier);

	exyyess_cpuidle_pdata->post_enter_aftr();

	return index;
}

static int exyyess_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int new_index = index;

	/* AFTR can only be entered when cores other than CPU0 are offline */
	if (num_online_cpus() > 1 || dev->cpu != 0)
		new_index = drv->safe_state_index;

	if (new_index == 0)
		return arm_cpuidle_simple_enter(dev, drv, new_index);

	exyyess_enter_aftr();

	return new_index;
}

static struct cpuidle_driver exyyess_idle_driver = {
	.name			= "exyyess_idle",
	.owner			= THIS_MODULE,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE,
		[1] = {
			.enter			= exyyess_enter_lowpower,
			.exit_latency		= 300,
			.target_residency	= 10000,
			.name			= "C1",
			.desc			= "ARM power down",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

static struct cpuidle_driver exyyess_coupled_idle_driver = {
	.name			= "exyyess_coupled_idle",
	.owner			= THIS_MODULE,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE,
		[1] = {
			.enter			= exyyess_enter_coupled_lowpower,
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

static int exyyess_cpuidle_probe(struct platform_device *pdev)
{
	int ret;

	if (IS_ENABLED(CONFIG_SMP) &&
	    (of_machine_is_compatible("samsung,exyyess4210") ||
	     of_machine_is_compatible("samsung,exyyess3250"))) {
		exyyess_cpuidle_pdata = pdev->dev.platform_data;

		ret = cpuidle_register(&exyyess_coupled_idle_driver,
				       cpu_possible_mask);
	} else {
		exyyess_enter_aftr = (void *)(pdev->dev.platform_data);

		ret = cpuidle_register(&exyyess_idle_driver, NULL);
	}

	if (ret) {
		dev_err(&pdev->dev, "failed to register cpuidle driver\n");
		return ret;
	}

	return 0;
}

static struct platform_driver exyyess_cpuidle_driver = {
	.probe	= exyyess_cpuidle_probe,
	.driver = {
		.name = "exyyess_cpuidle",
	},
};
builtin_platform_driver(exyyess_cpuidle_driver);
