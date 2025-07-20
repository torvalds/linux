/*
 * Battery driver for LEGO MINDSTORMS EV3
 *
 * Copyright (C) 2017 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>

struct lego_ev3_battery {
	struct iio_channel *iio_v;
	struct iio_channel *iio_i;
	struct gpio_desc *rechargeable_gpio;
	struct power_supply *psy;
	int technology;
	int v_max;
	int v_min;
};

static int lego_ev3_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct lego_ev3_battery *batt = power_supply_get_drvdata(psy);
	int ret, val2;

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = batt->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* battery voltage is iio channel * 2 + Vce of transistor */
		ret = iio_read_channel_processed(batt->iio_v, &val->intval);
		if (ret)
			return ret;

		val->intval *= 2000;
		val->intval += 50000;

		/* plus adjust for shunt resistor drop */
		ret = iio_read_channel_processed(batt->iio_i, &val2);
		if (ret)
			return ret;

		val2 *= 1000;
		val2 /= 15;
		val->intval += val2;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = batt->v_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = batt->v_min;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* battery current is iio channel / 15 / 0.05 ohms */
		ret = iio_read_channel_processed(batt->iio_i, &val->intval);
		if (ret)
			return ret;

		val->intval *= 20000;
		val->intval /= 15;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lego_ev3_battery_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct lego_ev3_battery *batt = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		/*
		 * Only allow changing technology from Unknown to NiMH. Li-ion
		 * batteries are automatically detected and should not be
		 * overridden. Rechargeable AA batteries, on the other hand,
		 * cannot be automatically detected, and so must be manually
		 * specified. This should only be set once during system init,
		 * so there is no mechanism to go back to Unknown.
		 */
		if (batt->technology != POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
			return -EINVAL;
		switch (val->intval) {
		case POWER_SUPPLY_TECHNOLOGY_NiMH:
			batt->technology = POWER_SUPPLY_TECHNOLOGY_NiMH;
			batt->v_max = 7800000;
			batt->v_min = 5400000;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lego_ev3_battery_property_is_writeable(struct power_supply *psy,
						  enum power_supply_property psp)
{
	struct lego_ev3_battery *batt = power_supply_get_drvdata(psy);

	return psp == POWER_SUPPLY_PROP_TECHNOLOGY &&
		batt->technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
}

static enum power_supply_property lego_ev3_battery_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_SCOPE,
};

static const struct power_supply_desc lego_ev3_battery_desc = {
	.name			= "lego-ev3-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= lego_ev3_battery_props,
	.num_properties		= ARRAY_SIZE(lego_ev3_battery_props),
	.get_property		= lego_ev3_battery_get_property,
	.set_property		= lego_ev3_battery_set_property,
	.property_is_writeable	= lego_ev3_battery_property_is_writeable,
};

static int lego_ev3_battery_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lego_ev3_battery *batt;
	struct power_supply_config psy_cfg = {};
	int err;

	batt = devm_kzalloc(dev, sizeof(*batt), GFP_KERNEL);
	if (!batt)
		return -ENOMEM;

	platform_set_drvdata(pdev, batt);

	batt->iio_v = devm_iio_channel_get(dev, "voltage");
	err = PTR_ERR_OR_ZERO(batt->iio_v);
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to get voltage iio channel\n");

	batt->iio_i = devm_iio_channel_get(dev, "current");
	err = PTR_ERR_OR_ZERO(batt->iio_i);
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to get current iio channel\n");

	batt->rechargeable_gpio = devm_gpiod_get(dev, "rechargeable", GPIOD_IN);
	err = PTR_ERR_OR_ZERO(batt->rechargeable_gpio);
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to get rechargeable gpio\n");

	/*
	 * The rechargeable battery indication switch cannot be changed without
	 * removing the battery, so we only need to read it once.
	 */
	if (gpiod_get_value(batt->rechargeable_gpio)) {
		/* 2-cell Li-ion, 7.4V nominal */
		batt->technology = POWER_SUPPLY_TECHNOLOGY_LION;
		batt->v_max = 84000000;
		batt->v_min = 60000000;
	} else {
		/* 6x AA Alkaline, 9V nominal */
		batt->technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		batt->v_max = 90000000;
		batt->v_min = 48000000;
	}

	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	psy_cfg.drv_data = batt;

	batt->psy = devm_power_supply_register(dev, &lego_ev3_battery_desc,
					       &psy_cfg);
	err = PTR_ERR_OR_ZERO(batt->psy);
	if (err) {
		dev_err(dev, "failed to register power supply\n");
		return err;
	}

	return 0;
}

static const struct of_device_id of_lego_ev3_battery_match[] = {
	{ .compatible = "lego,ev3-battery", },
	{ }
};
MODULE_DEVICE_TABLE(of, of_lego_ev3_battery_match);

static struct platform_driver lego_ev3_battery_driver = {
	.driver	= {
		.name		= "lego-ev3-battery",
		.of_match_table = of_lego_ev3_battery_match,
	},
	.probe	= lego_ev3_battery_probe,
};
module_platform_driver(lego_ev3_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("LEGO MINDSTORMS EV3 Battery Driver");
