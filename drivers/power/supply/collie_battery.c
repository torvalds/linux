// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery and Power Management code for the Sharp SL-5x00
 *
 * Copyright (C) 2009 Thomas Kunze
 *
 * based on tosa_battery.c
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/ucb1x00.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/mach-types.h>
#include <mach/collie.h>

static DEFINE_MUTEX(bat_lock); /* protects gpio pins */
static struct work_struct bat_work;
static struct ucb1x00 *ucb;

struct collie_bat {
	int status;
	struct power_supply *psy;
	int full_chrg;

	struct mutex work_lock; /* protects data */

	bool (*is_present)(struct collie_bat *bat);
	struct gpio_desc *gpio_full;
	struct gpio_desc *gpio_charge_on;

	int technology;

	struct gpio_desc *gpio_bat;
	int adc_bat;
	int adc_bat_divider;
	int bat_max;
	int bat_min;

	struct gpio_desc *gpio_temp;
	int adc_temp;
	int adc_temp_divider;
};

static struct collie_bat collie_bat_main;

static unsigned long collie_read_bat(struct collie_bat *bat)
{
	unsigned long value = 0;

	if (!bat->gpio_bat || bat->adc_bat < 0)
		return 0;
	mutex_lock(&bat_lock);
	gpiod_set_value(bat->gpio_bat, 1);
	msleep(5);
	ucb1x00_adc_enable(ucb);
	value = ucb1x00_adc_read(ucb, bat->adc_bat, UCB_SYNC);
	ucb1x00_adc_disable(ucb);
	gpiod_set_value(bat->gpio_bat, 0);
	mutex_unlock(&bat_lock);
	value = value * 1000000 / bat->adc_bat_divider;

	return value;
}

static unsigned long collie_read_temp(struct collie_bat *bat)
{
	unsigned long value = 0;
	if (!bat->gpio_temp || bat->adc_temp < 0)
		return 0;

	mutex_lock(&bat_lock);
	gpiod_set_value(bat->gpio_temp, 1);
	msleep(5);
	ucb1x00_adc_enable(ucb);
	value = ucb1x00_adc_read(ucb, bat->adc_temp, UCB_SYNC);
	ucb1x00_adc_disable(ucb);
	gpiod_set_value(bat->gpio_temp, 0);
	mutex_unlock(&bat_lock);

	value = value * 10000 / bat->adc_temp_divider;

	return value;
}

static int collie_bat_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	int ret = 0;
	struct collie_bat *bat = power_supply_get_drvdata(psy);

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
		val->intval = collie_read_bat(bat);
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
		val->intval = collie_read_temp(bat);
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

static void collie_bat_external_power_changed(struct power_supply *psy)
{
	schedule_work(&bat_work);
}

static irqreturn_t collie_bat_gpio_isr(int irq, void *data)
{
	pr_info("collie_bat_gpio irq\n");
	schedule_work(&bat_work);
	return IRQ_HANDLED;
}

static void collie_bat_update(struct collie_bat *bat)
{
	int old;
	struct power_supply *psy = bat->psy;

	mutex_lock(&bat->work_lock);

	old = bat->status;

	if (bat->is_present && !bat->is_present(bat)) {
		printk(KERN_NOTICE "%s not present\n", psy->desc->name);
		bat->status = POWER_SUPPLY_STATUS_UNKNOWN;
		bat->full_chrg = -1;
	} else if (power_supply_am_i_supplied(psy)) {
		if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING) {
			gpiod_set_value(bat->gpio_charge_on, 1);
			mdelay(15);
		}

		if (gpiod_get_value(bat->gpio_full)) {
			if (old == POWER_SUPPLY_STATUS_CHARGING ||
					bat->full_chrg == -1)
				bat->full_chrg = collie_read_bat(bat);

			gpiod_set_value(bat->gpio_charge_on, 0);
			bat->status = POWER_SUPPLY_STATUS_FULL;
		} else {
			gpiod_set_value(bat->gpio_charge_on, 1);
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
		}
	} else {
		gpiod_set_value(bat->gpio_charge_on, 0);
		bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (old != bat->status)
		power_supply_changed(psy);

	mutex_unlock(&bat->work_lock);
}

static void collie_bat_work(struct work_struct *work)
{
	collie_bat_update(&collie_bat_main);
}


static enum power_supply_property collie_bat_main_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property collie_bat_bu_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc collie_bat_main_desc = {
	.name		= "main-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= collie_bat_main_props,
	.num_properties	= ARRAY_SIZE(collie_bat_main_props),
	.get_property	= collie_bat_get_property,
	.external_power_changed = collie_bat_external_power_changed,
	.use_for_apm	= 1,
};

