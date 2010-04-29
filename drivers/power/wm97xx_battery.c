/*
 * linux/drivers/power/wm97xx_battery.c
 *
 * Battery measurement code for WM97xx
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/wm97xx.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/slab.h>

static DEFINE_MUTEX(bat_lock);
static struct work_struct bat_work;
static struct mutex work_lock;
static int bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
static struct wm97xx_batt_info *gpdata;
static enum power_supply_property *prop;

static unsigned long wm97xx_read_bat(struct power_supply *bat_ps)
{
	struct wm97xx_pdata *wmdata = bat_ps->dev->parent->platform_data;
	struct wm97xx_batt_pdata *pdata = wmdata->batt_pdata;

	return wm97xx_read_aux_adc(dev_get_drvdata(bat_ps->dev->parent),
					pdata->batt_aux) * pdata->batt_mult /
					pdata->batt_div;
}

static unsigned long wm97xx_read_temp(struct power_supply *bat_ps)
{
	struct wm97xx_pdata *wmdata = bat_ps->dev->parent->platform_data;
	struct wm97xx_batt_pdata *pdata = wmdata->batt_pdata;

	return wm97xx_read_aux_adc(dev_get_drvdata(bat_ps->dev->parent),
					pdata->temp_aux) * pdata->temp_mult /
					pdata->temp_div;
}

static int wm97xx_bat_get_property(struct power_supply *bat_ps,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct wm97xx_pdata *wmdata = bat_ps->dev->parent->platform_data;
	struct wm97xx_batt_pdata *pdata = wmdata->batt_pdata;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bat_status;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = pdata->batt_tech;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (pdata->batt_aux >= 0)
			val->intval = wm97xx_read_bat(bat_ps);
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (pdata->temp_aux >= 0)
			val->intval = wm97xx_read_temp(bat_ps);
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (pdata->max_voltage >= 0)
			val->intval = pdata->max_voltage;
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		if (pdata->min_voltage >= 0)
			val->intval = pdata->min_voltage;
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void wm97xx_bat_external_power_changed(struct power_supply *bat_ps)
{
	schedule_work(&bat_work);
}

static void wm97xx_bat_update(struct power_supply *bat_ps)
{
	int old_status = bat_status;
	struct wm97xx_pdata *wmdata = bat_ps->dev->parent->platform_data;
	struct wm97xx_batt_pdata *pdata = wmdata->batt_pdata;

	mutex_lock(&work_lock);

	bat_status = (pdata->charge_gpio >= 0) ?
			(gpio_get_value(pdata->charge_gpio) ?
			POWER_SUPPLY_STATUS_DISCHARGING :
			POWER_SUPPLY_STATUS_CHARGING) :
			POWER_SUPPLY_STATUS_UNKNOWN;

	if (old_status != bat_status) {
		pr_debug("%s: %i -> %i\n", bat_ps->name, old_status,
					bat_status);
		power_supply_changed(bat_ps);
	}

	mutex_unlock(&work_lock);
}

static struct power_supply bat_ps = {
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.get_property		= wm97xx_bat_get_property,
	.external_power_changed = wm97xx_bat_external_power_changed,
	.use_for_apm		= 1,
};

static void wm97xx_bat_work(struct work_struct *work)
{
	wm97xx_bat_update(&bat_ps);
}

static irqreturn_t wm97xx_chrg_irq(int irq, void *data)
{
	schedule_work(&bat_work);
	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int wm97xx_bat_suspend(struct device *dev)
{
	flush_scheduled_work();
	return 0;
}

static int wm97xx_bat_resume(struct device *dev)
{
	schedule_work(&bat_work);
	return 0;
}

static const struct dev_pm_ops wm97xx_bat_pm_ops = {
	.suspend	= wm97xx_bat_suspend,
	.resume		= wm97xx_bat_resume,
};
#endif

static int __devinit wm97xx_bat_probe(struct platform_device *dev)
{
	int ret = 0;
	int props = 1;	/* POWER_SUPPLY_PROP_PRESENT */
	int i = 0;
	struct wm97xx_pdata *wmdata = dev->dev.platform_data;
	struct wm97xx_batt_pdata *pdata;

	if (gpdata) {
		dev_err(&dev->dev, "Do not pass platform_data through "
			"wm97xx_bat_set_pdata!\n");
		return -EINVAL;
	}

	if (!wmdata) {
		dev_err(&dev->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	pdata = wmdata->batt_pdata;

	if (dev->id != -1)
		return -EINVAL;

	mutex_init(&work_lock);

	if (!pdata) {
		dev_err(&dev->dev, "No platform_data supplied\n");
		return -EINVAL;
	}

	if (gpio_is_valid(pdata->charge_gpio)) {
		ret = gpio_request(pdata->charge_gpio, "BATT CHRG");
		if (ret)
			goto err;
		ret = gpio_direction_input(pdata->charge_gpio);
		if (ret)
			goto err2;
		ret = request_irq(gpio_to_irq(pdata->charge_gpio),
				wm97xx_chrg_irq, IRQF_DISABLED,
				"AC Detect", dev);
		if (ret)
			goto err2;
		props++;	/* POWER_SUPPLY_PROP_STATUS */
	}

	if (pdata->batt_tech >= 0)
		props++;	/* POWER_SUPPLY_PROP_TECHNOLOGY */
	if (pdata->temp_aux >= 0)
		props++;	/* POWER_SUPPLY_PROP_TEMP */
	if (pdata->batt_aux >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_NOW */
	if (pdata->max_voltage >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_MAX */
	if (pdata->min_voltage >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_MIN */

	prop = kzalloc(props * sizeof(*prop), GFP_KERNEL);
	if (!prop)
		goto err3;

	prop[i++] = POWER_SUPPLY_PROP_PRESENT;
	if (pdata->charge_gpio >= 0)
		prop[i++] = POWER_SUPPLY_PROP_STATUS;
	if (pdata->batt_tech >= 0)
		prop[i++] = POWER_SUPPLY_PROP_TECHNOLOGY;
	if (pdata->temp_aux >= 0)
		prop[i++] = POWER_SUPPLY_PROP_TEMP;
	if (pdata->batt_aux >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_NOW;
	if (pdata->max_voltage >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_MAX;
	if (pdata->min_voltage >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_MIN;

	INIT_WORK(&bat_work, wm97xx_bat_work);

	if (!pdata->batt_name) {
		dev_info(&dev->dev, "Please consider setting proper battery "
				"name in platform definition file, falling "
				"back to name \"wm97xx-batt\"\n");
		bat_ps.name = "wm97xx-batt";
	} else
		bat_ps.name = pdata->batt_name;

	bat_ps.properties = prop;
	bat_ps.num_properties = props;

	ret = power_supply_register(&dev->dev, &bat_ps);
	if (!ret)
		schedule_work(&bat_work);
	else
		goto err4;

	return 0;
err4:
	kfree(prop);
err3:
	if (gpio_is_valid(pdata->charge_gpio))
		free_irq(gpio_to_irq(pdata->charge_gpio), dev);
err2:
	if (gpio_is_valid(pdata->charge_gpio))
		gpio_free(pdata->charge_gpio);
err:
	return ret;
}

static int __devexit wm97xx_bat_remove(struct platform_device *dev)
{
	struct wm97xx_pdata *wmdata = dev->dev.platform_data;
	struct wm97xx_batt_pdata *pdata = wmdata->batt_pdata;

	if (pdata && gpio_is_valid(pdata->charge_gpio)) {
		free_irq(gpio_to_irq(pdata->charge_gpio), dev);
		gpio_free(pdata->charge_gpio);
	}
	flush_scheduled_work();
	power_supply_unregister(&bat_ps);
	kfree(prop);
	return 0;
}

static struct platform_driver wm97xx_bat_driver = {
	.driver	= {
		.name	= "wm97xx-battery",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &wm97xx_bat_pm_ops,
#endif
	},
	.probe		= wm97xx_bat_probe,
	.remove		= __devexit_p(wm97xx_bat_remove),
};

static int __init wm97xx_bat_init(void)
{
	return platform_driver_register(&wm97xx_bat_driver);
}

static void __exit wm97xx_bat_exit(void)
{
	platform_driver_unregister(&wm97xx_bat_driver);
}

void wm97xx_bat_set_pdata(struct wm97xx_batt_info *data)
{
	gpdata = data;
}
EXPORT_SYMBOL_GPL(wm97xx_bat_set_pdata);

module_init(wm97xx_bat_init);
module_exit(wm97xx_bat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("WM97xx battery driver");
