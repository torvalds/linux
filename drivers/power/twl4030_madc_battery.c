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
#include <linux/i2c/twl4030-madc.h>
#include <linux/power/twl4030_madc_battery.h>

struct twl4030_madc_battery {
	struct power_supply psy;
	struct twl4030_madc_bat_platform_data *pdata;
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

static int madc_read(int index)
{
	struct twl4030_madc_request req;
	int val;

	req.channels = index;
	req.method = TWL4030_MADC_SW2;
	req.type = TWL4030_MADC_WAIT;
	req.do_avg = 0;
	req.raw = false;
	req.func_cb = NULL;

	val = twl4030_madc_conversion(&req);
	if (val < 0)
		return val;

	return req.rbuf[ffs(index) - 1];
}

static int twl4030_madc_bat_get_charging_status(void)
{
	return (madc_read(TWL4030_MADC_ICHG) > 0) ? 1 : 0;
}

static int twl4030_madc_bat_get_voltage(void)
{
	return madc_read(TWL4030_MADC_VBAT);
}

static int twl4030_madc_bat_get_current(void)
{
	return madc_read(TWL4030_MADC_ICHG) * 1000;
}

static int twl4030_madc_bat_get_temp(void)
{
	return madc_read(TWL4030_MADC_BTEMP) * 10;
}

static int twl4030_madc_bat_voltscale(struct twl4030_madc_battery *bat,
					int volt)
{
	struct twl4030_madc_bat_calibration *calibration;
	int i, res = 0;

	/* choose charging curve */
	if (twl4030_madc_bat_get_charging_status())
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
	struct twl4030_madc_battery *bat = container_of(psy,
					struct twl4030_madc_battery, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (twl4030_madc_bat_voltscale(bat,
				twl4030_madc_bat_get_voltage()) > 95)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else {
			if (twl4030_madc_bat_get_charging_status())
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = twl4030_madc_bat_get_voltage() * 1000;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = twl4030_madc_bat_get_current();
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		/* assume battery is always present */
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW: {
			int percent = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage());
			val->intval = (percent * bat->pdata->capacity) / 100;
			break;
		}
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage());
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = bat->pdata->capacity;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = twl4030_madc_bat_get_temp();
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW: {
			int percent = twl4030_madc_bat_voltscale(bat,
					twl4030_madc_bat_get_voltage());
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
	struct twl4030_madc_battery *bat = container_of(psy,
					struct twl4030_madc_battery, psy);

	power_supply_changed(&bat->psy);
}

static int twl4030_cmp(const void *a, const void *b)
{
	return ((struct twl4030_madc_bat_calibration *)b)->voltage -
		((struct twl4030_madc_bat_calibration *)a)->voltage;
}

static int twl4030_madc_battery_probe(struct platform_device *pdev)
{
	struct twl4030_madc_battery *twl4030_madc_bat;
	struct twl4030_madc_bat_platform_data *pdata = pdev->dev.platform_data;

	twl4030_madc_bat = kzalloc(sizeof(*twl4030_madc_bat), GFP_KERNEL);
	if (!twl4030_madc_bat)
		return -ENOMEM;

	twl4030_madc_bat->psy.name = "twl4030_battery";
	twl4030_madc_bat->psy.type = POWER_SUPPLY_TYPE_BATTERY;
	twl4030_madc_bat->psy.properties = twl4030_madc_bat_props;
	twl4030_madc_bat->psy.num_properties =
					ARRAY_SIZE(twl4030_madc_bat_props);
	twl4030_madc_bat->psy.get_property = twl4030_madc_bat_get_property;
	twl4030_madc_bat->psy.external_power_changed =
					twl4030_madc_bat_ext_changed;

	/* sort charging and discharging calibration data */
	sort(pdata->charging, pdata->charging_size,
		sizeof(struct twl4030_madc_bat_calibration),
		twl4030_cmp, NULL);
	sort(pdata->discharging, pdata->discharging_size,
		sizeof(struct twl4030_madc_bat_calibration),
		twl4030_cmp, NULL);

	twl4030_madc_bat->pdata = pdata;
	platform_set_drvdata(pdev, twl4030_madc_bat);
	power_supply_register(&pdev->dev, &twl4030_madc_bat->psy);

	return 0;
}

static int twl4030_madc_battery_remove(struct platform_device *pdev)
{
	struct twl4030_madc_battery *bat = platform_get_drvdata(pdev);

	power_supply_unregister(&bat->psy);
	kfree(bat);

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
