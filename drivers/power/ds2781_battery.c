/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based on ds2784_battery.c which is:
 * Copyright (C) 2009 HTC Corporation
 * Copyright (C) 2009 Google, Inc.
 */

#include <linux/android_alarm.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>

#include "../w1/w1.h"
#include "../w1/slaves/w1_ds2781.h"

extern int is_ac_charge_complete(void);

struct battery_status {
	int timestamp;

	int voltage_uV;		/* units of uV */
	int current_uA;		/* units of uA */
	int current_avg_uA;
	int charge_uAh;

	u16 temp_C;		/* units of 0.1 C */

	u8 percentage;		/* battery percentage */
	u8 age_scalar;		/* converted to percent */
	u8 charge_source;
	u8 status_reg;
	u8 battery_full;	/* battery full (don't charge) */

	u8 cooldown;		/* was overtemp */
};


#define TEMP_HOT	450 /* 45.0 degrees Celcius */

#define BATTERY_LOG_MAX 1024
#define BATTERY_LOG_MASK (BATTERY_LOG_MAX - 1)

/* When we're awake or running on wall power, sample the battery
 * gauge every FAST_POLL seconds.  If we're asleep and on battery
 * power, sample every SLOW_POLL seconds
 */
#define FAST_POLL	(1 * 60)
#define SLOW_POLL	(10 * 60)

static DEFINE_MUTEX(battery_log_lock);
static struct battery_status battery_log[BATTERY_LOG_MAX];
static unsigned battery_log_head;
static unsigned battery_log_tail;

static int battery_log_en;
module_param(battery_log_en, int, S_IRUGO | S_IWUSR | S_IWGRP);

void battery_log_status(struct battery_status *s)
{
	unsigned n;
	mutex_lock(&battery_log_lock);
	n = battery_log_head;
	memcpy(battery_log + n, s, sizeof(struct battery_status));
	n = (n + 1) & BATTERY_LOG_MASK;
	if (n == battery_log_tail)
		battery_log_tail = (battery_log_tail + 1) & BATTERY_LOG_MASK;
	battery_log_head = n;
	mutex_unlock(&battery_log_lock);
}

static const char *battery_source[2] = { "none", "  ac" };

static int battery_log_print(struct seq_file *sf, void *private)
{
	unsigned n;
	mutex_lock(&battery_log_lock);
	seq_printf(sf, "timestamp    mV     mA avg mA      uAh   dC   %%   src   reg full\n");
	for (n = battery_log_tail; n != battery_log_head; n = (n + 1) & BATTERY_LOG_MASK) {
		struct battery_status *s = battery_log + n;
		seq_printf(sf, "%9d %5d %6d %6d %8d %4d %3d  %s  0x%02x %d\n",
			   s->timestamp, s->voltage_uV / 1000,
			   s->current_uA / 1000, s->current_avg_uA / 1000,
			   s->charge_uAh, s->temp_C,
			   s->percentage,
			   battery_source[s->charge_source],
			   s->status_reg, s->battery_full);
	}
	mutex_unlock(&battery_log_lock);
	return 0;
}


struct ds2781_device_info {
	struct device *dev;

	/* DS2781 data, valid after calling ds2781_battery_read_status() */
	char raw[DS2781_DATA_SIZE];	/* raw DS2781 data */

	struct battery_status status;
	struct mutex status_lock;

	struct power_supply bat;
	struct device *w1_dev;
	struct workqueue_struct *monitor_wqueue;
	struct work_struct monitor_work;
	struct alarm alarm;
	struct wake_lock work_wake_lock;

	u8 slow_poll;
	ktime_t last_poll;
};

#define psy_to_dev_info(x) container_of((x), struct ds2781_device_info, bat)

static struct wake_lock vbus_wake_lock;

static enum power_supply_property battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
};

#define to_ds2781_device_info(x) container_of((x), struct ds2781_device_info, \
					      bat);

