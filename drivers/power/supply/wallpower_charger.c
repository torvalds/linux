// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

struct wall_charger_type {
	struct power_supply_desc wall_desc;
	struct power_supply_config wall_cfg;
	enum power_supply_usb_type type;
	struct power_supply *wall_psy;
};

static enum power_supply_property wall_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static int wall_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct wall_charger_type *info;

	info = (struct wall_charger_type *)power_supply_get_drvdata(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int wallpower_probe(struct platform_device *pdev)
{
	struct wall_charger_type *info;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->wall_desc.name = "Ac";
	info->wall_desc.type = POWER_SUPPLY_TYPE_MAINS;
	info->wall_desc.properties = wall_properties;
	info->wall_desc.num_properties = ARRAY_SIZE(wall_properties);
	info->wall_desc.get_property = wall_get_property;
	info->wall_cfg.drv_data = info;

	platform_set_drvdata(pdev, info);
	info->wall_psy = power_supply_register(&pdev->dev,
				&info->wall_desc, &info->wall_cfg);

	if (IS_ERR(info->wall_psy)) {
		pr_notice("%s Failed to register power supply: %ld\n",
					__func__, PTR_ERR(info->wall_psy));
		return PTR_ERR(info->wall_psy);
	}
	return 0;
}

static int wallpower_remove(struct platform_device *pdev)
{
	struct wall_charger_type *wall_type = platform_get_drvdata(pdev);

	power_supply_unregister(wall_type->wall_psy);
	return 0;
}

static struct platform_device wallpower_devices = {
	.name	= "power",
	.id	= 0,
};

static struct platform_driver wallpower_driver = {
	.probe  = wallpower_probe,
	.remove = wallpower_remove,
	.driver = {
		.name = "power",
	},
};

static int __init wallpower_init(void)
{
	platform_device_register(&wallpower_devices);
	platform_driver_register(&wallpower_driver);
	return 0;
}

static void __exit wallpower_exit(void)
{
	platform_device_unregister(&wallpower_devices);
	platform_driver_unregister(&wallpower_driver);
}

module_init(wallpower_init);
module_exit(wallpower_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for enabling Wallpower power source");
