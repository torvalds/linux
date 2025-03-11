// SPDX-License-Identifier: GPL-2.0-only
/*
 * INT3401 processor thermal device
 * Copyright (c) 2020, Intel Corporation.
 */
#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "int340x_thermal_zone.h"
#include "processor_thermal_device.h"

static const struct acpi_device_id int3401_device_ids[] = {
	{"INT3401", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, int3401_device_ids);

static int int3401_add(struct platform_device *pdev)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	proc_priv = devm_kzalloc(&pdev->dev, sizeof(*proc_priv), GFP_KERNEL);
	if (!proc_priv)
		return -ENOMEM;

	ret = proc_thermal_add(&pdev->dev, proc_priv);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, proc_priv);

	return ret;
}

static void int3401_remove(struct platform_device *pdev)
{
	proc_thermal_remove(platform_get_drvdata(pdev));
}

#ifdef CONFIG_PM_SLEEP
static int int3401_thermal_suspend(struct device *dev)
{
	return proc_thermal_suspend(dev);
}
static int int3401_thermal_resume(struct device *dev)
{
	return proc_thermal_resume(dev);
}
#else
#define int3401_thermal_suspend NULL
#define int3401_thermal_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(int3401_proc_thermal_pm, int3401_thermal_suspend,
			 int3401_thermal_resume);

static struct platform_driver int3401_driver = {
	.probe = int3401_add,
	.remove = int3401_remove,
	.driver = {
		.name = "int3401 thermal",
		.acpi_match_table = int3401_device_ids,
		.pm = &int3401_proc_thermal_pm,
	},
};

module_platform_driver(int3401_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
