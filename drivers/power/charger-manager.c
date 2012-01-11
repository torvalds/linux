/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This driver enables to monitor battery health and control charger
 * during suspend-to-mem.
 * Charger manager depends on other devices. register this later than
 * the depending devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
**/

#include <linux/io.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/power/charger-manager.h>
#include <linux/regulator/consumer.h>

/*
 * Regard CM_JIFFIES_SMALL jiffies is small enough to ignore for
 * delayed works so that we can run delayed works with CM_JIFFIES_SMALL
 * without any delays.
 */
#define	CM_JIFFIES_SMALL	(2)

/* If y is valid (> 0) and smaller than x, do x = y */
#define CM_MIN_VALID(x, y)	x = (((y > 0) && ((x) > (y))) ? (y) : (x))

/*
 * Regard CM_RTC_SMALL (sec) is small enough to ignore error in invoking
 * rtc alarm. It should be 2 or larger
 */
#define CM_RTC_SMALL		(2)

#define UEVENT_BUF_SIZE		32

static LIST_HEAD(cm_list);
static DEFINE_MUTEX(cm_list_mtx);

/* About in-suspend (suspend-again) monitoring */
static struct rtc_device *rtc_dev;
/*
 * Backup RTC alarm
 * Save the wakeup alarm before entering suspend-to-RAM
 */
static struct rtc_wkalrm rtc_wkalarm_save;
/* Backup RTC alarm time in terms of seconds since 01-01-1970 00:00:00 */
static unsigned long rtc_wkalarm_save_time;
static bool cm_suspended;
static bool cm_rtc_set;
static unsigned long cm_suspend_duration_ms;

/* Global charger-manager description */
static struct charger_global_desc *g_desc; /* init with setup_charger_manager */

/**
 * is_batt_present - See if the battery presents in place.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_batt_present(struct charger_manager *cm)
{
	union power_supply_propval val;
	bool present = false;
	int i, ret;

	switch (cm->desc->battery_present) {
	case CM_FUEL_GAUGE:
		ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
				POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret == 0 && val.intval)
			present = true;
		break;
	case CM_CHARGER_STAT:
		for (i = 0; cm->charger_stat[i]; i++) {
			ret = cm->charger_stat[i]->get_property(
					cm->charger_stat[i],
					POWER_SUPPLY_PROP_PRESENT, &val);
			if (ret == 0 && val.intval) {
				present = true;
				break;
			}
		}
		break;
	}

	return present;
}

/**
 * is_ext_pwr_online - See if an external power source is attached to charge
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if at least one of the chargers of the battery has an external
 * power source attached to charge the battery regardless of whether it is
 * actually charging or not.
 */
static bool is_ext_pwr_online(struct charger_manager *cm)
{
	union power_supply_propval val;
	bool online = false;
	int i, ret;

	/* If at least one of them has one, it's yes. */
	for (i = 0; cm->charger_stat[i]; i++) {
		ret = cm->charger_stat[i]->get_property(
				cm->charger_stat[i],
				POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret == 0 && val.intval) {
			online = true;
			break;
		}
	}

	return online;
}

/**
 * get_batt_uV - Get the voltage level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_batt_uV(struct charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	int ret;

	if (cm->fuel_gauge)
		ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	else
		return -ENODEV;

	if (ret)
		return ret;

	*uV = val.intval;
	return 0;
}

/**
 * is_charging - Returns true if the battery is being charged.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_charging(struct charger_manager *cm)
{
	int i, ret;
	bool charging = false;
	union power_supply_propval val;

	/* If there is no battery, it cannot be charged */
	if (!is_batt_present(cm))
		return false;

	/* If at least one of the charger is charging, return yes */
	for (i = 0; cm->charger_stat[i]; i++) {
		/* 1. The charger sholuld not be DISABLED */
		if (cm->emergency_stop)
			continue;
		if (!cm->charger_enabled)
			continue;

		/* 2. The charger should be online (ext-power) */
		ret = cm->charger_stat[i]->get_property(
				cm->charger_stat[i],
				POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret) {
			dev_warn(cm->dev, "Cannot read ONLINE value from %s.\n",
					cm->desc->psy_charger_stat[i]);
			continue;
		}
		if (val.intval == 0)
			continue;

		/*
		 * 3. The charger should not be FULL, DISCHARGING,
		 * or NOT_CHARGING.
		 */
		ret = cm->charger_stat[i]->get_property(
				cm->charger_stat[i],
				POWER_SUPPLY_PROP_STATUS, &val);
		if (ret) {
			dev_warn(cm->dev, "Cannot read STATUS value from %s.\n",
					cm->desc->psy_charger_stat[i]);
			continue;
		}
		if (val.intval == POWER_SUPPLY_STATUS_FULL ||
				val.intval == POWER_SUPPLY_STATUS_DISCHARGING ||
				val.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
			continue;

		/* Then, this is charging. */
		charging = true;
		break;
	}

	return charging;
}

