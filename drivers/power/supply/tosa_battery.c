// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery and Power Management code for the Sharp SL-6000x
 *
 * Copyright (c) 2005 Dirk Opfer
 * Copyright (c) 2008 Dmitry Baryshkov
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/wm97xx.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>

#include <asm/mach-types.h>

static DEFINE_MUTEX(bat_lock); /* protects gpio pins */
static struct work_struct bat_work;

struct tosa_bat {
	int status;
	struct power_supply *psy;
	int full_chrg;

	struct mutex work_lock; /* protects data */

	bool (*is_present)(struct tosa_bat *bat);
	struct gpio_desc *gpiod_full;
	struct gpio_desc *gpiod_charge_off;

	int technology;

	struct gpio_desc *gpiod_bat;
	int adc_bat;
	int adc_bat_divider;
	int bat_max;
	int bat_min;

	struct gpio_desc *gpiod_temp;
	int adc_temp;
	int adc_temp_divider;
};

static struct gpio_desc *jacket_detect;
static struct tosa_bat tosa_bat_main;
static struct tosa_bat tosa_bat_jacket;

static unsigned long tosa_read_bat(struct tosa_bat *bat)
{
	unsigned long value = 0;

	if (!bat->gpiod_bat || bat->adc_bat < 0)
		return 0;

	mutex_lock(&bat_lock);
	gpiod_set_value(bat->gpiod_bat, 1);
	msleep(5);
	value = wm97xx_read_aux_adc(dev_get_drvdata(bat->psy->dev.parent),
			bat->adc_bat);
	gpiod_set_value(bat->gpiod_bat, 0);
	mutex_unlock(&bat_lock);

	value = value * 1000000 / bat->adc_bat_divider;

	return value;
}

static unsigned long tosa_read_temp(struct tosa_bat *bat)
{
	unsigned long value = 0;

	if (!bat->gpiod_temp || bat->adc_temp < 0)
		return 0;

	mutex_lock(&bat_lock);
	gpiod_set_value(bat->gpiod_temp, 1);
	msleep(5);
	value = wm97xx_read_aux_adc(dev_get_drvdata(bat->psy->dev.parent),
			bat->adc_temp);
	gpiod_set_value(bat->gpiod_temp, 0);
	mutex_unlock(&bat_lock);

	value = value * 10000 / bat->adc_temp_divider;

	return value;
}

static int tosa_bat_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	int ret = 0;
	struct tosa_bat *bat = power_supply_get_drvdata(psy);

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
	return gpiod_get_value(jacket_detect) == 0;
}

static void tosa_bat_external_power_changed(struct power_supply *psy)
{
	schedule_work(&bat_work);
}

static irqreturn_t tosa_bat_gpio_isr(int irq, void *data)
{
	pr_info("tosa_bat_gpio irq\n");
	schedule_work(&bat_work);
	return IRQ_HANDLED;
}

