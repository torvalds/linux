/*
 * linux/drivers/power/palmtx_battery.c
 *
 * Battery measurement code for Palm T|X Handheld computer
 *
 * based on tosa_battery.c
 *
 * Copyright (C) 2008 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/wm97xx.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <mach/palmtx.h>

static DEFINE_MUTEX(bat_lock);
static struct work_struct bat_work;
struct mutex work_lock;
int bat_status = POWER_SUPPLY_STATUS_DISCHARGING;

static unsigned long palmtx_read_bat(struct power_supply *bat_ps)
{
	return wm97xx_read_aux_adc(bat_ps->dev->parent->driver_data,
				    WM97XX_AUX_ID3) * 1000 / 414;
}

static unsigned long palmtx_read_temp(struct power_supply *bat_ps)
{
	return wm97xx_read_aux_adc(bat_ps->dev->parent->driver_data,
				    WM97XX_AUX_ID2);
}

static int palmtx_bat_get_property(struct power_supply *bat_ps,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bat_status;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = palmtx_read_bat(bat_ps);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = PALMTX_BAT_MAX_VOLTAGE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = PALMTX_BAT_MIN_VOLTAGE;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = palmtx_read_temp(bat_ps);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void palmtx_bat_external_power_changed(struct power_supply *bat_ps)
{
	schedule_work(&bat_work);
}

static char *status_text[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN] =		"Unknown",
	[POWER_SUPPLY_STATUS_CHARGING] =	"Charging",
	[POWER_SUPPLY_STATUS_DISCHARGING] =	"Discharging",
};

static void palmtx_bat_update(struct power_supply *bat_ps)
{
	int old_status = bat_status;

	mutex_lock(&work_lock);

	bat_status = gpio_get_value(GPIO_NR_PALMTX_POWER_DETECT) ?
				    POWER_SUPPLY_STATUS_CHARGING :
				    POWER_SUPPLY_STATUS_DISCHARGING;

	if (old_status != bat_status) {
		pr_debug("%s %s -> %s\n", bat_ps->name,
				status_text[old_status],
				status_text[bat_status]);
		power_supply_changed(bat_ps);
	}

	mutex_unlock(&work_lock);
}

static enum power_supply_property palmtx_bat_main_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

struct power_supply bat_ps = {
	.name			= "main-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= palmtx_bat_main_props,
	.num_properties		= ARRAY_SIZE(palmtx_bat_main_props),
	.get_property		= palmtx_bat_get_property,
	.external_power_changed = palmtx_bat_external_power_changed,
	.use_for_apm		= 1,
};

static void palmtx_bat_work(struct work_struct *work)
{
	palmtx_bat_update(&bat_ps);
}

#ifdef CONFIG_PM
static int palmtx_bat_suspend(struct platform_device *dev, pm_message_t state)
{
	flush_scheduled_work();
	return 0;
}

static int palmtx_bat_resume(struct platform_device *dev)
{
	schedule_work(&bat_work);
	return 0;
}
#else
#define palmtx_bat_suspend NULL
#define palmtx_bat_resume NULL
#endif

static int __devinit palmtx_bat_probe(struct platform_device *dev)
{
	int ret = 0;

	if (!machine_is_palmtx())
		return -ENODEV;

	mutex_init(&work_lock);

	INIT_WORK(&bat_work, palmtx_bat_work);

	ret = power_supply_register(&dev->dev, &bat_ps);
	if (!ret)
		schedule_work(&bat_work);

	return ret;
}

static int __devexit palmtx_bat_remove(struct platform_device *dev)
{
	power_supply_unregister(&bat_ps);
	return 0;
}

static struct platform_driver palmtx_bat_driver = {
	.driver.name	= "wm97xx-battery",
	.driver.owner	= THIS_MODULE,
	.probe		= palmtx_bat_probe,
	.remove		= __devexit_p(palmtx_bat_remove),
	.suspend	= palmtx_bat_suspend,
	.resume		= palmtx_bat_resume,
};

static int __init palmtx_bat_init(void)
{
	return platform_driver_register(&palmtx_bat_driver);
}

static void __exit palmtx_bat_exit(void)
{
	platform_driver_unregister(&palmtx_bat_driver);
}

module_init(palmtx_bat_init);
module_exit(palmtx_bat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("Palm T|X battery driver");
