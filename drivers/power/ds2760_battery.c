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
	struct delayed_work set_charged_work;
};

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

static unsigned int pmod_enabled;
module_param(pmod_enabled, bool, 0644);
MODULE_PARM_DESC(pmod_enabled, "PMOD enable bit");

static unsigned int rated_capacity;
module_param(rated_capacity, uint, 0644);
MODULE_PARM_DESC(rated_capacity, "rated battery capacity, 10*mAh or index");

static unsigned int current_accum;
module_param(current_accum, uint, 0644);
MODULE_PARM_DESC(current_accum, "current accumulator value");

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

	/* If the full_active_uAh value is not given, fall back to the rated
	 * capacity. This is likely to happen when chips are not part of the
	 * battery pack and is therefore not bootstrapped. */
	if (di->full_active_uAh == 0)
		di->full_active_uAh = di->rated_capacity / 1000L;

	scale[0] = di->full_active_uAh;
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

	if (di->full_active_uAh == di->empty_uAh)
		di->rem_capacity = 0;
	else
		/* From Maxim Application Note 131: remaining capacity =
		 * ((ICA - Empty Value) / (Full Value - Empty Value)) x 100% */
		di->rem_capacity = ((di->accum_current_uAh - di->empty_uAh) * 100L) /
				    (di->full_active_uAh - di->empty_uAh);

	if (di->rem_capacity < 0)
		di->rem_capacity = 0;
	if (di->rem_capacity > 100)
		di->rem_capacity = 100;

	if (di->current_uA >= 100L)
		di->life_sec = -((di->accum_current_uAh - di->empty_uAh) * 36L)
					/ (di->current_uA / 100L);
	else
		di->life_sec = 0;

	return 0;
}

static void ds2760_battery_set_current_accum(struct ds2760_device_info *di,
					     unsigned int acr_val)
{
	unsigned char acr[2];

	/* acr is in units of 0.25 mAh */
	acr_val *= 4L;
	acr_val /= 1000;

	acr[0] = acr_val >> 8;
	acr[1] = acr_val & 0xff;

	if (w1_ds2760_write(di->w1_dev, acr, DS2760_CURRENT_ACCUM_MSB, 2) < 2)
		dev_warn(di->dev, "ACR write failed\n");
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
				di->charge_status = POWER_SUPPLY_STATUS_FULL;
				ds2760_battery_set_current_accum(di,
						di->full_active_uAh);
			}
		}
	} else {
		di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		di->full_counter = 0;
	}

	if (di->charge_status != old_charge_status)
		power_supply_changed(&di->bat);
}

static void ds2760_battery_write_status(struct ds2760_device_info *di,
					char status)
{
	if (status == di->raw[DS2760_STATUS_REG])
		return;

	w1_ds2760_write(di->w1_dev, &status, DS2760_STATUS_WRITE_REG, 1);
	w1_ds2760_store_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);
}

static void ds2760_battery_write_rated_capacity(struct ds2760_device_info *di,
						unsigned char rated_capacity)
{
	if (rated_capacity == di->raw[DS2760_RATED_CAPACITY])
		return;

	w1_ds2760_write(di->w1_dev, &rated_capacity, DS2760_RATED_CAPACITY, 1);
	w1_ds2760_store_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);
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


static void ds2760_battery_set_charged_work(struct work_struct *work)
{
	char bias;
	struct ds2760_device_info *di = container_of(work,
		struct ds2760_device_info, set_charged_work.work);

	dev_dbg(di->dev, "%s\n", __func__);

	ds2760_battery_read_status(di);

	/* When we get notified by external circuitry that the battery is
	 * considered fully charged now, we know that there is no current
	 * flow any more. However, the ds2760's internal current meter is
	 * too inaccurate to rely on - spec say something ~15% failure.
	 * Hence, we use the current offset bias register to compensate
	 * that error.
	 */

	if (!power_supply_am_i_supplied(&di->bat))
		return;

	bias = (signed char) di->current_raw +
		(signed char) di->raw[DS2760_CURRENT_OFFSET_BIAS];

	dev_dbg(di->dev, "%s: bias = %d\n", __func__, bias);

	w1_ds2760_write(di->w1_dev, &bias, DS2760_CURRENT_OFFSET_BIAS, 1);
	w1_ds2760_store_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->w1_dev, DS2760_EEPROM_BLOCK1);

	/* Write to the di->raw[] buffer directly - the CURRENT_OFFSET_BIAS
	 * value won't be read back by ds2760_battery_read_status() */
	di->raw[DS2760_CURRENT_OFFSET_BIAS] = bias;
}

static void ds2760_battery_set_charged(struct power_supply *psy)
{
	struct ds2760_device_info *di = to_ds2760_device_info(psy);

	/* postpone the actual work by 20 secs. This is for debouncing GPIO
	 * signals and to let the current value settle. See AN4188. */
	cancel_delayed_work(&di->set_charged_work);
	queue_delayed_work(di->monitor_wqueue, &di->set_charged_work, HZ * 20);
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
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		val->intval = di->life_sec;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->rem_capacity;
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
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int ds2760_battery_probe(struct platform_device *pdev)
{
	char status;
	int retval = 0;
	struct ds2760_device_info *di;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		retval = -ENOMEM;
		goto di_alloc_failed;
	}

	platform_set_drvdata(pdev, di);

	di->dev			= &pdev->dev;
	di->w1_dev		= pdev->dev.parent;
	di->bat.name		= dev_name(&pdev->dev);
	di->bat.type		= POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties	= ds2760_battery_props;
	di->bat.num_properties	= ARRAY_SIZE(ds2760_battery_props);
	di->bat.get_property	= ds2760_battery_get_property;
	di->bat.set_charged	= ds2760_battery_set_charged;
	di->bat.external_power_changed =
				  ds2760_battery_external_power_changed;

	di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;

	/* enable sleep mode feature */
	ds2760_battery_read_status(di);
	status = di->raw[DS2760_STATUS_REG];
	if (pmod_enabled)
		status |= DS2760_STATUS_PMOD;
	else
		status &= ~DS2760_STATUS_PMOD;

	ds2760_battery_write_status(di, status);

	/* set rated capacity from module param */
	if (rated_capacity)
		ds2760_battery_write_rated_capacity(di, rated_capacity);

	/* set current accumulator if given as parameter.
	 * this should only be done for bootstrapping the value */
	if (current_accum)
		ds2760_battery_set_current_accum(di, current_accum);

	retval = power_supply_register(&pdev->dev, &di->bat);
	if (retval) {
		dev_err(di->dev, "failed to register battery\n");
		goto batt_failed;
	}

	INIT_DELAYED_WORK(&di->monitor_work, ds2760_battery_work);
	INIT_DELAYED_WORK(&di->set_charged_work,
			  ds2760_battery_set_charged_work);
	di->monitor_wqueue = create_singlethread_workqueue(dev_name(&pdev->dev));
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
	cancel_rearming_delayed_workqueue(di->monitor_wqueue,
					  &di->set_charged_work);
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
