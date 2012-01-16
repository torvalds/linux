/*
 * Battery and Power Management code for the Sharp SL-6000x
 *
 * Copyright (c) 2005 Dirk Opfer
 * Copyright (c) 2008 Dmitry Baryshkov
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
#include <mach/tosa.h>

static DEFINE_MUTEX(bat_lock); /* protects gpio pins */
static struct work_struct bat_work;

struct tosa_bat {
	int status;
	struct power_supply psy;
	int full_chrg;

	struct mutex work_lock; /* protects data */

	bool (*is_present)(struct tosa_bat *bat);
	int gpio_full;
	int gpio_charge_off;

	int technology;

	int gpio_bat;
	int adc_bat;
	int adc_bat_divider;
	int bat_max;
	int bat_min;

	int gpio_temp;
	int adc_temp;
	int adc_temp_divider;
};

static struct tosa_bat tosa_bat_main;
static struct tosa_bat tosa_bat_jacket;

static unsigned long tosa_read_bat(struct tosa_bat *bat)
{
	unsigned long value = 0;

	if (bat->gpio_bat < 0 || bat->adc_bat < 0)
		return 0;

	mutex_lock(&bat_lock);
	gpio_set_value(bat->gpio_bat, 1);
	msleep(5);
	value = wm97xx_read_aux_adc(dev_get_drvdata(bat->psy.dev->parent),
			bat->adc_bat);
	gpio_set_value(bat->gpio_bat, 0);
	mutex_unlock(&bat_lock);

	value = value * 1000000 / bat->adc_bat_divider;

	return value;
}

static unsigned long tosa_read_temp(struct tosa_bat *bat)
{
	unsigned long value = 0;

	if (bat->gpio_temp < 0 || bat->adc_temp < 0)
		return 0;

	mutex_lock(&bat_lock);
	gpio_set_value(bat->gpio_temp, 1);
	msleep(5);
	value = wm97xx_read_aux_adc(dev_get_drvdata(bat->psy.dev->parent),
			bat->adc_temp);
	gpio_set_value(bat->gpio_temp, 0);
	mutex_unlock(&bat_lock);

	value = value * 10000 / bat->adc_temp_divider;

	return value;
}

static int tosa_bat_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	int ret = 0;
	struct tosa_bat *bat = container_of(psy, struct tosa_bat, psy);

	if (bat->is_present && !bat->is_present(bat)
			&& psp != POWER_SUPPLY_PROP_PRESENT) {
		return -ENODEV;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bat->status;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = bat->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = tosa_read_bat(bat);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (bat->full_chrg == -1)
			val->intval = bat->bat_max;
		else
			val->intval = bat->full_chrg;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = bat->bat_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = bat->bat_min;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = tosa_read_temp(bat);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bat->is_present ? bat->is_present(bat) : 1;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static bool tosa_jacket_bat_is_present(struct tosa_bat *bat)
{
	return gpio_get_value(TOSA_GPIO_JACKET_DETECT) == 0;
}

static void tosa_bat_external_power_changed(struct power_supply *psy)
{
	schedule_work(&bat_work);
}

static irqreturn_t tosa_bat_gpio_isr(int irq, void *data)
{
	pr_info("tosa_bat_gpio irq: %d\n", gpio_get_value(irq_to_gpio(irq)));
	schedule_work(&bat_work);
	return IRQ_HANDLED;
}

static void tosa_bat_update(struct tosa_bat *bat)
{
	int old;
	struct power_supply *psy = &bat->psy;

	mutex_lock(&bat->work_lock);

	old = bat->status;

	if (bat->is_present && !bat->is_present(bat)) {
		printk(KERN_NOTICE "%s not present\n", psy->name);
		bat->status = POWER_SUPPLY_STATUS_UNKNOWN;
		bat->full_chrg = -1;
	} else if (power_supply_am_i_supplied(psy)) {
		if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING) {
			gpio_set_value(bat->gpio_charge_off, 0);
			mdelay(15);
		}

		if (gpio_get_value(bat->gpio_full)) {
			if (old == POWER_SUPPLY_STATUS_CHARGING ||
					bat->full_chrg == -1)
				bat->full_chrg = tosa_read_bat(bat);

			gpio_set_value(bat->gpio_charge_off, 1);
			bat->status = POWER_SUPPLY_STATUS_FULL;
		} else {
			gpio_set_value(bat->gpio_charge_off, 0);
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
		}
	} else {
		gpio_set_value(bat->gpio_charge_off, 1);
		bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (old != bat->status)
		power_supply_changed(psy);

	mutex_unlock(&bat->work_lock);
}

static void tosa_bat_work(struct work_struct *work)
{
	tosa_bat_update(&tosa_bat_main);
	tosa_bat_update(&tosa_bat_jacket);
}


static enum power_supply_property tosa_bat_main_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

static enum power_supply_property tosa_bat_bu_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_PRESENT,
};

