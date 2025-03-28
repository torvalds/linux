// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power off driver for ams AS3722 device.
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/mfd/as3722.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>

struct as3722_poweroff {
	struct device *dev;
	struct as3722 *as3722;
};

static int as3722_pm_power_off(struct sys_off_data *data)
{
	struct as3722_poweroff *as3722_pm_poweroff = data->cb_data;
	int ret;

	ret = as3722_update_bits(as3722_pm_poweroff->as3722,
		AS3722_RESET_CONTROL_REG, AS3722_POWER_OFF, AS3722_POWER_OFF);
	if (ret < 0)
		dev_err(as3722_pm_poweroff->dev,
			"RESET_CONTROL_REG update failed, %d\n", ret);

	return NOTIFY_DONE;
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

	return devm_register_sys_off_handler(as3722_poweroff->dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_DEFAULT,
					     as3722_pm_power_off,
					     as3722_poweroff);
}

static struct platform_driver as3722_poweroff_driver = {
	.driver = {
		.name = "as3722-power-off",
	},
	.probe = as3722_poweroff_probe,
};

module_platform_driver(as3722_poweroff_driver);

MODULE_DESCRIPTION("Power off driver for ams AS3722 PMIC Device");
MODULE_ALIAS("platform:as3722-power-off");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
