/*
 *  android_battery.c
 *  Android Battery Driver
 *
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Samsung Electronics
 *
 * Based on work by himihee.seo@samsung.com, ms925.kim@samsung.com, and
 * joshua.chang@samsung.com.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/alarmtimer.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/platform_data/android_battery.h>

#define FAST_POLL               (1 * 60)
#define SLOW_POLL               (10 * 60)

struct android_bat_data {
	struct android_bat_platform_data *pdata;
	struct android_bat_callbacks callbacks;

	struct device		*dev;

	struct power_supply	psy_bat;
	struct power_supply	psy_usb;
	struct power_supply	psy_ac;

	struct wake_lock	monitor_wake_lock;
	struct wake_lock	charger_wake_lock;

	int			charge_source;

	int			batt_temp;
	int			batt_current;
	unsigned int		batt_health;
	unsigned int		batt_vcell;
	unsigned int		batt_soc;
	unsigned int		charging_status;
	bool			recharging;
	unsigned long		charging_start_time;

	struct workqueue_struct *monitor_wqueue;
	struct work_struct	monitor_work;
	struct work_struct	charger_work;

	struct alarm		monitor_alarm;
	ktime_t			last_poll;

	struct dentry		*debugfs_entry;
};

static char *supply_list[] = {
	"android-battery",
};

static enum power_supply_property android_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property android_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static DEFINE_MUTEX(android_bat_state_lock);

static void android_bat_update_data(struct android_bat_data *battery);
static int android_bat_enable_charging(struct android_bat_data *battery,
					bool enable);

static char *charge_source_str(int charge_source)
{
	switch (charge_source) {
	case CHARGE_SOURCE_NONE:
		return "none";
	case CHARGE_SOURCE_AC:
		return "ac";
	case CHARGE_SOURCE_USB:
		return "usb";
	default:
		break;
	}

	return "?";
}

static int android_bat_get_property(struct power_supply *ps,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct android_bat_data *battery =
		container_of(ps, struct android_bat_data, psy_bat);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battery->charging_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery->batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->batt_temp;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		android_bat_update_data(battery);
		val->intval = battery->batt_vcell;
		if (val->intval == -1)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery->batt_soc;
		if (val->intval == -1)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		android_bat_update_data(battery);
		val->intval = battery->batt_current;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int android_usb_get_property(struct power_supply *ps,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct android_bat_data *battery = container_of(ps,
				struct android_bat_data, psy_usb);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	val->intval = (battery->charge_source == CHARGE_SOURCE_USB);

	return 0;
}

static int android_ac_get_property(struct power_supply *ps,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct android_bat_data *battery = container_of(ps,
				struct android_bat_data, psy_ac);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	val->intval = (battery->charge_source == CHARGE_SOURCE_AC);

	return 0;
}

static void android_bat_get_temp(struct android_bat_data *battery)
{
	int batt_temp = 42; /* 4.2C */
	int health = battery->batt_health;

	if (battery->pdata->get_temperature)
		battery->pdata->get_temperature(&batt_temp);

	if (batt_temp >= battery->pdata->temp_high_threshold) {
		if (health != POWER_SUPPLY_HEALTH_OVERHEAT &&
				health != POWER_SUPPLY_HEALTH_UNSPEC_FAILURE) {
			pr_info("battery overheat (%d>=%d), charging unavailable\n",
				batt_temp, battery->pdata->temp_high_threshold);
			battery->batt_health = POWER_SUPPLY_HEALTH_OVERHEAT;
		}
	} else if (batt_temp <= battery->pdata->temp_high_recovery &&
			batt_temp >= battery->pdata->temp_low_recovery) {
		if (health == POWER_SUPPLY_HEALTH_OVERHEAT ||
				health == POWER_SUPPLY_HEALTH_COLD) {
			pr_info("battery recovery (%d,%d~%d), charging available\n",
				batt_temp, battery->pdata->temp_low_recovery,
				battery->pdata->temp_high_recovery);
			battery->batt_health = POWER_SUPPLY_HEALTH_GOOD;
		}
	} else if (batt_temp <= battery->pdata->temp_low_threshold) {
		if (health != POWER_SUPPLY_HEALTH_COLD &&
				health != POWER_SUPPLY_HEALTH_UNSPEC_FAILURE) {
			pr_info("battery cold (%d <= %d), charging unavailable\n",
				batt_temp, battery->pdata->temp_low_threshold);
			battery->batt_health = POWER_SUPPLY_HEALTH_COLD;
		}
	}

	battery->batt_temp = batt_temp;
}

