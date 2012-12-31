/*
 * linux/drivers/power/samsung_fake_battery.c
 *
 * Battery measurement code for samsung smdk platform.
 *
 * Copyright (C) 2011 Samsung Electronics.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/wakelock.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <plat/gpio-cfg.h>

#define FAKE_BAT_LEVEL	80

static struct wake_lock vbus_wake_lock;

/* Prototypes */
static ssize_t samsung_fake_bat_show_property(struct device *dev,
				struct device_attribute *attr,
				char *buf);
static ssize_t samsung_fake_bat_store_property(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);

static struct device *dev;
static int samsung_fake_battery_initial;

static char *status_text[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN] =		"Unknown",
	[POWER_SUPPLY_STATUS_CHARGING] =	"Charging",
	[POWER_SUPPLY_STATUS_DISCHARGING] =	"Discharging",
	[POWER_SUPPLY_STATUS_NOT_CHARGING] =	"Not Charging",
	[POWER_SUPPLY_STATUS_FULL] =		"Full",
};

typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC,
	CHARGER_DISCHARGE
} charger_type_t;

struct battery_info {
	u32 batt_id;		/* Battery ID from ADC */
	u32 batt_vol;		/* Battery voltage from ADC */
	u32 batt_vol_adc;	/* Battery ADC value */
	u32 batt_vol_adc_cal;	/* Battery ADC value (calibrated)*/
	u32 batt_temp;		/* Battery Temperature (C) from ADC */
	u32 batt_temp_adc;	/* Battery Temperature ADC value */
	u32 batt_temp_adc_cal;	/* Battery Temperature ADC value (calibrated) */
	u32 batt_current;	/* Battery current from ADC */
	u32 level;		/* formula */
	u32 charging_source;	/* 0: no cable, 1:usb, 2:AC */
	u32 charging_enabled;	/* 0: Disable, 1: Enable */
	u32 batt_health;	/* Battery Health (Authority) */
	u32 batt_is_full;	/* 0 : Not full 1: Full */
};

/* lock to protect the battery info */
static DEFINE_MUTEX(work_lock);

struct samsung_fake_battery_info {
	int present;
	int polling;
	unsigned long polling_interval;

	struct battery_info bat_info;
};
static struct samsung_fake_battery_info samsung_fake_bat_info;

static int samsung_get_bat_level(struct power_supply *bat_ps)
{
	return FAKE_BAT_LEVEL;
}

static int samsung_get_bat_vol(struct power_supply *bat_ps)
{
	int bat_vol = 0;

	return bat_vol;
}

static u32 samsung_get_bat_health(void)
{
	return samsung_fake_bat_info.bat_info.batt_health;
}

static int samsung_get_bat_temp(struct power_supply *bat_ps)
{
	int temp = 0;

	return temp;
}

static int samsung_fake_bat_get_charging_status(void)
{
	charger_type_t charger = CHARGER_BATTERY;
	int ret = 0;

	charger = samsung_fake_bat_info.bat_info.charging_source;

	switch (charger) {
	case CHARGER_BATTERY:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHARGER_USB:
	case CHARGER_AC:
		if (samsung_fake_bat_info.bat_info.level == 100 &&
		    samsung_fake_bat_info.bat_info.batt_is_full)
			ret = POWER_SUPPLY_STATUS_FULL;
		else
			ret = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case CHARGER_DISCHARGE:
		ret = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		ret = POWER_SUPPLY_STATUS_UNKNOWN;
	}
	dev_dbg(dev, "%s : %s\n", __func__, status_text[ret]);

	return ret;
}