static struct collie_bat collie_bat_main = {
	.status = POWER_SUPPLY_STATUS_DISCHARGING,
	.full_chrg = -1,
	.psy = NULL,

	.gpio_full = NULL,
	.gpio_charge_on = NULL,

	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,

	.gpio_bat = NULL,
	.adc_bat = UCB_ADC_INP_AD1,
	.adc_bat_divider = 155,
	.bat_max = 4310000,
	.bat_min = 1551 * 1000000 / 414,

	.gpio_temp = NULL,
	.adc_temp = UCB_ADC_INP_AD0,
	.adc_temp_divider = 10000,
};

static const struct power_supply_desc collie_bat_bu_desc = {
	.name		= "backup-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= collie_bat_bu_props,
	.num_properties	= ARRAY_SIZE(collie_bat_bu_props),
	.get_property	= collie_bat_get_property,
	.external_power_changed = collie_bat_external_power_changed,
};

static struct collie_bat collie_bat_bu = {
	.status = POWER_SUPPLY_STATUS_UNKNOWN,
	.full_chrg = -1,
	.psy = NULL,

	.gpio_full = NULL,
	.gpio_charge_on = NULL,

	.technology = POWER_SUPPLY_TECHNOLOGY_LiMn,

	.gpio_bat = NULL,
	.adc_bat = UCB_ADC_INP_AD1,
	.adc_bat_divider = 155,
	.bat_max = 3000000,
	.bat_min = 1900000,

	.gpio_temp = NULL,
	.adc_temp = -1,
	.adc_temp_divider = -1,
};

/* Obtained but unused GPIO */
static struct gpio_desc *collie_mbat_low;

#ifdef CONFIG_PM
static int wakeup_enabled;

static int collie_bat_suspend(struct ucb1x00_dev *dev)
{
	/* flush all pending status updates */
	flush_work(&bat_work);

	if (device_may_wakeup(&dev->ucb->dev) &&
	    collie_bat_main.status == POWER_SUPPLY_STATUS_CHARGING)
		wakeup_enabled = !enable_irq_wake(gpiod_to_irq(collie_bat_main.gpio_full));
	else
		wakeup_enabled = 0;

	return 0;
}

static int collie_bat_resume(struct ucb1x00_dev *dev)
{
	if (wakeup_enabled)
		disable_irq_wake(gpiod_to_irq(collie_bat_main.gpio_full));

	/* things may have changed while we were away */
	schedule_work(&bat_work);
	return 0;
}
#else
#define collie_bat_suspend NULL
#define collie_bat_resume NULL
#endif

