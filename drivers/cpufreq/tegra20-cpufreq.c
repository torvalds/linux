// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
 */

#include <linux/bits.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/types.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

static bool cpu0_node_has_opp_v2_prop(void)
{
	struct device_node *np = of_cpu_device_node_get(0);
	bool ret = false;

	if (of_get_property(np, "operating-points-v2", NULL))
		ret = true;

	of_node_put(np);
	return ret;
}

static void tegra20_cpufreq_put_supported_hw(void *opp_token)
{
	dev_pm_opp_put_supported_hw((unsigned long) opp_token);
}

static void tegra20_cpufreq_dt_unregister(void *cpufreq_dt)
{
	platform_device_unregister(cpufreq_dt);
}

static int tegra20_cpufreq_probe(struct platform_device *pdev)
{
	struct platform_device *cpufreq_dt;
	struct device *cpu_dev;
	u32 versions[2];
	int err;

	if (!cpu0_node_has_opp_v2_prop()) {
		dev_err(&pdev->dev, "operating points not found\n");
		dev_err(&pdev->dev, "please update your device tree\n");
		return -ENODEV;
	}

	if (of_machine_is_compatible("nvidia,tegra20")) {
		versions[0] = BIT(tegra_sku_info.cpu_process_id);
		versions[1] = BIT(tegra_sku_info.soc_speedo_id);
	} else {
		versions[0] = BIT(tegra_sku_info.cpu_process_id);
		versions[1] = BIT(tegra_sku_info.cpu_speedo_id);
	}

	dev_info(&pdev->dev, "hardware version 0x%x 0x%x\n",
		 versions[0], versions[1]);

	cpu_dev = get_cpu_device(0);
	if (WARN_ON(!cpu_dev))
		return -ENODEV;

	err = dev_pm_opp_set_supported_hw(cpu_dev, versions, 2);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set supported hw: %d\n", err);
		return err;
	}

	err = devm_add_action_or_reset(&pdev->dev,
				       tegra20_cpufreq_put_supported_hw,
				       (void *)((unsigned long) err));
	if (err)
		return err;

	cpufreq_dt = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	err = PTR_ERR_OR_ZERO(cpufreq_dt);
	if (err) {
		dev_err(&pdev->dev,
			"failed to create cpufreq-dt device: %d\n", err);
		return err;
	}

	err = devm_add_action_or_reset(&pdev->dev,
				       tegra20_cpufreq_dt_unregister,
				       cpufreq_dt);
	if (err)
		return err;

	return 0;
}

static struct platform_driver tegra20_cpufreq_driver = {
	.probe		= tegra20_cpufreq_probe,
	.driver		= {
		.name	= "tegra20-cpufreq",
	},
};
module_platform_driver(tegra20_cpufreq_driver);

MODULE_ALIAS("platform:tegra20-cpufreq");
MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("NVIDIA Tegra20 cpufreq driver");
MODULE_LICENSE("GPL");