static int samsung_fake_bat_get_property(struct power_supply *bat_ps,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	dev_dbg(bat_ps->dev, "%s : psp = %d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = samsung_fake_bat_get_charging_status();
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = samsung_get_bat_health();
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = samsung_fake_bat_info.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = samsung_fake_bat_info.bat_info.level;
		dev_dbg(dev, "%s : level = %d\n", __func__,
				val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = samsung_fake_bat_info.bat_info.batt_temp;
		dev_dbg(bat_ps->dev, "%s : temp = %d\n", __func__,
				val->intval);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int samsung_power_get_property(struct power_supply *bat_ps,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	charger_type_t charger;

	dev_dbg(bat_ps->dev, "%s : psp = %d\n", __func__, psp);

	charger = samsung_fake_bat_info.bat_info.charging_source;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (bat_ps->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = (charger == CHARGER_AC ? 1 : 0);
		else if (bat_ps->type == POWER_SUPPLY_TYPE_USB)
			val->intval = (charger == CHARGER_USB ? 1 : 0);
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define SAMSUNG_FAKE_BAT_ATTR(_name)			\
{							\
	.attr = { .name = #_name,},			\
	.show = samsung_fake_bat_show_property,		\
	.store = samsung_fake_bat_store_property,	\
}

static struct device_attribute samsung_fake_battery_attrs[] = {
	SAMSUNG_FAKE_BAT_ATTR(batt_vol),
	SAMSUNG_FAKE_BAT_ATTR(batt_vol_adc),
	SAMSUNG_FAKE_BAT_ATTR(batt_vol_adc_cal),
	SAMSUNG_FAKE_BAT_ATTR(batt_temp),
	SAMSUNG_FAKE_BAT_ATTR(batt_temp_adc),
	SAMSUNG_FAKE_BAT_ATTR(batt_temp_adc_cal),
};

enum {
	BATT_VOL = 0,
	BATT_VOL_ADC,
	BATT_VOL_ADC_CAL,
	BATT_TEMP,
	BATT_TEMP_ADC,
	BATT_TEMP_ADC_CAL,
};

static int samsung_fake_bat_create_attrs(struct device * dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(samsung_fake_battery_attrs); i++) {
		rc = device_create_file(dev, &samsung_fake_battery_attrs[i]);
		if (rc)
		goto attrs_failed;
	}
	goto succeed;

attrs_failed:
	while (i--)
	device_remove_file(dev, &samsung_fake_battery_attrs[i]);
succeed:
	return rc;
}

static ssize_t samsung_fake_bat_show_property(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int i = 0;
	const ptrdiff_t off = attr - samsung_fake_battery_attrs;

	switch (off) {
	case BATT_VOL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_vol);
	break;
	case BATT_VOL_ADC:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_vol_adc);
		break;
	case BATT_VOL_ADC_CAL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_vol_adc_cal);
		break;
	case BATT_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_temp);
		break;
	case BATT_TEMP_ADC:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_temp_adc);
		break;
	case BATT_TEMP_ADC_CAL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			samsung_fake_bat_info.bat_info.batt_temp_adc_cal);
		break;
	default:
		i = -EINVAL;
	}

	return i;
}