/*
 * android_bat_state_lock not held, may call back into
 * android_bat_charge_source_changed.  Gathering data here can be
 * non-atomic; updating our state based on the data may need to be
 * atomic.
 */

static void android_bat_update_data(struct android_bat_data *battery)
{
	int ret;
	int v;

	if (battery->pdata->poll_charge_source)
		battery->charge_source = battery->pdata->poll_charge_source();

	if (battery->pdata->get_voltage_now) {
		ret = battery->pdata->get_voltage_now();
		battery->batt_vcell = ret >= 0 ? ret : 4242000;
	}

	if (battery->pdata->get_capacity) {
		ret = battery->pdata->get_capacity();
		battery->batt_soc = ret >= 0 ? ret : 42;
	}

	if (battery->pdata->get_current_now) {
		ret = battery->pdata->get_current_now(&v);

		if (!ret)
			battery->batt_current = v;
	}

	android_bat_get_temp(battery);
}

static void android_bat_set_charge_time(struct android_bat_data *battery,
					bool enable)
{
	if (enable && !battery->charging_start_time) {
		struct timespec cur_time;

		get_monotonic_boottime(&cur_time);
		/* record start time for charge timeout timer */
		battery->charging_start_time = cur_time.tv_sec;
	} else if (!enable) {
		/* clear charge timeout timer */
		battery->charging_start_time = 0;
	}
}

static int android_bat_enable_charging(struct android_bat_data *battery,
				       bool enable)
{
	if (enable && (battery->batt_health != POWER_SUPPLY_HEALTH_GOOD)) {
		battery->charging_status =
		    POWER_SUPPLY_STATUS_NOT_CHARGING;
		return -EPERM;
	}

	if (enable) {
		if (battery->pdata && battery->pdata->set_charging_current)
			battery->pdata->set_charging_current
			(battery->charge_source);
	}

	if (battery->pdata && battery->pdata->set_charging_enable)
		battery->pdata->set_charging_enable(enable);

	android_bat_set_charge_time(battery, enable);
	pr_info("battery: enable=%d charger: %s\n", enable,
		charge_source_str(battery->charge_source));
	return 0;
}

static bool android_bat_charge_timeout(struct android_bat_data *battery,
				       unsigned long timeout)
{
	struct timespec cur_time;

	if (!battery->charging_start_time)
		return 0;

	get_monotonic_boottime(&cur_time);
	pr_debug("%s: Start time: %ld, End time: %ld, current time: %ld\n",
		 __func__, battery->charging_start_time,
		 battery->charging_start_time + timeout,
		 cur_time.tv_sec);
	return cur_time.tv_sec >= battery->charging_start_time + timeout;
}

static void android_bat_charging_timer(struct android_bat_data *battery)
{
	if (!battery->charging_start_time &&
	    battery->charging_status == POWER_SUPPLY_STATUS_CHARGING) {
		android_bat_enable_charging(battery, true);
		battery->recharging = true;
		pr_debug("%s: charge status charging but timer is expired\n",
			__func__);
	} else if (battery->charging_start_time == 0) {
		pr_debug("%s: charging_start_time never initialized\n",
				__func__);
		return;
	}

	if (android_bat_charge_timeout(
		    battery,
		    battery->recharging ? battery->pdata->recharging_time :
		    battery->pdata->full_charging_time)) {
		android_bat_enable_charging(battery, false);
		if (battery->batt_vcell >
		    battery->pdata->recharging_voltage &&
		    battery->batt_soc == 100)
			battery->charging_status =
				POWER_SUPPLY_STATUS_FULL;
		battery->recharging = false;
		battery->charging_start_time = 0;
		pr_info("battery: charging timer expired\n");
	}

	return;
}

static void android_bat_charge_source_changed(struct android_bat_callbacks *ptr,
					      int charge_source)
{
	struct android_bat_data *battery =
		container_of(ptr, struct android_bat_data, callbacks);

	wake_lock(&battery->charger_wake_lock);
	mutex_lock(&android_bat_state_lock);
	battery->charge_source = charge_source;

	pr_info("battery: charge source type was changed: %s\n",
		charge_source_str(battery->charge_source));

	mutex_unlock(&android_bat_state_lock);
	queue_work(battery->monitor_wqueue, &battery->charger_work);
}

