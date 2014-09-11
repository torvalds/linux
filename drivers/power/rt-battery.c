/*
 *  drivers/power/rt-battery.c
 *  Driver for Richtek RT Test Battery driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/power/rt-battery.h>

struct rt_battery_info {
	struct device *dev;
	struct power_supply psy;
	int chg_status;
	unsigned char batt_present:1;
	unsigned char suspend:1;
};

static enum power_supply_property rt_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int rt_battery_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct rt_battery_info *rbi = dev_get_drvdata(psy->dev->parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		rbi->chg_status = val->intval;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rbi->batt_present = val->intval;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_HEALTH:
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CAPACITY:
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int rt_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct rt_battery_info *rbi = dev_get_drvdata(psy->dev->parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = rbi->chg_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = rbi->batt_present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = 4000 * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (rbi->chg_status == POWER_SUPPLY_STATUS_FULL)
			val->intval = 100;
		else
			val->intval = 50;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int rt_battery_probe(struct platform_device *pdev)
{
	struct rt_battery_info *rbi;
	int ret;

	rbi = devm_kzalloc(&pdev->dev, sizeof(*rbi), GFP_KERNEL);
	if (!rbi)
		return -ENOMEM;
	rbi->dev = &pdev->dev;
	rbi->chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
	rbi->batt_present = 1;
	platform_set_drvdata(pdev, rbi);

	rbi->psy.name = RT_BATT_NAME;
	rbi->psy.type = POWER_SUPPLY_TYPE_BATTERY;
	rbi->psy.set_property = rt_battery_set_property;
	rbi->psy.get_property = rt_battery_get_property;
	rbi->psy.properties = rt_battery_props;
	rbi->psy.num_properties = ARRAY_SIZE(rt_battery_props);
	ret = power_supply_register(&pdev->dev, &rbi->psy);
	if (ret < 0) {
		dev_err(&pdev->dev, "battery supply registered fail\n");
		goto out_dev;
	}
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
out_dev:
	return ret;
}

static int rt_battery_remove(struct platform_device *pdev)
{
	struct rt_battery_info *rbi = platform_get_drvdata(pdev);

	power_supply_unregister(&rbi->psy);
	return 0;
}

static int rt_battery_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt_battery_info *rbi = platform_get_drvdata(pdev);

	rbi->suspend = 1;
	return 0;
}

static int rt_battery_resume(struct platform_device *pdev)
{
	struct rt_battery_info *rbi = platform_get_drvdata(pdev);

	rbi->suspend = 0;
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt-battery",},
	{},
};

static struct platform_driver rt_battery_driver = {
	.driver = {
		   .name = "rt-battery",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt_battery_probe,
	.remove = rt_battery_remove,
	.suspend = rt_battery_suspend,
	.resume = rt_battery_resume,
};

static int __init rt_battery_init(void)
{
	return platform_driver_register(&rt_battery_driver);
}
subsys_initcall(rt_battery_init);

static void __exit rt_battery_exit(void)
{
	platform_driver_unregister(&rt_battery_driver);
}
module_exit(rt_battery_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("RT Test Battery driver");
MODULE_ALIAS("platform:rt-battery");
MODULE_VERSION("1.0.0_G");