/**
 * is_polling_required - Return true if need to continue polling for this CM.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_polling_required(struct charger_manager *cm)
{
	switch (cm->desc->polling_mode) {
	case CM_POLL_DISABLE:
		return false;
	case CM_POLL_ALWAYS:
		return true;
	case CM_POLL_EXTERNAL_POWER_ONLY:
		return is_ext_pwr_online(cm);
	case CM_POLL_CHARGING_ONLY:
		return is_charging(cm);
	default:
		dev_warn(cm->dev, "Incorrect polling_mode (%d)\n",
			cm->desc->polling_mode);
	}

	return false;
}

/**
 * try_charger_enable - Enable/Disable chargers altogether
 * @cm: the Charger Manager representing the battery.
 * @enable: true: enable / false: disable
 *
 * Note that Charger Manager keeps the charger enabled regardless whether
 * the charger is charging or not (because battery is full or no external
 * power source exists) except when CM needs to disable chargers forcibly
 * bacause of emergency causes; when the battery is overheated or too cold.
 */
static int try_charger_enable(struct charger_manager *cm, bool enable)
{
	int err = 0, i;
	struct charger_desc *desc = cm->desc;

	/* Ignore if it's redundent command */
	if (enable && cm->charger_enabled)
		return 0;
	if (!enable && !cm->charger_enabled)
		return 0;

	if (enable) {
		if (cm->emergency_stop)
			return -EAGAIN;
		err = regulator_bulk_enable(desc->num_charger_regulators,
					desc->charger_regulators);
	} else {
		/*
		 * Abnormal battery state - Stop charging forcibly,
		 * even if charger was enabled at the other places
		 */
		err = regulator_bulk_disable(desc->num_charger_regulators,
					desc->charger_regulators);

		for (i = 0; i < desc->num_charger_regulators; i++) {
			if (regulator_is_enabled(
				    desc->charger_regulators[i].consumer)) {
				regulator_force_disable(
					desc->charger_regulators[i].consumer);
				dev_warn(cm->dev,
					"Disable regulator(%s) forcibly.\n",
					desc->charger_regulators[i].supply);
			}
		}
	}

	if (!err)
		cm->charger_enabled = enable;

	return err;
}

/**
 * uevent_notify - Let users know something has changed.
 * @cm: the Charger Manager representing the battery.
 * @event: the event string.
 *
 * If @event is null, it implies that uevent_notify is called
 * by resume function. When called in the resume function, cm_suspended
 * should be already reset to false in order to let uevent_notify
 * notify the recent event during the suspend to users. While
 * suspended, uevent_notify does not notify users, but tracks
 * events so that uevent_notify can notify users later after resumed.
 */
static void uevent_notify(struct charger_manager *cm, const char *event)
{
	static char env_str[UEVENT_BUF_SIZE + 1] = "";
	static char env_str_save[UEVENT_BUF_SIZE + 1] = "";

	if (cm_suspended) {
		/* Nothing in suspended-event buffer */
		if (env_str_save[0] == 0) {
			if (!strncmp(env_str, event, UEVENT_BUF_SIZE))
				return; /* status not changed */
			strncpy(env_str_save, event, UEVENT_BUF_SIZE);
			return;
		}

		if (!strncmp(env_str_save, event, UEVENT_BUF_SIZE))
			return; /* Duplicated. */
		else
			strncpy(env_str_save, event, UEVENT_BUF_SIZE);

		return;
	}

	if (event == NULL) {
		/* No messages pending */
		if (!env_str_save[0])
			return;

		strncpy(env_str, env_str_save, UEVENT_BUF_SIZE);
		kobject_uevent(&cm->dev->kobj, KOBJ_CHANGE);
		env_str_save[0] = 0;

		return;
	}

	/* status not changed */
	if (!strncmp(env_str, event, UEVENT_BUF_SIZE))
		return;

	/* save the status and notify the update */
	strncpy(env_str, event, UEVENT_BUF_SIZE);
	kobject_uevent(&cm->dev->kobj, KOBJ_CHANGE);

	dev_info(cm->dev, event);
}