static void android_bat_set_full_status(struct android_bat_callbacks *ptr)
{
	struct android_bat_data *battery =
		container_of(ptr, struct android_bat_data, callbacks);

	mutex_lock(&android_bat_state_lock);
	pr_info("battery: battery full\n");
	battery->charging_status = POWER_SUPPLY_STATUS_FULL;
	android_bat_enable_charging(battery, false);
	battery->recharging = false;
	mutex_unlock(&android_bat_state_lock);
	power_supply_changed(&battery->psy_bat);
}

static void android_bat_charger_work(struct work_struct *work)
{
	struct android_bat_data *battery =
		container_of(work, struct android_bat_data, charger_work);

	mutex_lock(&android_bat_state_lock);

	switch (battery->charge_source) {
	case CHARGE_SOURCE_NONE:
		battery->charging_status = POWER_SUPPLY_STATUS_DISCHARGING;
		android_bat_enable_charging(battery, false);
		battery->batt_health = POWER_SUPPLY_HEALTH_GOOD;
		battery->recharging = false;
		battery->charging_start_time = 0;
		break;
	case CHARGE_SOURCE_USB:
	case CHARGE_SOURCE_AC:
		/*
		 * If charging status indicates a charger was already
		 * connected prior to this and a non-charging status is
		 * set, leave the status alone.
		 */
		if (battery->charging_status ==
		    POWER_SUPPLY_STATUS_DISCHARGING ||
		    battery->charging_status == POWER_SUPPLY_STATUS_UNKNOWN) {
			battery->charging_status = POWER_SUPPLY_STATUS_CHARGING;
			android_bat_enable_charging(battery, true);
		}

		break;
	default:
		pr_err("%s: Invalid charger type\n", __func__);
		break;
	}

	mutex_unlock(&android_bat_state_lock);
	wake_lock_timeout(&battery->charger_wake_lock, HZ * 2);
	power_supply_changed(&battery->psy_ac);
	power_supply_changed(&battery->psy_usb);
}


static void android_bat_monitor_set_alarm(struct android_bat_data *battery,
					  int seconds)
{
	alarm_start(&battery->monitor_alarm,
		    ktime_add(battery->last_poll, ktime_set(seconds, 0)));
}

static void android_bat_monitor_work(struct work_struct *work)
{
	struct android_bat_data *battery =
		container_of(work, struct android_bat_data, monitor_work);
	struct timespec cur_time;

	wake_lock(&battery->monitor_wake_lock);
	android_bat_update_data(battery);
	mutex_lock(&android_bat_state_lock);

	switch (battery->charging_status) {
	case POWER_SUPPLY_STATUS_FULL:
		if (battery->batt_vcell < battery->pdata->recharging_voltage &&
		    !battery->recharging) {
			battery->recharging = true;
			android_bat_enable_charging(battery, true);
			pr_info("battery: start recharging, v=%d\n",
				battery->batt_vcell/1000);
		}
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
		switch (battery->batt_health) {
		case POWER_SUPPLY_HEALTH_OVERHEAT:
		case POWER_SUPPLY_HEALTH_COLD:
		case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
		case POWER_SUPPLY_HEALTH_DEAD:
		case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
			battery->charging_status =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
			android_bat_enable_charging(battery, false);

			pr_info("battery: Not charging, health=%d\n",
				battery->batt_health);
			break;
		default:
			break;
		}
		break;
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
		if (battery->batt_health == POWER_SUPPLY_HEALTH_GOOD) {
			pr_info("battery: battery health recovered\n");
			if (battery->charge_source != CHARGE_SOURCE_NONE) {
				android_bat_enable_charging(battery, true);
				battery->charging_status
					= POWER_SUPPLY_STATUS_CHARGING;
			} else {
				battery->charging_status
					= POWER_SUPPLY_STATUS_DISCHARGING;
			}
		}
		break;
	default:
		pr_err("%s: Undefined battery status: %d\n", __func__,
		       battery->charging_status);
		break;
	}

	android_bat_charging_timer(battery);
	get_monotonic_boottime(&cur_time);
	pr_info("battery: l=%d v=%d c=%d temp=%s%ld.%ld h=%d st=%d%s ct=%lu type=%s\n",
		battery->batt_soc, battery->batt_vcell/1000,
		battery->batt_current, battery->batt_temp < 0 ? "-" : "",
		abs(battery->batt_temp / 10), abs(battery->batt_temp % 10),
		battery->batt_health, battery->charging_status,
		   battery->recharging ? "r" : "",
		   battery->charging_start_time ?
		   cur_time.tv_sec - battery->charging_start_time : 0,
		charge_source_str(battery->charge_source));
	mutex_unlock(&android_bat_state_lock);
	power_supply_changed(&battery->psy_bat);
	battery->last_poll = ktime_get_boottime();
	android_bat_monitor_set_alarm(battery, FAST_POLL);
	wake_unlock(&battery->monitor_wake_lock);
	return;
}

