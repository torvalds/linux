/*
 * Power off driver for ams AS3722 device.
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/mfd/as3722.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct as3722_poweroff {
	struct device *dev;
	struct as3722 *as3722;
};

static struct as3722_poweroff *as3722_pm_poweroff;

static void as3722_pm_power_off(void)
{
	int ret;

	if (!as3722_pm_poweroff) {
		pr_err("AS3722 poweroff is not initialised\n");
		return;
	}

	ret = as3722_update_bits(as3722_pm_poweroff->as3722,
		AS3722_RESET_CONTROL_REG, AS3722_POWER_OFF, AS3722_POWER_OFF);
	if (ret < 0)
		dev_err(as3722_pm_poweroff->dev,
			"RESET_CONTROL_REG update failed, %d\n", ret);
}

static int as3722_poweroff_probe(struct platform_device *pdev)
{
	struct as3722_poweroff *as3722_poweroff;
	struct device_node *np = pdev->dev.parent->of_node;

	if (!np)
		return -EINVAL;

	if (!of_property_read_bool(np, "ams,system-power-controller"))
		return 0;

	as3722_poweroff = devm_kzalloc(&pdev->dev, sizeof(*as3722_poweroff),
				GFP_KERNEL);
	if (!as3722_poweroff)
		return -ENOMEM;

	as3722_poweroff->as3722 = dev_get_drvdata(pdev->dev.parent);
	as3722_poweroff->dev = &pdev->dev;
	as3722_pm_poweroff = as3722_poweroff;
	if (!pm_power_off)
		pm_power_off = as3722_pm_power_off;

	return 0;
}

static int as3722_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == as3722_pm_power_off)
		pm_power_off = NULL;
	as3722_pm_poweroff = NULL;

	return 0;
}

static struct platform_driver as3722_poweroff_driver = {
	.driver = {
		.name = "as3722-power-off",
		.owner = THIS_MODULE,
	},
	.probe = as3722_poweroff_probe,
	.remove = as3722_poweroff_remove,
};

module_platform_driver(as3722_poweroff_driver);

MODULE_DESCRIPTION("Power off driver for ams AS3722 PMIC Device");
MODULE_ALIAS("platform:as3722-power-off");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