static void ds2781_parse_data(u8 *raw, struct battery_status *s)
{
	short n;

	/* Get status reg */
	s->status_reg = raw[DS2781_REG_STATUS];

	/* Get Level */
	s->percentage = raw[DS2781_REG_RARC];

	/* Get Voltage: Unit=9.76mV, range is 0V to 9.9902V */
	n = (((raw[DS2781_REG_VOLT_MSB] << 8) |
	      (raw[DS2781_REG_VOLT_LSB])) >> 5);

	s->voltage_uV = n * 9760;

	/* Get Current: Unit= 1.5625uV x Rsnsp */
	n = ((raw[DS2781_REG_CURR_MSB]) << 8) |
		raw[DS2781_REG_CURR_LSB];
	s->current_uA = ((n * 15625) / 10000) * raw[DS2781_REG_RSNSP];

	n = ((raw[DS2781_REG_AVG_CURR_MSB]) << 8) |
		raw[DS2781_REG_AVG_CURR_LSB];
	s->current_avg_uA = ((n * 15625) / 10000) * raw[DS2781_REG_RSNSP];

	/* Get Temperature:
	 * Unit=0.125 degree C,therefore, give up LSB ,
	 * just caculate MSB for temperature only.
	 */
	n = (((signed char)raw[DS2781_REG_TEMP_MSB]) << 3) |
		(raw[DS2781_REG_TEMP_LSB] >> 5);

	s->temp_C = n + (n / 4);

	/* RAAC is in units of 1.6mAh */
	s->charge_uAh = ((raw[DS2781_REG_RAAC_MSB] << 8) |
			  raw[DS2781_REG_RAAC_LSB]) * 1600;

	/* Get Age: Unit=0.78125%, range is 49.2% to 100% */
	n = raw[DS2781_REG_AGE_SCALAR];
	s->age_scalar = (n * 78125) / 100000;
}

static int ds2781_battery_read_status(struct ds2781_device_info *di)
{
	int ret;
	int start;
	int count;

	/* The first time we read the entire contents of SRAM/EEPROM,
	 * but after that we just read the interesting bits that change. */
	if (di->raw[DS2781_REG_RSNSP] == 0x00) {
		start = DS2781_REG_STATUS;
		count = DS2781_DATA_SIZE - start;
	} else {
		start = DS2781_REG_STATUS;
		count = DS2781_REG_AGE_SCALAR - start + 1;
	}

	ret = w1_ds2781_read(di->w1_dev, di->raw + start, start, count);
	if (ret != count) {
		dev_warn(di->dev, "call to w1_ds2781_read failed (0x%p)\n",
			 di->w1_dev);
		return 1;
	}

	mutex_lock(&di->status_lock);
	ds2781_parse_data(di->raw, &di->status);

	if (battery_log_en)
		pr_info("batt: %3d%%, %d mV, %d mA (%d avg), %d.%d C, %d mAh\n",
			di->status.percentage,
			di->status.voltage_uV / 1000,
			di->status.current_uA / 1000,
			di->status.current_avg_uA / 1000,
			di->status.temp_C / 10, di->status.temp_C % 10,
			di->status.charge_uAh / 1000);
	mutex_unlock(&di->status_lock);

	return 0;
}

static int battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct ds2781_device_info *di = psy_to_dev_info(psy);
	int retval = 0;

	mutex_lock(&di->status_lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (di->status.charge_source) {
			if ((di->status.battery_full) ||
			    (di->status.percentage >= 100))
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->status.temp_C >= TEMP_HOT)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = di->status.age_scalar;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (di->status.battery_full)
			val->intval = 100;
		else
			val->intval = di->status.percentage;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->status.voltage_uV;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = di->status.temp_C;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->status.current_uA;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->status.current_avg_uA;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = di->status.charge_uAh;
		break;
	default:
		retval = -EINVAL;
	}

	mutex_unlock(&di->status_lock);

	return retval;
}

static void ds2781_battery_update_status(struct ds2781_device_info *di)
{
	u8 last_level;
	last_level = di->status.percentage;

	ds2781_battery_read_status(di);

	if (last_level != di->status.percentage)
		power_supply_changed(&di->bat);
}

static void ds2781_program_alarm(struct ds2781_device_info *di, int seconds)
{
	ktime_t low_interval = ktime_set(seconds - 10, 0);
	ktime_t slack = ktime_set(20, 0);
	ktime_t next;

	next = ktime_add(di->last_poll, low_interval);

	alarm_start_range(&di->alarm, next, ktime_add(next, slack));
}

static void ds2781_battery_work(struct work_struct *work)
{
	struct ds2781_device_info *di =
		container_of(work, struct ds2781_device_info, monitor_work);
	struct timespec ts;

	ds2781_battery_update_status(di);

	di->last_poll = alarm_get_elapsed_realtime();

	ts = ktime_to_timespec(di->last_poll);
	di->status.timestamp = ts.tv_sec;

	if (battery_log_en)
		battery_log_status(&di->status);

	ds2781_program_alarm(di, FAST_POLL);
	wake_unlock(&di->work_wake_lock);
}