static enum alarmtimer_restart android_bat_monitor_alarm(
	struct alarm *alarm, ktime_t now)
{
	struct android_bat_data *battery =
		container_of(alarm, struct android_bat_data, monitor_alarm);

	wake_lock(&battery->monitor_wake_lock);
	queue_work(battery->monitor_wqueue, &battery->monitor_work);
	return ALARMTIMER_NORESTART;
}

static int android_power_debug_dump(struct seq_file *s, void *unused)
{
	struct android_bat_data *battery = s->private;
	struct timespec cur_time;

	android_bat_update_data(battery);
	get_monotonic_boottime(&cur_time);
	mutex_lock(&android_bat_state_lock);
	seq_printf(s, "l=%d v=%d c=%d temp=%s%ld.%ld h=%d st=%d%s ct=%lu type=%s\n",
		   battery->batt_soc, battery->batt_vcell/1000,
		   battery->batt_current, battery->batt_temp < 0 ? "-" : "",
		   abs(battery->batt_temp / 10), abs(battery->batt_temp % 10),
		   battery->batt_health, battery->charging_status,
		   battery->recharging ? "r" : "",
		   battery->charging_start_time ?
		   cur_time.tv_sec - battery->charging_start_time : 0,
		   charge_source_str(battery->charge_source));
	mutex_unlock(&android_bat_state_lock);
	return 0;
}

static int android_power_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, android_power_debug_dump, inode->i_private);
}