/**
 * _cm_monitor - Monitor the temperature and return true for exceptions.
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if there is an event to notify for the battery.
 * (True if the status of "emergency_stop" changes)
 */
static bool _cm_monitor(struct charger_manager *cm)
{
	struct charger_desc *desc = cm->desc;
	int temp = desc->temperature_out_of_range(&cm->last_temp_mC);

	dev_dbg(cm->dev, "monitoring (%2.2d.%3.3dC)\n",
		cm->last_temp_mC / 1000, cm->last_temp_mC % 1000);

	/* It has been stopped or charging already */
	if (!!temp == !!cm->emergency_stop)
		return false;

	if (temp) {
		cm->emergency_stop = temp;
		if (!try_charger_enable(cm, false)) {
			if (temp > 0)
				uevent_notify(cm, "OVERHEAT");
			else
				uevent_notify(cm, "COLD");
		}
	} else {
		cm->emergency_stop = 0;
		if (!try_charger_enable(cm, true))
			uevent_notify(cm, "CHARGING");
	}

	return true;
}

/**
 * cm_monitor - Monitor every battery.
 *
 * Returns true if there is an event to notify from any of the batteries.
 * (True if the status of "emergency_stop" changes)
 */
static bool cm_monitor(void)
{
	bool stop = false;
	struct charger_manager *cm;

	mutex_lock(&cm_list_mtx);

	list_for_each_entry(cm, &cm_list, entry)
		stop = stop || _cm_monitor(cm);

	mutex_unlock(&cm_list_mtx);

	return stop;
}

static int charger_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct charger_manager *cm = container_of(psy,
			struct charger_manager, charger_psy);
	struct charger_desc *desc = cm->desc;
	int i, ret = 0, uV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (is_charging(cm))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (is_ext_pwr_online(cm))
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (cm->emergency_stop > 0)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (cm->emergency_stop < 0)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (is_batt_present(cm))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = get_batt_uV(cm, &i);
		val->intval = i;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		/* in thenth of centigrade */
		if (cm->last_temp_mC == INT_MIN)
			desc->temperature_out_of_range(&cm->last_temp_mC);
		val->intval = cm->last_temp_mC / 100;
		if (!desc->measure_battery_temp)
			ret = -ENODEV;
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		/* in thenth of centigrade */
		if (cm->last_temp_mC == INT_MIN)
			desc->temperature_out_of_range(&cm->last_temp_mC);
		val->intval = cm->last_temp_mC / 100;
		if (desc->measure_battery_temp)
			ret = -ENODEV;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!cm->fuel_gauge) {
			ret = -ENODEV;
			break;
		}

		if (!is_batt_present(cm)) {
			/* There is no battery. Assume 100% */
			val->intval = 100;
			break;
		}

		ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
					POWER_SUPPLY_PROP_CAPACITY, val);
		if (ret)
			break;

		if (val->intval > 100) {
			val->intval = 100;
			break;
		}
		if (val->intval < 0)
			val->intval = 0;

		/* Do not adjust SOC when charging: voltage is overrated */
		if (is_charging(cm))
			break;

		/*
		 * If the capacity value is inconsistent, calibrate it base on
		 * the battery voltage values and the thresholds given as desc
		 */
		ret = get_batt_uV(cm, &uV);
		if (ret) {
			/* Voltage information not available. No calibration */
			ret = 0;
			break;
		}

		if (desc->fullbatt_uV > 0 && uV >= desc->fullbatt_uV &&
		    !is_charging(cm)) {
			val->intval = 100;
			break;
		}

		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (is_ext_pwr_online(cm))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (cm->fuel_gauge) {
			if (cm->fuel_gauge->get_property(cm->fuel_gauge,
			    POWER_SUPPLY_PROP_CHARGE_FULL, val) == 0)
				break;
		}

		if (is_ext_pwr_online(cm)) {
			/* Not full if it's charging. */
			if (is_charging(cm)) {
				val->intval = 0;
				break;
			}
			/*
			 * Full if it's powered but not charging andi
			 * not forced stop by emergency
			 */
			if (!cm->emergency_stop) {
				val->intval = 1;
				break;
			}
		}

		/* Full if it's over the fullbatt voltage */
		ret = get_batt_uV(cm, &uV);
		if (!ret && desc->fullbatt_uV > 0 && uV >= desc->fullbatt_uV &&
		    !is_charging(cm)) {
			val->intval = 1;
			break;
		}

		/* Full if the cap is 100 */
		if (cm->fuel_gauge) {
			ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
					POWER_SUPPLY_PROP_CAPACITY, val);
			if (!ret && val->intval >= 100 && !is_charging(cm)) {
				val->intval = 1;
				break;
			}
		}

		val->intval = 0;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (is_charging(cm)) {
			ret = cm->fuel_gauge->get_property(cm->fuel_gauge,
						POWER_SUPPLY_PROP_CHARGE_NOW,
						val);
			if (ret) {
				val->intval = 1;
				ret = 0;
			} else {
				/* If CHARGE_NOW is supplied, use it */
				val->intval = (val->intval > 0) ?
						val->intval : 1;
			}
		} else {
			val->intval = 0;
		}
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