static struct tosa_bat tosa_bat_main = {
	.status = POWER_SUPPLY_STATUS_DISCHARGING,
	.full_chrg = -1,
	.psy = {
		.name		= "main-battery",
		.type		= POWER_SUPPLY_TYPE_BATTERY,
		.properties	= tosa_bat_main_props,
		.num_properties	= ARRAY_SIZE(tosa_bat_main_props),
		.get_property	= tosa_bat_get_property,
		.external_power_changed = tosa_bat_external_power_changed,
		.use_for_apm	= 1,
	},

	.gpio_full = TOSA_GPIO_BAT0_CRG,
	.gpio_charge_off = TOSA_GPIO_CHARGE_OFF,

	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,

	.gpio_bat = TOSA_GPIO_BAT0_V_ON,
	.adc_bat = WM97XX_AUX_ID3,
	.adc_bat_divider = 414,
	.bat_max = 4310000,
	.bat_min = 1551 * 1000000 / 414,

	.gpio_temp = TOSA_GPIO_BAT1_TH_ON,
	.adc_temp = WM97XX_AUX_ID2,
	.adc_temp_divider = 10000,
};

static struct tosa_bat tosa_bat_jacket = {
	.status = POWER_SUPPLY_STATUS_DISCHARGING,
	.full_chrg = -1,
	.psy = {
		.name		= "jacket-battery",
		.type		= POWER_SUPPLY_TYPE_BATTERY,
		.properties	= tosa_bat_main_props,
		.num_properties	= ARRAY_SIZE(tosa_bat_main_props),
		.get_property	= tosa_bat_get_property,
		.external_power_changed = tosa_bat_external_power_changed,
	},

	.is_present = tosa_jacket_bat_is_present,
	.gpio_full = TOSA_GPIO_BAT1_CRG,
	.gpio_charge_off = TOSA_GPIO_CHARGE_OFF_JC,

	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,

	.gpio_bat = TOSA_GPIO_BAT1_V_ON,
	.adc_bat = WM97XX_AUX_ID3,
	.adc_bat_divider = 414,
	.bat_max = 4310000,
	.bat_min = 1551 * 1000000 / 414,

	.gpio_temp = TOSA_GPIO_BAT0_TH_ON,
	.adc_temp = WM97XX_AUX_ID2,
	.adc_temp_divider = 10000,
};

static struct tosa_bat tosa_bat_bu = {
	.status = POWER_SUPPLY_STATUS_UNKNOWN,
	.full_chrg = -1,

	.psy = {
		.name		= "backup-battery",
		.type		= POWER_SUPPLY_TYPE_BATTERY,
		.properties	= tosa_bat_bu_props,
		.num_properties	= ARRAY_SIZE(tosa_bat_bu_props),
		.get_property	= tosa_bat_get_property,
		.external_power_changed = tosa_bat_external_power_changed,
	},

	.gpio_full = -1,
	.gpio_charge_off = -1,

	.technology = POWER_SUPPLY_TECHNOLOGY_LiMn,

	.gpio_bat = TOSA_GPIO_BU_CHRG_ON,
	.adc_bat = WM97XX_AUX_ID4,
	.adc_bat_divider = 1266,

	.gpio_temp = -1,
	.adc_temp = -1,
	.adc_temp_divider = -1,
};

static struct gpio tosa_bat_gpios[] = {
	{ TOSA_GPIO_CHARGE_OFF,	   GPIOF_OUT_INIT_HIGH, "main charge off" },
	{ TOSA_GPIO_CHARGE_OFF_JC, GPIOF_OUT_INIT_HIGH, "jacket charge off" },
	{ TOSA_GPIO_BAT_SW_ON,	   GPIOF_OUT_INIT_LOW,	"battery switch" },
	{ TOSA_GPIO_BAT0_V_ON,	   GPIOF_OUT_INIT_LOW,	"main battery" },
	{ TOSA_GPIO_BAT1_V_ON,	   GPIOF_OUT_INIT_LOW,	"jacket battery" },
	{ TOSA_GPIO_BAT1_TH_ON,	   GPIOF_OUT_INIT_LOW,	"main battery temp" },
	{ TOSA_GPIO_BAT0_TH_ON,	   GPIOF_OUT_INIT_LOW,	"jacket battery temp" },
	{ TOSA_GPIO_BU_CHRG_ON,	   GPIOF_OUT_INIT_LOW,	"backup battery" },
	{ TOSA_GPIO_BAT0_CRG,	   GPIOF_IN,		"main battery full" },
	{ TOSA_GPIO_BAT1_CRG,	   GPIOF_IN,		"jacket battery full" },
	{ TOSA_GPIO_BAT0_LOW,	   GPIOF_IN,		"main battery low" },
	{ TOSA_GPIO_BAT1_LOW,	   GPIOF_IN,		"jacket battery low" },
	{ TOSA_GPIO_JACKET_DETECT, GPIOF_IN,		"jacket detect" },
};