static void ds2781_battery_alarm(struct alarm *alarm)
{
	struct ds2781_device_info *di =
		container_of(alarm, struct ds2781_device_info, alarm);
	wake_lock(&di->work_wake_lock);
	queue_work(di->monitor_wqueue, &di->monitor_work);
}

static void battery_ext_power_changed(struct power_supply *psy)
{
	struct ds2781_device_info *di;
	int got_power;

	di = psy_to_dev_info(psy);
	got_power = power_supply_am_i_supplied(psy);

	mutex_lock(&di->status_lock);
	if (got_power) {
		di->status.charge_source = 1;
		if (is_ac_charge_complete())
			di->status.battery_full = 1;

		wake_lock(&vbus_wake_lock);
	} else {
		di->status.charge_source = 0;
		di->status.battery_full = 0;

		/* give userspace some time to see the uevent and update
		 * LED state or whatnot...
		 */
		wake_lock_timeout(&vbus_wake_lock, HZ / 2);
	}
	mutex_unlock(&di->status_lock);

	power_supply_changed(psy);
}

static int ds2781_battery_probe(struct platform_device *pdev)
{
	int rc;
	struct ds2781_device_info *di;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);

	di->dev = &pdev->dev;
	di->w1_dev = pdev->dev.parent;
	mutex_init(&di->status_lock);

	di->bat.name = "battery";
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = battery_properties;
	di->bat.num_properties = ARRAY_SIZE(battery_properties);
	di->bat.external_power_changed = battery_ext_power_changed;
	di->bat.get_property = battery_get_property;

	rc = power_supply_register(&pdev->dev, &di->bat);
	if (rc)
		goto fail_register;

	INIT_WORK(&di->monitor_work, ds2781_battery_work);
	di->monitor_wqueue = create_singlethread_workqueue(dev_name(&pdev->dev));

	/* init to something sane */
	di->last_poll = alarm_get_elapsed_realtime();

	if (!di->monitor_wqueue) {
		rc = -ESRCH;
		goto fail_workqueue;
	}
	wake_lock_init(&di->work_wake_lock, WAKE_LOCK_SUSPEND,
			"ds2781-battery");
	alarm_init(&di->alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			ds2781_battery_alarm);
	wake_lock(&di->work_wake_lock);
	queue_work(di->monitor_wqueue, &di->monitor_work);
	return 0;

fail_workqueue:
	power_supply_unregister(&di->bat);
fail_register:
	kfree(di);
	return rc;
}

static int ds2781_suspend(struct device *dev)
{
	struct ds2781_device_info *di = dev_get_drvdata(dev);

	/* If on battery, reduce update rate until next resume. */
	if (!di->status.charge_source) {
		ds2781_program_alarm(di, SLOW_POLL);
		di->slow_poll = 1;
	}
	return 0;
}

static int ds2781_resume(struct device *dev)
{
	struct ds2781_device_info *di = dev_get_drvdata(dev);

	/* We might be on a slow sample cycle.  If we're
	 * resuming we should resample the battery state
	 * if it's been over a minute since we last did
	 * so, and move back to sampling every minute until
	 * we suspend again.
	 */
	if (di->slow_poll) {
		ds2781_program_alarm(di, FAST_POLL);
		di->slow_poll = 0;
	}
	return 0;
}

static struct dev_pm_ops ds2781_pm_ops = {
	.suspend	= ds2781_suspend,
	.resume		= ds2781_resume,
};

static struct platform_driver ds2781_battery_driver = {
	.driver = {
		.name = "ds2781-battery",
		.pm = &ds2781_pm_ops,
	},
	.probe	  = ds2781_battery_probe,
};

static int battery_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, battery_log_print, NULL);
}

static struct file_operations battery_log_fops = {
	.open = battery_log_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init ds2781_battery_init(void)
{
	debugfs_create_file("battery_log", 0444, NULL, NULL, &battery_log_fops);
	wake_lock_init(&vbus_wake_lock, WAKE_LOCK_SUSPEND, "vbus_present");
	return platform_driver_register(&ds2781_battery_driver);
}

module_init(ds2781_battery_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("ds2781 battery driver");