#define NUM_CHARGER_PSY_OPTIONAL	(4)
static enum power_supply_property default_charger_props[] = {
	/* Guaranteed to provide */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	/*
	 * Optional properties are:
	 * POWER_SUPPLY_PROP_CHARGE_NOW,
	 * POWER_SUPPLY_PROP_CURRENT_NOW,
	 * POWER_SUPPLY_PROP_TEMP, and
	 * POWER_SUPPLY_PROP_TEMP_AMBIENT,
	 */
};

static struct power_supply psy_default = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = default_charger_props,
	.num_properties = ARRAY_SIZE(default_charger_props),
	.get_property = charger_get_property,
};

/**
 * cm_setup_timer - For in-suspend monitoring setup wakeup alarm
 *		    for suspend_again.
 *
 * Returns true if the alarm is set for Charger Manager to use.
 * Returns false if
 *	cm_setup_timer fails to set an alarm,
 *	cm_setup_timer does not need to set an alarm for Charger Manager,
 *	or an alarm previously configured is to be used.
 */
static bool cm_setup_timer(void)
{
	struct charger_manager *cm;
	unsigned int wakeup_ms = UINT_MAX;
	bool ret = false;

	mutex_lock(&cm_list_mtx);

	list_for_each_entry(cm, &cm_list, entry) {
		/* Skip if polling is not required for this CM */
		if (!is_polling_required(cm) && !cm->emergency_stop)
			continue;
		if (cm->desc->polling_interval_ms == 0)
			continue;
		CM_MIN_VALID(wakeup_ms, cm->desc->polling_interval_ms);
	}

	mutex_unlock(&cm_list_mtx);

	if (wakeup_ms < UINT_MAX && wakeup_ms > 0) {
		pr_info("Charger Manager wakeup timer: %u ms.\n", wakeup_ms);
		if (rtc_dev) {
			struct rtc_wkalrm tmp;
			unsigned long time, now;
			unsigned long add = DIV_ROUND_UP(wakeup_ms, 1000);

			/*
			 * Set alarm with the polling interval (wakeup_ms)
			 * except when rtc_wkalarm_save comes first.
			 * However, the alarm time should be NOW +
			 * CM_RTC_SMALL or later.
			 */
			tmp.enabled = 1;
			rtc_read_time(rtc_dev, &tmp.time);
			rtc_tm_to_time(&tmp.time, &now);
			if (add < CM_RTC_SMALL)
				add = CM_RTC_SMALL;
			time = now + add;

			ret = true;

			if (rtc_wkalarm_save.enabled &&
			    rtc_wkalarm_save_time &&
			    rtc_wkalarm_save_time < time) {
				if (rtc_wkalarm_save_time < now + CM_RTC_SMALL)
					time = now + CM_RTC_SMALL;
				else
					time = rtc_wkalarm_save_time;

				/* The timer is not appointed by CM */
				ret = false;
			}

			pr_info("Waking up after %lu secs.\n",
					time - now);

			rtc_time_to_tm(time, &tmp.time);
			rtc_set_alarm(rtc_dev, &tmp);
			cm_suspend_duration_ms += wakeup_ms;
			return ret;
		}
	}

	if (rtc_dev)
		rtc_set_alarm(rtc_dev, &rtc_wkalarm_save);
	return false;
}

