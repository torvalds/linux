/*
 * Driver for batteries with DS2760 chips inside.
 *
 * Copyright © 2007 Anton Vorontsov
 *	       2004-2007 Matt Reimer
 *	       2004 Szabolcs Gyurko
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * Author:  Anton Vorontsov <cbou@mail.ru>
 *	    February 2007
 *
 *	    Matt Reimer <mreimer@vpop.net>
 *	    April 2004, 2005, 2007
 *
 *	    Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 *	    September 2004
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include "../w1/w1.h"
#include "../w1/slaves/w1_ds2760.h"

struct ds2760_device_info {
	struct device *dev;

	/* DS2760 data, valid after calling ds2760_battery_read_status() */
	unsigned long update_time;	/* jiffies when data read */
	char raw[DS2760_DATA_SIZE];	/* raw DS2760 data */
	int voltage_raw;		/* units of 4.88 mV */
	int voltage_uV;			/* units of µV */
	int current_raw;		/* units of 0.625 mA */
	int current_uA;			/* units of µA */
	int accum_current_raw;		/* units of 0.25 mAh */
	int accum_current_uAh;		/* units of µAh */
	int temp_raw;			/* units of 0.125 °C */
	int temp_C;			/* units of 0.1 °C */
	int rated_capacity;		/* units of µAh */
	int rem_capacity;		/* percentage */
	int full_active_uAh;		/* units of µAh */
	int empty_uAh;			/* units of µAh */
	int life_sec;			/* units of seconds */
	int charge_status;		/* POWER_SUPPLY_STATUS_* */

	int full_counter;
	struct power_supply bat;
	struct device *w1_dev;
	struct workqueue_struct *monitor_wqueue;
	struct delayed_work monitor_work;
};

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

/* Some batteries have their rated capacity stored a N * 10 mAh, while
 * others use an index into this table. */
static int rated_capacities[] = {
	0,
	920,	/* Samsung */
	920,	/* BYD */
	920,	/* Lishen */
	920,	/* NEC */
	1440,	/* Samsung */
	1440,	/* BYD */
	1440,	/* Lishen */
	1440,	/* NEC */
	2880,	/* Samsung */
	2880,	/* BYD */
	2880,	/* Lishen */
	2880	/* NEC */
};

/* array is level at temps 0°C, 10°C, 20°C, 30°C, 40°C
 * temp is in Celsius */
static int battery_interpolate(int array[], int temp)
{
	int index, dt;

	if (temp <= 0)
		return array[0];
	if (temp >= 40)
		return array[4];

	index = temp / 10;
	dt    = temp % 10;

	return array[index] + (((array[index + 1] - array[index]) * dt) / 10);
}

