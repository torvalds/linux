// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb driver for LiIon batteries using TWL4030 madc.
 *
 * Copyright 2013 Golden Delicious Computers
 * Lukas Märdian <lukas@goldelico.com>
 *
 * Based on dumb driver for gta01 battery
 * Copyright 2009 Openmoko, Inc
 * Balaji Rao <balajirrao@openmoko.org>
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/power/twl4030_madc_battery.h>
#include <linux/iio/consumer.h>

struct twl4030_madc_battery {
	struct power_supply *psy;
	struct twl4030_madc_bat_platform_data *pdata;
	struct iio_channel *channel_temp;
	struct iio_channel *channel_ichg;
	struct iio_channel *channel_vbat;
};

static enum power_supply_property twl4030_madc_bat_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
};

static int madc_read(struct iio_channel *channel)
{
	int val, err;
	err = iio_read_channel_processed(channel, &val);
	if (err < 0)
		return err;

	return val;
}

static int twl4030_madc_bat_get_charging_status(struct twl4030_madc_battery *bt)
{
	return (madc_read(bt->channel_ichg) > 0) ? 1 : 0;
}

static int twl4030_madc_bat_get_voltage(struct twl4030_madc_battery *bt)
{
	return madc_read(bt->channel_vbat);
}

static int twl4030_madc_bat_get_current(struct twl4030_madc_battery *bt)
{
	return madc_read(bt->channel_ichg) * 1000;
}

static int twl4030_madc_bat_get_temp(struct twl4030_madc_battery *bt)
{
	return madc_read(bt->channel_temp) * 10;
}

static int twl4030_madc_bat_voltscale(struct twl4030_madc_battery *bat,
					int volt)
{
	struct twl4030_madc_bat_calibration *calibration;
	int i, res = 0;

	/* choose charging curve */
	if (twl4030_madc_bat_get_charging_status(bat))
		calibration = bat->pdata->charging;
	else
		calibration = bat->pdata->discharging;

	if (volt > calibration[0].voltage) {
		res = calibration[0].level;
	} else {
		for (i = 0; calibration[i+1].voltage >= 0; i++) {
			if (volt <= calibration[i].voltage &&
					volt >= calibration[i+1].voltage) {
				/* interval found - interpolate within range */
				res = calibration[i].level -
					((calibration[i].voltage - volt) *
					(calibration[i].level -
					calibration[i+1].level)) /
					(calibration[i].voltage -
					calibration[i+1].voltage);
				break;
			}
		}
	}
	return res;
}

static int twl4030_madc_bat_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct twl4030_madc_battery *bat = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (twl4030_madc_bat_voltscale(bat,
				twl4030_madc_bat_get_voltage(bat)) > 95)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else {
			if (twl4030_madc_bat_get_charging_status(bat))
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = twl4030_madc_bat_get_voltage(bat) * 1000;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = twl4030_madc_bat_get_current(bat);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		/* assume battery is always present */
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW: {
			int percent = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage(bat));
			val->intval = (percent * bat->pdata->capacity) / 100;
			break;
		}
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage(bat));
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = bat->pdata->capacity;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = twl4030_madc_bat_get_temp(bat);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW: {
			int percent = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage(bat));
			/* in mAh */
			int chg = (percent * (bat->pdata->capacity/1000))/100;

			/* assume discharge with 400 mA (ca. 1.5W) */
			val->intval = (3600l * chg) / 400;
			break;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

static void twl4030_madc_bat_ext_changed(struct power_supply *psy)
{
	power_supply_changed(psy);
}

static const struct power_supply_desc twl4030_madc_bat_desc = {
	.name			= "twl4030_battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= twl4030_madc_bat_props,
	.num_properties		= ARRAY_SIZE(twl4030_madc_bat_props),
	.get_property		= twl4030_madc_bat_get_property,
	.external_power_changed	= twl4030_madc_bat_ext_changed,

};

static int twl4030_cmp(const void *a, const void *b)
{
	return ((struct twl4030_madc_bat_calibration *)b)->voltage -
		((struct twl4030_madc_bat_calibration *)a)->voltage;
}

static int twl4030_madc_battery_probe(struct platform_device *pdev)
{
	struct twl4030_madc_battery *twl4030_madc_bat;
	struct twl4030_madc_bat_platform_data *pdata = pdev->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	int ret = 0;

	twl4030_madc_bat = devm_kzalloc(&pdev->dev, sizeof(*twl4030_madc_bat),
				GFP_KERNEL);
	if (!twl4030_madc_bat)
		return -ENOMEM;

	twl4030_madc_bat->channel_temp = iio_channel_get(&pdev->dev, "temp");
	if (IS_ERR(twl4030_madc_bat->channel_temp)) {
		ret = PTR_ERR(twl4030_madc_bat->channel_temp);
		goto err;
	}

	twl4030_madc_bat->channel_ichg = iio_channel_get(&pdev->dev, "ichg");
	if (IS_ERR(twl4030_madc_bat->channel_ichg)) {
		ret = PTR_ERR(twl4030_madc_bat->channel_ichg);
		goto err_temp;
	}

	twl4030_madc_bat->channel_vbat = iio_channel_get(&pdev->dev, "vbat");
	if (IS_ERR(twl4030_madc_bat->channel_vbat)) {
		ret = PTR_ERR(twl4030_madc_bat->channel_vbat);
		goto err_ichg;
	}

	/* sort charging and discharging calibration data */
	sort(pdata->charging, pdata->charging_size,
		sizeof(struct twl4030_madc_bat_calibration),
		twl4030_cmp, NULL);
	sort(pdata->discharging, pdata->discharging_size,
		sizeof(struct twl4030_madc_bat_calibration),
		twl4030_cmp, NULL);

	twl4030_madc_bat->pdata = pdata;
	platform_set_drvdata(pdev, twl4030_madc_bat);
	psy_cfg.drv_data = twl4030_madc_bat;
	twl4030_madc_bat->psy = power_supply_register(&pdev->dev,
						      &twl4030_madc_bat_desc,
						      &psy_cfg);
	if (IS_ERR(twl4030_madc_bat->psy)) {
		ret = PTR_ERR(twl4030_madc_bat->psy);
		goto err_vbat;
	}

	return 0;

err_vbat:
	iio_channel_release(twl4030_madc_bat->channel_vbat);
err_ichg:
	iio_channel_release(twl4030_madc_bat->channel_ichg);
err_temp:
	iio_channel_release(twl4030_madc_bat->channel_temp);
err:
	return ret;
}

static int twl4030_madc_battery_remove(struct platform_device *pdev)
{
	struct twl4030_madc_battery *bat = platform_get_drvdata(pdev);

	power_supply_unregister(bat->psy);

	iio_channel_release(bat->channel_vbat);
	iio_channel_release(bat->channel_ichg);
	iio_channel_release(bat->channel_temp);

	return 0;
}

static struct platform_driver twl4030_madc_battery_driver = {
	.driver = {
		.name = "twl4030_madc_battery",
	},
	.probe  = twl4030_madc_battery_probe,
	.remove = twl4030_madc_battery_remove,
};
module_platform_driver(twl4030_madc_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lukas Märdian <lukas@goldelico.com>");
MODULE_DESCRIPTION("twl4030_madc battery driver");
MODULE_ALIAS("platform:twl4030_madc_battery");