/**
 * cm_suspend_again - Determine whether suspend again or not
 *
 * Returns true if the system should be suspended again
 * Returns false if the system should be woken up
 */
bool cm_suspend_again(void)
{
	struct charger_manager *cm;
	bool ret = false;

	if (!g_desc || !g_desc->rtc_only_wakeup || !g_desc->rtc_only_wakeup() ||
	    !cm_rtc_set)
		return false;

	if (cm_monitor())
		goto out;

	ret = true;
	mutex_lock(&cm_list_mtx);
	list_for_each_entry(cm, &cm_list, entry) {
		if (cm->status_save_ext_pwr_inserted != is_ext_pwr_online(cm) ||
		    cm->status_save_batt != is_batt_present(cm))
			ret = false;
	}
	mutex_unlock(&cm_list_mtx);

	cm_rtc_set = cm_setup_timer();
out:
	/* It's about the time when the non-CM appointed timer goes off */
	if (rtc_wkalarm_save.enabled) {
		unsigned long now;
		struct rtc_time tmp;

		rtc_read_time(rtc_dev, &tmp);
		rtc_tm_to_time(&tmp, &now);

		if (rtc_wkalarm_save_time &&
		    now + CM_RTC_SMALL >= rtc_wkalarm_save_time)
			return false;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cm_suspend_again);

/**
 * setup_charger_manager - initialize charger_global_desc data
 * @gd: pointer to instance of charger_global_desc
 */
int setup_charger_manager(struct charger_global_desc *gd)
{
	if (!gd)
		return -EINVAL;

	if (rtc_dev)
		rtc_class_close(rtc_dev);
	rtc_dev = NULL;
	g_desc = NULL;

	if (!gd->rtc_only_wakeup) {
		pr_err("The callback rtc_only_wakeup is not given.\n");
		return -EINVAL;
	}

	if (gd->rtc_name) {
		rtc_dev = rtc_class_open(gd->rtc_name);
		if (IS_ERR_OR_NULL(rtc_dev)) {
			rtc_dev = NULL;
			/* Retry at probe. RTC may be not registered yet */
		}
	} else {
		pr_warn("No wakeup timer is given for charger manager."
			"In-suspend monitoring won't work.\n");
	}

	g_desc = gd;
	return 0;
}
EXPORT_SYMBOL_GPL(setup_charger_manager);

static int charger_manager_probe(struct platform_device *pdev)
{
	struct charger_desc *desc = dev_get_platdata(&pdev->dev);
	struct charger_manager *cm;
	int ret = 0, i = 0;
	union power_supply_propval val;

	if (g_desc && !rtc_dev && g_desc->rtc_name) {
		rtc_dev = rtc_class_open(g_desc->rtc_name);
		if (IS_ERR_OR_NULL(rtc_dev)) {
			rtc_dev = NULL;
			dev_err(&pdev->dev, "Cannot get RTC %s.\n",
				g_desc->rtc_name);
			ret = -ENODEV;
			goto err_alloc;
		}
	}

	if (!desc) {
		dev_err(&pdev->dev, "No platform data (desc) found.\n");
		ret = -ENODEV;
		goto err_alloc;
	}

	cm = kzalloc(sizeof(struct charger_manager), GFP_KERNEL);
	if (!cm) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Basic Values. Unspecified are Null or 0 */
	cm->dev = &pdev->dev;
	cm->desc = kzalloc(sizeof(struct charger_desc), GFP_KERNEL);
	if (!cm->desc) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		ret = -ENOMEM;
		goto err_alloc_desc;
	}
	memcpy(cm->desc, desc, sizeof(struct charger_desc));
	cm->last_temp_mC = INT_MIN; /* denotes "unmeasured, yet" */

	if (!desc->charger_regulators || desc->num_charger_regulators < 1) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "charger_regulators undefined.\n");
		goto err_no_charger;
	}

	if (!desc->psy_charger_stat || !desc->psy_charger_stat[0]) {
		dev_err(&pdev->dev, "No power supply defined.\n");
		ret = -EINVAL;
		goto err_no_charger_stat;
	}

	/* Counting index only */
	while (desc->psy_charger_stat[i])
		i++;

	cm->charger_stat = kzalloc(sizeof(struct power_supply *) * (i + 1),
				   GFP_KERNEL);
	if (!cm->charger_stat) {
		ret = -ENOMEM;
		goto err_no_charger_stat;
	}

	for (i = 0; desc->psy_charger_stat[i]; i++) {
		cm->charger_stat[i] = power_supply_get_by_name(
					desc->psy_charger_stat[i]);
		if (!cm->charger_stat[i]) {
			dev_err(&pdev->dev, "Cannot find power supply "
					"\"%s\"\n",
					desc->psy_charger_stat[i]);
			ret = -ENODEV;
			goto err_chg_stat;
		}
	}

	cm->fuel_gauge = power_supply_get_by_name(desc->psy_fuel_gauge);
	if (!cm->fuel_gauge) {
		dev_err(&pdev->dev, "Cannot find power supply \"%s\"\n",
				desc->psy_fuel_gauge);
		ret = -ENODEV;
		goto err_chg_stat;
	}

	if (desc->polling_interval_ms == 0 ||
	    msecs_to_jiffies(desc->polling_interval_ms) <= CM_JIFFIES_SMALL) {
		dev_err(&pdev->dev, "polling_interval_ms is too small\n");
		ret = -EINVAL;
		goto err_chg_stat;
	}

	if (!desc->temperature_out_of_range) {
		dev_err(&pdev->dev, "there is no temperature_out_of_range\n");
		ret = -EINVAL;
		goto err_chg_stat;
	}

	platform_set_drvdata(pdev, cm);

	memcpy(&cm->charger_psy, &psy_default,
				sizeof(psy_default));
	if (!desc->psy_name) {
		strncpy(cm->psy_name_buf, psy_default.name,
				PSY_NAME_MAX);
	} else {
		strncpy(cm->psy_name_buf, desc->psy_name, PSY_NAME_MAX);
	}
	cm->charger_psy.name = cm->psy_name_buf;

	/* Allocate for psy properties because they may vary */
	cm->charger_psy.properties = kzalloc(sizeof(enum power_supply_property)
				* (ARRAY_SIZE(default_charger_props) +
				NUM_CHARGER_PSY_OPTIONAL),
				GFP_KERNEL);
	if (!cm->charger_psy.properties) {
		dev_err(&pdev->dev, "Cannot allocate for psy properties.\n");
		ret = -ENOMEM;
		goto err_chg_stat;
	}
	memcpy(cm->charger_psy.properties, default_charger_props,
		sizeof(enum power_supply_property) *
		ARRAY_SIZE(default_charger_props));
	cm->charger_psy.num_properties = psy_default.num_properties;

	/* Find which optional psy-properties are available */
	if (!cm->fuel_gauge->get_property(cm->fuel_gauge,
					  POWER_SUPPLY_PROP_CHARGE_NOW, &val)) {
		cm->charger_psy.properties[cm->charger_psy.num_properties] =
				POWER_SUPPLY_PROP_CHARGE_NOW;
		cm->charger_psy.num_properties++;
	}
	if (!cm->fuel_gauge->get_property(cm->fuel_gauge,
					  POWER_SUPPLY_PROP_CURRENT_NOW,
					  &val)) {
		cm->charger_psy.properties[cm->charger_psy.num_properties] =
				POWER_SUPPLY_PROP_CURRENT_NOW;
		cm->charger_psy.num_properties++;
	}
	if (!desc->measure_battery_temp) {
		cm->charger_psy.properties[cm->charger_psy.num_properties] =
				POWER_SUPPLY_PROP_TEMP_AMBIENT;
		cm->charger_psy.num_properties++;
	}
	if (desc->measure_battery_temp) {
		cm->charger_psy.properties[cm->charger_psy.num_properties] =
				POWER_SUPPLY_PROP_TEMP;
		cm->charger_psy.num_properties++;
	}

	ret = power_supply_register(NULL, &cm->charger_psy);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register charger-manager with"
				" name \"%s\".\n", cm->charger_psy.name);
		goto err_register;
	}

	ret = regulator_bulk_get(&pdev->dev, desc->num_charger_regulators,
				 desc->charger_regulators);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get charger regulators.\n");
		goto err_bulk_get;
	}

	ret = try_charger_enable(cm, true);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable charger regulators\n");
		goto err_chg_enable;
	}

	/* Add to the list */
	mutex_lock(&cm_list_mtx);
	list_add(&cm->entry, &cm_list);
	mutex_unlock(&cm_list_mtx);

	return 0;