static void tosa_bat_update(struct tosa_bat *bat)
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
			gpiod_set_value(bat->gpiod_charge_off, 0);
			mdelay(15);
		}

		if (gpiod_get_value(bat->gpiod_full)) {
			if (old == POWER_SUPPLY_STATUS_CHARGING ||
					bat->full_chrg == -1)
				bat->full_chrg = tosa_read_bat(bat);

			gpiod_set_value(bat->gpiod_charge_off, 1);
			bat->status = POWER_SUPPLY_STATUS_FULL;
		} else {
			gpiod_set_value(bat->gpiod_charge_off, 0);
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
		}
	} else {
		gpiod_set_value(bat->gpiod_charge_off, 1);
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

static const struct power_supply_desc tosa_bat_main_desc = {
	.name		= "main-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= tosa_bat_main_props,
	.num_properties	= ARRAY_SIZE(tosa_bat_main_props),
	.get_property	= tosa_bat_get_property,
	.external_power_changed = tosa_bat_external_power_changed,
	.use_for_apm	= 1,
};

static const struct power_supply_desc tosa_bat_jacket_desc = {
	.name		= "jacket-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= tosa_bat_main_props,
	.num_properties	= ARRAY_SIZE(tosa_bat_main_props),
	.get_property	= tosa_bat_get_property,
	.external_power_changed = tosa_bat_external_power_changed,
};

static const struct power_supply_desc tosa_bat_bu_desc = {
	.name		= "backup-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= tosa_bat_bu_props,
	.num_properties	= ARRAY_SIZE(tosa_bat_bu_props),
	.get_property	= tosa_bat_get_property,
	.external_power_changed = tosa_bat_external_power_changed,
};

static struct tosa_bat tosa_bat_main = {
	.status = POWER_SUPPLY_STATUS_DISCHARGING,
	.full_chrg = -1,
	.psy = NULL,

	.gpiod_full = NULL,
	.gpiod_charge_off = NULL,

	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,

	.gpiod_bat = NULL,
	.adc_bat = WM97XX_AUX_ID3,
	.adc_bat_divider = 414,
	.bat_max = 4310000,
	.bat_min = 1551 * 1000000 / 414,

	.gpiod_temp = NULL,
	.adc_temp = WM97XX_AUX_ID2,
	.adc_temp_divider = 10000,
};

static struct tosa_bat tosa_bat_jacket = {
	.status = POWER_SUPPLY_STATUS_DISCHARGING,
	.full_chrg = -1,
	.psy = NULL,

	.is_present = tosa_jacket_bat_is_present,
	.gpiod_full = NULL,
	.gpiod_charge_off = NULL,

	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,

	.gpiod_bat = NULL,
	.adc_bat = WM97XX_AUX_ID3,
	.adc_bat_divider = 414,
	.bat_max = 4310000,
	.bat_min = 1551 * 1000000 / 414,

	.gpiod_temp = NULL,
	.adc_temp = WM97XX_AUX_ID2,
	.adc_temp_divider = 10000,
};

static struct tosa_bat tosa_bat_bu = {
	.status = POWER_SUPPLY_STATUS_UNKNOWN,
	.full_chrg = -1,
	.psy = NULL,

	.gpiod_full = NULL,
	.gpiod_charge_off = NULL,

	.technology = POWER_SUPPLY_TECHNOLOGY_LiMn,

	.gpiod_bat = NULL,
	.adc_bat = WM97XX_AUX_ID4,
	.adc_bat_divider = 1266,

	.gpiod_temp = NULL,
	.adc_temp = -1,
	.adc_temp_divider = -1,
};

#ifdef CONFIG_PM
static int tosa_bat_suspend(struct platform_device *dev, pm_message_t state)
{
	/* flush all pending status updates */
	flush_work(&bat_work);
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

static int tosa_bat_probe(struct platform_device *pdev)
{
	int ret;
	struct power_supply_config main_psy_cfg = {},
				   jacket_psy_cfg = {},
				   bu_psy_cfg = {};
	struct device *dev = &pdev->dev;
	struct gpio_desc *dummy;

	if (!machine_is_tosa())
		return -ENODEV;

	/* Main charging control GPIOs */
	tosa_bat_main.gpiod_charge_off = devm_gpiod_get(dev, "main charge off", GPIOD_OUT_HIGH);
	if (IS_ERR(tosa_bat_main.gpiod_charge_off))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_main.gpiod_charge_off),
				     "no main charger GPIO\n");
	tosa_bat_jacket.gpiod_charge_off = devm_gpiod_get(dev, "jacket charge off", GPIOD_OUT_HIGH);
	if (IS_ERR(tosa_bat_jacket.gpiod_charge_off))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_jacket.gpiod_charge_off),
				     "no jacket charger GPIO\n");

	/* Per-battery output check (routes battery voltage to ADC) */
	tosa_bat_main.gpiod_bat = devm_gpiod_get(dev, "main battery", GPIOD_OUT_LOW);
	if (IS_ERR(tosa_bat_main.gpiod_bat))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_main.gpiod_bat),
				     "no main battery GPIO\n");
	tosa_bat_jacket.gpiod_bat = devm_gpiod_get(dev, "jacket battery", GPIOD_OUT_LOW);
	if (IS_ERR(tosa_bat_jacket.gpiod_bat))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_jacket.gpiod_bat),
				     "no jacket battery GPIO\n");
	tosa_bat_bu.gpiod_bat = devm_gpiod_get(dev, "backup battery", GPIOD_OUT_LOW);
	if (IS_ERR(tosa_bat_bu.gpiod_bat))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_bu.gpiod_bat),
				     "no backup battery GPIO\n");

	/* Battery full detect GPIOs (using PXA SoC GPIOs) */
	tosa_bat_main.gpiod_full = devm_gpiod_get(dev, "main battery full", GPIOD_IN);
	if (IS_ERR(tosa_bat_main.gpiod_full))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_main.gpiod_full),
				     "no main battery full GPIO\n");
	tosa_bat_jacket.gpiod_full = devm_gpiod_get(dev, "jacket battery full", GPIOD_IN);
	if (IS_ERR(tosa_bat_jacket.gpiod_full))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_jacket.gpiod_full),
				     "no jacket battery full GPIO\n");

	/* Battery temperature GPIOs (routes thermistor voltage to ADC) */
	tosa_bat_main.gpiod_temp = devm_gpiod_get(dev, "main battery temp", GPIOD_OUT_LOW);
	if (IS_ERR(tosa_bat_main.gpiod_temp))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_main.gpiod_temp),
				     "no main battery temp GPIO\n");
	tosa_bat_jacket.gpiod_temp = devm_gpiod_get(dev, "jacket battery temp", GPIOD_OUT_LOW);
	if (IS_ERR(tosa_bat_jacket.gpiod_temp))
		return dev_err_probe(dev, PTR_ERR(tosa_bat_jacket.gpiod_temp),
				     "no jacket battery temp GPIO\n");

	/* Jacket detect GPIO */
	jacket_detect = devm_gpiod_get(dev, "jacket detect", GPIOD_IN);
	if (IS_ERR(jacket_detect))
		return dev_err_probe(dev, PTR_ERR(jacket_detect),
				     "no jacket detect GPIO\n");

	/* Battery low indication GPIOs (not used, we just request them) */
	dummy = devm_gpiod_get(dev, "main battery low", GPIOD_IN);
	if (IS_ERR(dummy))
		return dev_err_probe(dev, PTR_ERR(dummy),
				     "no main battery low GPIO\n");
	dummy = devm_gpiod_get(dev, "jacket battery low", GPIOD_IN);
	if (IS_ERR(dummy))
		return dev_err_probe(dev, PTR_ERR(dummy),
				     "no jacket battery low GPIO\n");

	/* Battery switch GPIO (not used just requested) */
	dummy = devm_gpiod_get(dev, "battery switch", GPIOD_OUT_LOW);
	if (IS_ERR(dummy))
		return dev_err_probe(dev, PTR_ERR(dummy),
				     "no battery switch GPIO\n");

	mutex_init(&tosa_bat_main.work_lock);
	mutex_init(&tosa_bat_jacket.work_lock);

	INIT_WORK(&bat_work, tosa_bat_work);

	main_psy_cfg.drv_data = &tosa_bat_main;
	tosa_bat_main.psy = power_supply_register(dev,
						  &tosa_bat_main_desc,
						  &main_psy_cfg);
	if (IS_ERR(tosa_bat_main.psy)) {
		ret = PTR_ERR(tosa_bat_main.psy);
		goto err_psy_reg_main;
	}

	jacket_psy_cfg.drv_data = &tosa_bat_jacket;
	tosa_bat_jacket.psy = power_supply_register(dev,
						    &tosa_bat_jacket_desc,
						    &jacket_psy_cfg);
	if (IS_ERR(tosa_bat_jacket.psy)) {
		ret = PTR_ERR(tosa_bat_jacket.psy);
		goto err_psy_reg_jacket;
	}

	bu_psy_cfg.drv_data = &tosa_bat_bu;
	tosa_bat_bu.psy = power_supply_register(dev, &tosa_bat_bu_desc,
						&bu_psy_cfg);
	if (IS_ERR(tosa_bat_bu.psy)) {
		ret = PTR_ERR(tosa_bat_bu.psy);
		goto err_psy_reg_bu;
	}

	ret = request_irq(gpiod_to_irq(tosa_bat_main.gpiod_full),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"main full", &tosa_bat_main);
	if (ret)
		goto err_req_main;

	ret = request_irq(gpiod_to_irq(tosa_bat_jacket.gpiod_full),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"jacket full", &tosa_bat_jacket);
	if (ret)
		goto err_req_jacket;

	ret = request_irq(gpiod_to_irq(jacket_detect),
				tosa_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"jacket detect", &tosa_bat_jacket);
	if (!ret) {
		schedule_work(&bat_work);
		return 0;
	}

	free_irq(gpiod_to_irq(tosa_bat_jacket.gpiod_full), &tosa_bat_jacket);