#ifdef CONFIG_PM
static int tosa_bat_suspend(struct platform_device *dev, pm_message_t state)
{
	/* flush all pending status updates */
	flush_work_sync(&bat_work);
	return 0;
}

static int tosa_bat_resume(struct platform_device *dev)
{
	/* things may have changed while we were away */
	schedule_work(&bat_work);
	return 0;
}
#else
#define tosa_bat_suspend NULL
#define tosa_bat_resume NULL
#endif

static int __devinit tosa_bat_probe(struct platform_device *dev)
{
	int ret;

	if (!machine_is_tosa())
		return -ENODEV;

	ret = gpio_request_array(tosa_bat_gpios, ARRAY_SIZE(tosa_bat_gpios));
	if (ret)
		return ret;

	mutex_init(&tosa_bat_main.work_lock);
	mutex_init(&tosa_bat_jacket.work_lock);

	INIT_WORK(&bat_work, tosa_bat_work);

	ret = power_supply_register(&dev->dev, &tosa_bat_main.psy);
	if (ret)
		goto err_psy_reg_main;
	ret = power_supply_register(&dev->dev, &tosa_bat_jacket.psy);
	if (ret)
		goto err_psy_reg_jacket;
	ret = power_supply_register(&dev->dev, &tosa_bat_bu.psy);
	if (ret)
		goto err_psy_reg_bu;

	ret = request_irq(gpio_to_irq(TOSA_GPIO_BAT0_CRG),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"main full", &tosa_bat_main);
	if (ret)
		goto err_req_main;

	ret = request_irq(gpio_to_irq(TOSA_GPIO_BAT1_CRG),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"jacket full", &tosa_bat_jacket);
	if (ret)
		goto err_req_jacket;

	ret = request_irq(gpio_to_irq(TOSA_GPIO_JACKET_DETECT),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"jacket detect", &tosa_bat_jacket);
	if (!ret) {
		schedule_work(&bat_work);
		return 0;
	}

	free_irq(gpio_to_irq(TOSA_GPIO_BAT1_CRG), &tosa_bat_jacket);
err_req_jacket:
	free_irq(gpio_to_irq(TOSA_GPIO_BAT0_CRG), &tosa_bat_main);
err_req_main:
	power_supply_unregister(&tosa_bat_bu.psy);
err_psy_reg_bu:
	power_supply_unregister(&tosa_bat_jacket.psy);
err_psy_reg_jacket:
	power_supply_unregister(&tosa_bat_main.psy);
err_psy_reg_main:

	/* see comment in tosa_bat_remove */
	cancel_work_sync(&bat_work);

	gpio_free_array(tosa_bat_gpios, ARRAY_SIZE(tosa_bat_gpios));
	return ret;
}

static int __devexit tosa_bat_remove(struct platform_device *dev)
{
	free_irq(gpio_to_irq(TOSA_GPIO_JACKET_DETECT), &tosa_bat_jacket);
	free_irq(gpio_to_irq(TOSA_GPIO_BAT1_CRG), &tosa_bat_jacket);
	free_irq(gpio_to_irq(TOSA_GPIO_BAT0_CRG), &tosa_bat_main);

	power_supply_unregister(&tosa_bat_bu.psy);
	power_supply_unregister(&tosa_bat_jacket.psy);
	power_supply_unregister(&tosa_bat_main.psy);

	/*
	 * Now cancel the bat_work.  We won't get any more schedules,
	 * since all sources (isr and external_power_changed) are
	 * unregistered now.
	 */
	cancel_work_sync(&bat_work);
	gpio_free_array(tosa_bat_gpios, ARRAY_SIZE(tosa_bat_gpios));
	return 0;
}

static struct platform_driver tosa_bat_driver = {
	.driver.name	= "wm97xx-battery",
	.driver.owner	= THIS_MODULE,
	.probe		= tosa_bat_probe,
	.remove		= __devexit_p(tosa_bat_remove),
	.suspend	= tosa_bat_suspend,
	.resume		= tosa_bat_resume,
};

module_platform_driver(tosa_bat_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Baryshkov");
MODULE_DESCRIPTION("Tosa battery driver");
MODULE_ALIAS("platform:wm97xx-battery");