static int ds2760_battery_read_status(struct ds2760_device_info *di)
{
	int ret, i, start, count, scale[5];

	if (di->update_time && time_before(jiffies, di->update_time +
					   msecs_to_jiffies(cache_time)))
		return 0;

	/* The first time we read the entire contents of SRAM/EEPROM,
	 * but after that we just read the interesting bits that change. */
	if (di->update_time == 0) {
		start = 0;
		count = DS2760_DATA_SIZE;
	} else {
		start = DS2760_VOLTAGE_MSB;
		count = DS2760_TEMP_LSB - start + 1;
	}

	ret = w1_ds2760_read(di->w1_dev, di->raw + start, start, count);
	if (ret != count) {
		dev_warn(di->dev, "call to w1_ds2760_read failed (0x%p)\n",
			 di->w1_dev);
		return 1;
	}

	di->update_time = jiffies;

	/* DS2760 reports voltage in units of 4.88mV, but the battery class
	 * reports in units of uV, so convert by multiplying by 4880. */
	di->voltage_raw = (di->raw[DS2760_VOLTAGE_MSB] << 3) |
			  (di->raw[DS2760_VOLTAGE_LSB] >> 5);
	di->voltage_uV = di->voltage_raw * 4880;

	/* DS2760 reports current in signed units of 0.625mA, but the battery
	 * class reports in units of µA, so convert by multiplying by 625. */
	di->current_raw =
	    (((signed char)di->raw[DS2760_CURRENT_MSB]) << 5) |
			  (di->raw[DS2760_CURRENT_LSB] >> 3);
	di->current_uA = di->current_raw * 625;

	/* DS2760 reports accumulated current in signed units of 0.25mAh. */
	di->accum_current_raw =
	    (((signed char)di->raw[DS2760_CURRENT_ACCUM_MSB]) << 8) |
			   di->raw[DS2760_CURRENT_ACCUM_LSB];
	di->accum_current_uAh = di->accum_current_raw * 250;

	/* DS2760 reports temperature in signed units of 0.125°C, but the
	 * battery class reports in units of 1/10 °C, so we convert by
	 * multiplying by .125 * 10 = 1.25. */
	di->temp_raw = (((signed char)di->raw[DS2760_TEMP_MSB]) << 3) |
				     (di->raw[DS2760_TEMP_LSB] >> 5);
	di->temp_C = di->temp_raw + (di->temp_raw / 4);

	/* At least some battery monitors (e.g. HP iPAQ) store the battery's
	 * maximum rated capacity. */
	if (di->raw[DS2760_RATED_CAPACITY] < ARRAY_SIZE(rated_capacities))
		di->rated_capacity = rated_capacities[
			(unsigned int)di->raw[DS2760_RATED_CAPACITY]];
	else
		di->rated_capacity = di->raw[DS2760_RATED_CAPACITY] * 10;

	di->rated_capacity *= 1000; /* convert to µAh */

	/* Calculate the full level at the present temperature. */
	di->full_active_uAh = di->raw[DS2760_ACTIVE_FULL] << 8 |
			      di->raw[DS2760_ACTIVE_FULL + 1];

	scale[0] = di->raw[DS2760_ACTIVE_FULL] << 8 |
		   di->raw[DS2760_ACTIVE_FULL + 1];
	for (i = 1; i < 5; i++)
		scale[i] = scale[i - 1] + di->raw[DS2760_ACTIVE_FULL + 2 + i];

	di->full_active_uAh = battery_interpolate(scale, di->temp_C / 10);
	di->full_active_uAh *= 1000; /* convert to µAh */

	/* Calculate the empty level at the present temperature. */
	scale[4] = di->raw[DS2760_ACTIVE_EMPTY + 4];
	for (i = 3; i >= 0; i--)
		scale[i] = scale[i + 1] + di->raw[DS2760_ACTIVE_EMPTY + i];

	di->empty_uAh = battery_interpolate(scale, di->temp_C / 10);
	di->empty_uAh *= 1000; /* convert to µAh */

	/* From Maxim Application Note 131: remaining capacity =
	 * ((ICA - Empty Value) / (Full Value - Empty Value)) x 100% */
	di->rem_capacity = ((di->accum_current_uAh - di->empty_uAh) * 100L) /
			    (di->full_active_uAh - di->empty_uAh);

	if (di->rem_capacity < 0)
		di->rem_capacity = 0;
	if (di->rem_capacity > 100)
		di->rem_capacity = 100;

	if (di->current_uA)
		di->life_sec = -((di->accum_current_uAh - di->empty_uAh) *
				 3600L) / di->current_uA;
	else
		di->life_sec = 0;

	return 0;
}

static void ds2760_battery_update_status(struct ds2760_device_info *di)
{
	int old_charge_status = di->charge_status;

	ds2760_battery_read_status(di);

	if (di->charge_status == POWER_SUPPLY_STATUS_UNKNOWN)
		di->full_counter = 0;

	if (power_supply_am_i_supplied(&di->bat)) {
		if (di->current_uA > 10000) {
			di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			di->full_counter = 0;
		} else if (di->current_uA < -5000) {
			if (di->charge_status != POWER_SUPPLY_STATUS_NOT_CHARGING)
				dev_notice(di->dev, "not enough power to "
					   "charge\n");
			di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			di->full_counter = 0;
		} else if (di->current_uA < 10000 &&
			    di->charge_status != POWER_SUPPLY_STATUS_FULL) {

			/* Don't consider the battery to be full unless
			 * we've seen the current < 10 mA at least two
			 * consecutive times. */

			di->full_counter++;

			if (di->full_counter < 2) {
				di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			} else {
				unsigned char acr[2];
				int acr_val;

				/* acr is in units of 0.25 mAh */
				acr_val = di->full_active_uAh * 4L / 1000;

				acr[0] = acr_val >> 8;
				acr[1] = acr_val & 0xff;

				if (w1_ds2760_write(di->w1_dev, acr,
				    DS2760_CURRENT_ACCUM_MSB, 2) < 2)
					dev_warn(di->dev,
						 "ACR reset failed\n");

				di->charge_status = POWER_SUPPLY_STATUS_FULL;
			}
		}
	} else {
		di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		di->full_counter = 0;
	}

	if (di->charge_status != old_charge_status)
		power_supply_changed(&di->bat);
}