err_chg_enable:
	if (desc->charger_regulators)
		regulator_bulk_free(desc->num_charger_regulators,
					desc->charger_regulators);
err_bulk_get:
	power_supply_unregister(&cm->charger_psy);
err_register:
	kfree(cm->charger_psy.properties);
err_chg_stat:
	kfree(cm->charger_stat);
err_no_charger_stat:
err_no_charger:
	kfree(cm->desc);
err_alloc_desc:
	kfree(cm);
err_alloc:
	return ret;
}

static int __devexit charger_manager_remove(struct platform_device *pdev)
{
	struct charger_manager *cm = platform_get_drvdata(pdev);
	struct charger_desc *desc = cm->desc;

	/* Remove from the list */
	mutex_lock(&cm_list_mtx);
	list_del(&cm->entry);
	mutex_unlock(&cm_list_mtx);

	if (desc->charger_regulators)
		regulator_bulk_free(desc->num_charger_regulators,
					desc->charger_regulators);

	power_supply_unregister(&cm->charger_psy);
	kfree(cm->charger_psy.properties);
	kfree(cm->charger_stat);
	kfree(cm->desc);
	kfree(cm);

	return 0;
}

static const struct platform_device_id charger_manager_id[] = {
	{ "charger-manager", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, charger_manager_id);

static int cm_suspend_prepare(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct charger_manager *cm = platform_get_drvdata(pdev);

	if (!cm_suspended) {
		if (rtc_dev) {
			struct rtc_time tmp;
			unsigned long now;

			rtc_read_alarm(rtc_dev, &rtc_wkalarm_save);
			rtc_read_time(rtc_dev, &tmp);

			if (rtc_wkalarm_save.enabled) {
				rtc_tm_to_time(&rtc_wkalarm_save.time,
					       &rtc_wkalarm_save_time);
				rtc_tm_to_time(&tmp, &now);
				if (now > rtc_wkalarm_save_time)
					rtc_wkalarm_save_time = 0;
			} else {
				rtc_wkalarm_save_time = 0;
			}
		}
		cm_suspended = true;
	}

	cm->status_save_ext_pwr_inserted = is_ext_pwr_online(cm);
	cm->status_save_batt = is_batt_present(cm);

	if (!cm_rtc_set) {
		cm_suspend_duration_ms = 0;
		cm_rtc_set = cm_setup_timer();
	}

	return 0;
}

static void cm_suspend_complete(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct charger_manager *cm = platform_get_drvdata(pdev);

	if (cm_suspended) {
		if (rtc_dev) {
			struct rtc_wkalrm tmp;

			rtc_read_alarm(rtc_dev, &tmp);
			rtc_wkalarm_save.pending = tmp.pending;
			rtc_set_alarm(rtc_dev, &rtc_wkalarm_save);
		}
		cm_suspended = false;
		cm_rtc_set = false;
	}

	uevent_notify(cm, NULL);
}

static const struct dev_pm_ops charger_manager_pm = {
	.prepare	= cm_suspend_prepare,
	.complete	= cm_suspend_complete,
};

static struct platform_driver charger_manager_driver = {
	.driver = {
		.name = "charger-manager",
		.owner = THIS_MODULE,
		.pm = &charger_manager_pm,
	},
	.probe = charger_manager_probe,
	.remove = __devexit_p(charger_manager_remove),
	.id_table = charger_manager_id,
};

static int __init charger_manager_init(void)
{
	return platform_driver_register(&charger_manager_driver);
}
late_initcall(charger_manager_init);

static void __exit charger_manager_cleanup(void)
{
	platform_driver_unregister(&charger_manager_driver);
}
module_exit(charger_manager_cleanup);

MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("Charger Manager");
MODULE_LICENSE("GPL");