static int collie_bat_probe(struct ucb1x00_dev *dev)
{
	int ret;
	struct power_supply_config psy_main_cfg = {}, psy_bu_cfg = {};
	struct gpio_chip *gc = &dev->ucb->gpio;

	if (!machine_is_collie())
		return -ENODEV;

	ucb = dev->ucb;

	/* Obtain all the main battery GPIOs */
	collie_bat_main.gpio_full = gpiod_get(&dev->ucb->dev,
					      "main battery full",
					      GPIOD_IN);
	if (IS_ERR(collie_bat_main.gpio_full))
		return PTR_ERR(collie_bat_main.gpio_full);

	collie_mbat_low = gpiod_get(&dev->ucb->dev,
				    "main battery low",
				    GPIOD_IN);
	if (IS_ERR(collie_mbat_low)) {
		ret = PTR_ERR(collie_mbat_low);
		goto err_put_gpio_full;
	}

	collie_bat_main.gpio_charge_on = gpiod_get(&dev->ucb->dev,
						   "main charge on",
						   GPIOD_OUT_LOW);
	if (IS_ERR(collie_bat_main.gpio_charge_on)) {
		ret = PTR_ERR(collie_bat_main.gpio_charge_on);
		goto err_put_mbat_low;
	}

	/* COLLIE_GPIO_MBAT_ON = GPIO 7 on the UCB (TC35143) */
	collie_bat_main.gpio_bat = gpiochip_request_own_desc(gc,
						7,
						"main battery",
						GPIO_ACTIVE_HIGH,
						GPIOD_OUT_LOW);
	if (IS_ERR(collie_bat_main.gpio_bat)) {
		ret = PTR_ERR(collie_bat_main.gpio_bat);
		goto err_put_gpio_charge_on;
	}

	/* COLLIE_GPIO_TMP_ON = GPIO 9 on the UCB (TC35143) */
	collie_bat_main.gpio_temp = gpiochip_request_own_desc(gc,
						9,
						"main battery temp",
						GPIO_ACTIVE_HIGH,
						GPIOD_OUT_LOW);
	if (IS_ERR(collie_bat_main.gpio_temp)) {
		ret = PTR_ERR(collie_bat_main.gpio_temp);
		goto err_free_gpio_bat;
	}

	/*
	 * Obtain the backup battery COLLIE_GPIO_BBAT_ON which is
	 * GPIO 8 on the UCB (TC35143)
	 */
	collie_bat_bu.gpio_bat = gpiochip_request_own_desc(gc,
						8,
						"backup battery",
						GPIO_ACTIVE_HIGH,
						GPIOD_OUT_LOW);
	if (IS_ERR(collie_bat_bu.gpio_bat)) {
		ret = PTR_ERR(collie_bat_bu.gpio_bat);
		goto err_free_gpio_temp;
	}

	mutex_init(&collie_bat_main.work_lock);

	INIT_WORK(&bat_work, collie_bat_work);

	psy_main_cfg.drv_data = &collie_bat_main;
	collie_bat_main.psy = power_supply_register(&dev->ucb->dev,
						    &collie_bat_main_desc,
						    &psy_main_cfg);
	if (IS_ERR(collie_bat_main.psy)) {
		ret = PTR_ERR(collie_bat_main.psy);
		goto err_psy_reg_main;
	}

	psy_bu_cfg.drv_data = &collie_bat_bu;
	collie_bat_bu.psy = power_supply_register(&dev->ucb->dev,
						  &collie_bat_bu_desc,
						  &psy_bu_cfg);
	if (IS_ERR(collie_bat_bu.psy)) {
		ret = PTR_ERR(collie_bat_bu.psy);
		goto err_psy_reg_bu;
	}

	ret = request_irq(gpiod_to_irq(collie_bat_main.gpio_full),
				collie_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"main full", &collie_bat_main);
	if (ret)
		goto err_irq;

	device_init_wakeup(&ucb->dev, 1);
	schedule_work(&bat_work);

	return 0;

err_irq:
	power_supply_unregister(collie_bat_bu.psy);
err_psy_reg_bu:
	power_supply_unregister(collie_bat_main.psy);
err_psy_reg_main:
	/* see comment in collie_bat_remove */
	cancel_work_sync(&bat_work);
	gpiochip_free_own_desc(collie_bat_bu.gpio_bat);
err_free_gpio_temp:
	gpiochip_free_own_desc(collie_bat_main.gpio_temp);
err_free_gpio_bat:
	gpiochip_free_own_desc(collie_bat_main.gpio_bat);
err_put_gpio_charge_on:
	gpiod_put(collie_bat_main.gpio_charge_on);
err_put_mbat_low:
	gpiod_put(collie_mbat_low);
err_put_gpio_full:
	gpiod_put(collie_bat_main.gpio_full);

	return ret;
}

static void collie_bat_remove(struct ucb1x00_dev *dev)
{
	free_irq(gpiod_to_irq(collie_bat_main.gpio_full), &collie_bat_main);
	power_supply_unregister(collie_bat_bu.psy);
	power_supply_unregister(collie_bat_main.psy);

	/* These are obtained from the machine */
	gpiod_put(collie_bat_main.gpio_full);
	gpiod_put(collie_mbat_low);
	gpiod_put(collie_bat_main.gpio_charge_on);
	/* These are directly from the UCB so let's free them */
	gpiochip_free_own_desc(collie_bat_main.gpio_bat);
	gpiochip_free_own_desc(collie_bat_main.gpio_temp);
	gpiochip_free_own_desc(collie_bat_bu.gpio_bat);
	/*
	 * Now cancel the bat_work.  We won't get any more schedules,
	 * since all sources (isr and external_power_changed) are
	 * unregistered now.
	 */
	cancel_work_sync(&bat_work);
}

static struct ucb1x00_driver collie_bat_driver = {
	.add		= collie_bat_probe,
	.remove		= collie_bat_remove,
	.suspend	= collie_bat_suspend,
	.resume		= collie_bat_resume,
};

static int __init collie_bat_init(void)
{
	return ucb1x00_register_driver(&collie_bat_driver);
}

static void __exit collie_bat_exit(void)
{
	ucb1x00_unregister_driver(&collie_bat_driver);
}

module_init(collie_bat_init);
module_exit(collie_bat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Kunze");
MODULE_DESCRIPTION("Collie battery driver");