static void ds2760_battery_work(struct work_struct *work)
{
	struct ds2760_device_info *di = container_of(work,
		struct ds2760_device_info, monitor_work.work);
	const int interval = HZ * 60;

	dev_dbg(di->dev, "%s\n", __func__);

	ds2760_battery_update_status(di);
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, interval);
}

#define to_ds2760_device_info(x) container_of((x), struct ds2760_device_info, \
					      bat);

static void ds2760_battery_external_power_changed(struct power_supply *psy)
{
	struct ds2760_device_info *di = to_ds2760_device_info(psy);

	dev_dbg(di->dev, "%s\n", __func__);

	cancel_delayed_work(&di->monitor_work);
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ/10);
}

static int ds2760_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct ds2760_device_info *di = to_ds2760_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->charge_status;
		return 0;
	default:
		break;
	}

	ds2760_battery_read_status(di);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage_uV;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_uA;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = di->rated_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = di->full_active_uAh;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = di->empty_uAh;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = di->accum_current_uAh;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = di->temp_C;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property ds2760_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static int ds2760_battery_probe(struct platform_device *pdev)
{
	int retval = 0;
	struct ds2760_device_info *di;
	struct ds2760_platform_data *pdata;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		retval = -ENOMEM;
		goto di_alloc_failed;
	}

	platform_set_drvdata(pdev, di);

	pdata = pdev->dev.platform_data;
	di->dev		= &pdev->dev;
	di->w1_dev	     = pdev->dev.parent;
	di->bat.name	   = pdev->dev.bus_id;
	di->bat.type	   = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties     = ds2760_battery_props;
	di->bat.num_properties = ARRAY_SIZE(ds2760_battery_props);
	di->bat.get_property   = ds2760_battery_get_property;
	di->bat.external_power_changed =
				  ds2760_battery_external_power_changed;

	di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;

	retval = power_supply_register(&pdev->dev, &di->bat);
	if (retval) {
		dev_err(di->dev, "failed to register battery\n");
		goto batt_failed;
	}

	INIT_DELAYED_WORK(&di->monitor_work, ds2760_battery_work);
	di->monitor_wqueue = create_singlethread_workqueue(pdev->dev.bus_id);
	if (!di->monitor_wqueue) {
		retval = -ESRCH;
		goto workqueue_failed;
	}
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ * 1);

	goto success;

workqueue_failed:
	power_supply_unregister(&di->bat);
batt_failed:
	kfree(di);
di_alloc_failed:
success:
	return retval;
}

static int ds2760_battery_remove(struct platform_device *pdev)
{
	struct ds2760_device_info *di = platform_get_drvdata(pdev);

	cancel_rearming_delayed_workqueue(di->monitor_wqueue,
					  &di->monitor_work);
	destroy_workqueue(di->monitor_wqueue);
	power_supply_unregister(&di->bat);

	return 0;
}

#ifdef CONFIG_PM

static int ds2760_battery_suspend(struct platform_device *pdev,
				  pm_message_t state)
{
	struct ds2760_device_info *di = platform_get_drvdata(pdev);

	di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;

	return 0;
}

static int ds2760_battery_resume(struct platform_device *pdev)
{
	struct ds2760_device_info *di = platform_get_drvdata(pdev);

	di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
	power_supply_changed(&di->bat);

	cancel_delayed_work(&di->monitor_work);
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ);

	return 0;
}

#else

#define ds2760_battery_suspend NULL
#define ds2760_battery_resume NULL

#endif /* CONFIG_PM */

MODULE_ALIAS("platform:ds2760-battery");

static struct platform_driver ds2760_battery_driver = {
	.driver = {
		.name = "ds2760-battery",
	},
	.probe	  = ds2760_battery_probe,
	.remove   = ds2760_battery_remove,
	.suspend  = ds2760_battery_suspend,
	.resume	  = ds2760_battery_resume,
};

static int __init ds2760_battery_init(void)
{
	return platform_driver_register(&ds2760_battery_driver);
}

static void __exit ds2760_battery_exit(void)
{
	platform_driver_unregister(&ds2760_battery_driver);
}

module_init(ds2760_battery_init);
module_exit(ds2760_battery_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>, "
	      "Matt Reimer <mreimer@vpop.net>, "
	      "Anton Vorontsov <cbou@mail.ru>");
MODULE_DESCRIPTION("ds2760 battery driver");