static ssize_t samsung_fake_bat_store_property(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int x = 0;
	int ret = 0;
	const ptrdiff_t off = attr - samsung_fake_battery_attrs;

	switch (off) {
	case BATT_VOL_ADC_CAL:
		if (sscanf(buf, "%d\n", &x) == 1) {
			samsung_fake_bat_info.bat_info.batt_vol_adc_cal = x;
			ret = count;
		}
		dev_info(dev, "%s : batt_vol_adc_cal = %d\n", __func__, x);
		break;
	case BATT_TEMP_ADC_CAL:
		if (sscanf(buf, "%d\n", &x) == 1) {
			samsung_fake_bat_info.bat_info.batt_temp_adc_cal = x;
			ret = count;
		}
		dev_info(dev, "%s : batt_temp_adc_cal = %d\n", __func__, x);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property samsung_fake_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
};

static enum power_supply_property samsung_power_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *supply_list[] = {
	"battery",
};

static struct power_supply samsung_power_supplies[] = {
	{
		.name = "battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = samsung_fake_battery_properties,
		.num_properties = ARRAY_SIZE(samsung_fake_battery_properties),
		.get_property = samsung_fake_bat_get_property,
	}, {
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = samsung_power_properties,
		.num_properties = ARRAY_SIZE(samsung_power_properties),
		.get_property = samsung_power_get_property,
	}, {
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = samsung_power_properties,
		.num_properties = ARRAY_SIZE(samsung_power_properties),
		.get_property = samsung_power_get_property,
	},
};

static int samsung_cable_status_update(int status)
{
	int ret = 0;
	charger_type_t source = CHARGER_BATTERY;

	dev_dbg(dev, "%s\n", __func__);

	if (!samsung_fake_battery_initial)
		return -EPERM;

	switch (status) {
	case CHARGER_BATTERY:
		dev_dbg(dev, "%s : cable NOT PRESENT\n", __func__);
		samsung_fake_bat_info.bat_info.charging_source = CHARGER_BATTERY;
		break;
	case CHARGER_USB:
		dev_dbg(dev, "%s : cable USB\n", __func__);
		samsung_fake_bat_info.bat_info.charging_source = CHARGER_USB;
		break;
	case CHARGER_AC:
		dev_dbg(dev, "%s : cable AC\n", __func__);
		samsung_fake_bat_info.bat_info.charging_source = CHARGER_AC;
		break;
	case CHARGER_DISCHARGE:
		dev_dbg(dev, "%s : Discharge\n", __func__);
		samsung_fake_bat_info.bat_info.charging_source = CHARGER_DISCHARGE;
		break;
	default:
		dev_err(dev, "%s : Nat supported status\n", __func__);
		ret = -EINVAL;
	}
	source = samsung_fake_bat_info.bat_info.charging_source;

	if (source == CHARGER_USB || source == CHARGER_AC)
		wake_lock(&vbus_wake_lock);
	else
		wake_lock_timeout(&vbus_wake_lock, HZ / 2);

	/* if the power source changes, all power supplies may change state */
	power_supply_changed(&samsung_power_supplies[CHARGER_BATTERY]);

	dev_dbg(dev, "%s : call power_supply_changed\n", __func__);
	return ret;
}

static void samsung_fake_bat_status_update(struct power_supply *bat_ps)
{
	int old_level, old_temp, old_is_full;
	dev_dbg(dev, "%s ++\n", __func__);

	if (!samsung_fake_battery_initial)
		return;

	mutex_lock(&work_lock);

	old_temp = samsung_fake_bat_info.bat_info.batt_temp;
	old_level = samsung_fake_bat_info.bat_info.level;
	old_is_full = samsung_fake_bat_info.bat_info.batt_is_full;
	samsung_fake_bat_info.bat_info.batt_temp = samsung_get_bat_temp(bat_ps);
	samsung_fake_bat_info.bat_info.level = samsung_get_bat_level(bat_ps);
	samsung_fake_bat_info.bat_info.batt_vol = samsung_get_bat_vol(bat_ps);

	if (old_level != samsung_fake_bat_info.bat_info.level ||
	    old_temp != samsung_fake_bat_info.bat_info.batt_temp ||
	    old_is_full != samsung_fake_bat_info.bat_info.batt_is_full) {
		power_supply_changed(bat_ps);
		dev_dbg(dev, "%s : call power_supply_changed\n", __func__);
	}

	mutex_unlock(&work_lock);
	dev_dbg(dev, "%s --\n", __func__);
}

void samsung_cable_check_status(int flag)
{
    charger_type_t status = 0;

    if (flag == 0)
	status = CHARGER_BATTERY;
    else
	status = CHARGER_USB;

    samsung_cable_status_update(status);
}
EXPORT_SYMBOL(samsung_cable_check_status);

#ifdef CONFIG_PM
static int samsung_fake_bat_suspend(struct device *dev)
{
	return 0;
}

static void samsung_fake_bat_resume(struct device *dev)
{
}
#else
#define samsung_fake_bat_suspend NULL
#define samsung_fake_bat_resume NULL
#endif /* CONFIG_PM */

static int __devinit samsung_fake_bat_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;

	dev = &pdev->dev;
	dev_info(dev, "%s\n", __func__);

	samsung_fake_bat_info.present = 1;

	samsung_fake_bat_info.bat_info.batt_id = 0;
	samsung_fake_bat_info.bat_info.batt_vol = 0;
	samsung_fake_bat_info.bat_info.batt_vol_adc = 0;
	samsung_fake_bat_info.bat_info.batt_vol_adc_cal = 0;
	samsung_fake_bat_info.bat_info.batt_temp = 0;
	samsung_fake_bat_info.bat_info.batt_temp_adc = 0;
	samsung_fake_bat_info.bat_info.batt_temp_adc_cal = 0;
	samsung_fake_bat_info.bat_info.batt_current = 0;
	samsung_fake_bat_info.bat_info.level = 0;
	samsung_fake_bat_info.bat_info.charging_source = CHARGER_BATTERY;
	samsung_fake_bat_info.bat_info.charging_enabled = 0;
	samsung_fake_bat_info.bat_info.batt_health = POWER_SUPPLY_HEALTH_GOOD;

	/* init power supplier framework */
	for (i = 0; i < ARRAY_SIZE(samsung_power_supplies); i++) {
		ret = power_supply_register(&pdev->dev,
				&samsung_power_supplies[i]);
		if (ret) {
			dev_err(dev, "Failed to register"
					"power supply %d,%d\n", i, ret);
			goto __end__;
		}
	}

	/* create sec detail attributes */
	samsung_fake_bat_create_attrs(samsung_power_supplies[CHARGER_BATTERY].dev);

	samsung_fake_battery_initial = 1;

	samsung_fake_bat_status_update(
			&samsung_power_supplies[CHARGER_BATTERY]);

__end__:
	return ret;
}

static int __devexit samsung_fake_bat_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(samsung_power_supplies); i++)
		power_supply_unregister(&samsung_power_supplies[i]);

	return 0;
}

static const struct dev_pm_ops samsung_fake_bat_pm_ops = {
	.prepare	= samsung_fake_bat_suspend,
	.complete	= samsung_fake_bat_resume,
};

static struct platform_driver samsung_fake_bat_driver = {
	.driver = {
		.name	= "samsung-fake-battery",
		.owner	= THIS_MODULE,
		.pm	= &samsung_fake_bat_pm_ops,
	},
	.probe		= samsung_fake_bat_probe,
	.remove		= __devexit_p(samsung_fake_bat_remove),
};

static int __init samsung_fake_bat_init(void)
{
	wake_lock_init(&vbus_wake_lock, WAKE_LOCK_SUSPEND, "vbus_present");

	return platform_driver_register(&samsung_fake_bat_driver);
}

static void __exit samsung_fake_bat_exit(void)
{
	platform_driver_unregister(&samsung_fake_bat_driver);
}

module_init(samsung_fake_bat_init);
module_exit(samsung_fake_bat_exit);

MODULE_AUTHOR("HuiSung Kang <hs1218.kang@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG Fake battery driver for SMDK Board");
MODULE_LICENSE("GPL");