err_req_jacket:
	free_irq(gpiod_to_irq(tosa_bat_main.gpiod_full), &tosa_bat_main);
err_req_main:
	power_supply_unregister(tosa_bat_bu.psy);
err_psy_reg_bu:
	power_supply_unregister(tosa_bat_jacket.psy);
err_psy_reg_jacket:
	power_supply_unregister(tosa_bat_main.psy);
err_psy_reg_main:

	/* see comment in tosa_bat_remove */
	cancel_work_sync(&bat_work);

	return ret;
}

static int tosa_bat_remove(struct platform_device *dev)
{
	free_irq(gpiod_to_irq(jacket_detect), &tosa_bat_jacket);
	free_irq(gpiod_to_irq(tosa_bat_jacket.gpiod_full), &tosa_bat_jacket);
	free_irq(gpiod_to_irq(tosa_bat_main.gpiod_full), &tosa_bat_main);

	power_supply_unregister(tosa_bat_bu.psy);
	power_supply_unregister(tosa_bat_jacket.psy);
	power_supply_unregister(tosa_bat_main.psy);

	/*
	 * Now cancel the bat_work.  We won't get any more schedules,
	 * since all sources (isr and external_power_changed) are
	 * unregistered now.
	 */
	cancel_work_sync(&bat_work);
	return 0;
}

static struct platform_driver tosa_bat_driver = {
	.driver.name	= "wm97xx-battery",
	.driver.owner	= THIS_MODULE,
	.probe		= tosa_bat_probe,
	.remove		= tosa_bat_remove,
	.suspend	= tosa_bat_suspend,
	.resume		= tosa_bat_resume,
};

module_platform_driver(tosa_bat_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Baryshkov");
MODULE_DESCRIPTION("Tosa battery driver");
MODULE_ALIAS("platform:wm97xx-battery");