static const struct file_operations android_power_debug_fops = {
	.open = android_power_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int android_bat_probe(struct platform_device *pdev)
{
	struct android_bat_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct android_bat_data *battery;
	int ret = 0;

	dev_info(&pdev->dev, "Android Battery Driver\n");
	battery = kzalloc(sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->pdata = pdata;
	if (!battery->pdata) {
		pr_err("%s : No platform data\n", __func__);
		ret = -EINVAL;
		goto err_pdata;
	}

	battery->dev = &pdev->dev;
	platform_set_drvdata(pdev, battery);
	battery->batt_health = POWER_SUPPLY_HEALTH_GOOD;

	battery->psy_bat.name = "android-battery",
	battery->psy_bat.type = POWER_SUPPLY_TYPE_BATTERY,
	battery->psy_bat.properties = android_battery_props,
	battery->psy_bat.num_properties = ARRAY_SIZE(android_battery_props),
	battery->psy_bat.get_property = android_bat_get_property,

	battery->psy_usb.name = "android-usb",
	battery->psy_usb.type = POWER_SUPPLY_TYPE_USB,
	battery->psy_usb.supplied_to = supply_list,
	battery->psy_usb.num_supplicants = ARRAY_SIZE(supply_list),
	battery->psy_usb.properties = android_power_props,
	battery->psy_usb.num_properties = ARRAY_SIZE(android_power_props),
	battery->psy_usb.get_property = android_usb_get_property,

	battery->psy_ac.name = "android-ac",
	battery->psy_ac.type = POWER_SUPPLY_TYPE_MAINS,
	battery->psy_ac.supplied_to = supply_list,
	battery->psy_ac.num_supplicants = ARRAY_SIZE(supply_list),
	battery->psy_ac.properties = android_power_props,
	battery->psy_ac.num_properties = ARRAY_SIZE(android_power_props),
	battery->psy_ac.get_property = android_ac_get_property;

	battery->batt_vcell = -1;
	battery->batt_soc = -1;

	wake_lock_init(&battery->monitor_wake_lock, WAKE_LOCK_SUSPEND,
			"android-battery-monitor");
	wake_lock_init(&battery->charger_wake_lock, WAKE_LOCK_SUSPEND,
			"android-chargerdetect");

	ret = power_supply_register(&pdev->dev, &battery->psy_bat);
	if (ret) {
		dev_err(battery->dev, "%s: failed to register psy_bat\n",
			__func__);
		goto err_psy_bat_reg;
	}

	ret = power_supply_register(&pdev->dev, &battery->psy_usb);
	if (ret) {
		dev_err(battery->dev, "%s: failed to register psy_usb\n",
				__func__);
		goto err_psy_usb_reg;
	}

	ret = power_supply_register(&pdev->dev, &battery->psy_ac);
	if (ret) {
		dev_err(battery->dev, "%s: failed to register psy_ac\n",
				__func__);
		goto err_psy_ac_reg;
	}

	battery->monitor_wqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!battery->monitor_wqueue) {
		dev_err(battery->dev, "%s: fail to create workqueue\n",
				__func__);
		goto err_wq;
	}

	INIT_WORK(&battery->monitor_work, android_bat_monitor_work);
	INIT_WORK(&battery->charger_work, android_bat_charger_work);

	battery->callbacks.charge_source_changed =
		android_bat_charge_source_changed;
	battery->callbacks.battery_set_full =
		android_bat_set_full_status;
	if (battery->pdata && battery->pdata->register_callbacks)
		battery->pdata->register_callbacks(&battery->callbacks);

	/* get initial charger status */
	if (battery->pdata->poll_charge_source)
		battery->charge_source = battery->pdata->poll_charge_source();

	wake_lock(&battery->charger_wake_lock);
	queue_work(battery->monitor_wqueue, &battery->charger_work);

	wake_lock(&battery->monitor_wake_lock);
	battery->last_poll = ktime_get_boottime();
	alarm_init(&battery->monitor_alarm, ALARM_BOOTTIME,
		   android_bat_monitor_alarm);
	queue_work(battery->monitor_wqueue, &battery->monitor_work);

	battery->debugfs_entry =
		debugfs_create_file("android-power", S_IRUGO, NULL,
				    battery, &android_power_debug_fops);
	if (!battery->debugfs_entry)
		pr_err("failed to create android-power debugfs entry\n");

	return 0;

err_wq:
	power_supply_unregister(&battery->psy_ac);
err_psy_ac_reg:
	power_supply_unregister(&battery->psy_usb);
err_psy_usb_reg:
	power_supply_unregister(&battery->psy_bat);
err_psy_bat_reg:
	wake_lock_destroy(&battery->monitor_wake_lock);
	wake_lock_destroy(&battery->charger_wake_lock);
err_pdata:
	kfree(battery);

	return ret;
}

static int android_bat_remove(struct platform_device *pdev)
{
	struct android_bat_data *battery = platform_get_drvdata(pdev);

	alarm_cancel(&battery->monitor_alarm);
	flush_workqueue(battery->monitor_wqueue);
	destroy_workqueue(battery->monitor_wqueue);
	power_supply_unregister(&battery->psy_bat);
	wake_lock_destroy(&battery->monitor_wake_lock);
	wake_lock_destroy(&battery->charger_wake_lock);
	debugfs_remove(battery->debugfs_entry);
	kfree(battery);
	return 0;
}

static int android_bat_suspend(struct device *dev)
{
	struct android_bat_data *battery = dev_get_drvdata(dev);

	cancel_work_sync(&battery->monitor_work);
	android_bat_monitor_set_alarm(battery, SLOW_POLL);
	return 0;
}

static int android_bat_resume(struct device *dev)
{
	struct android_bat_data *battery = dev_get_drvdata(dev);

	android_bat_monitor_set_alarm(battery, FAST_POLL);
	return 0;
}

static const struct dev_pm_ops android_bat_pm_ops = {
	.suspend	= android_bat_suspend,
	.resume = android_bat_resume,
};

static struct platform_driver android_bat_driver = {
	.driver = {
		.name = "android-battery",
		.owner = THIS_MODULE,
		.pm = &android_bat_pm_ops,
	},
	.probe = android_bat_probe,
	.remove = android_bat_remove,
};

static int __init android_bat_init(void)
{
	return platform_driver_register(&android_bat_driver);
}

static void __exit android_bat_exit(void)
{
	platform_driver_unregister(&android_bat_driver);
}

late_initcall(android_bat_init);
module_exit(android_bat_exit);

MODULE_DESCRIPTION("Android battery driver");
MODULE_LICENSE("GPL");
