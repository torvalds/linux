/*
 *	iPAQ h1930/h1940/rx1950 battery controller driver
 *	Copyright (c) Vasily Khoruzhick
 *	Based on h1940_battery.c by Arnaud Patard
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/s3c_adc_battery.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include <plat/adc.h>

#define BAT_POLL_INTERVAL		10000 /* ms */
#define JITTER_DELAY			500 /* ms */

struct s3c_adc_bat {
	struct power_supply		psy;
	struct s3c_adc_client		*client;
	struct s3c_adc_bat_pdata	*pdata;
	int				volt_value;
	int				cur_value;
	unsigned int			timestamp;
	int				level;
	int				status;
	int				cable_plugged:1;
};

static struct delayed_work bat_work;

static void s3c_adc_bat_ext_power_changed(struct power_supply *psy)
{
	schedule_delayed_work(&bat_work,
		msecs_to_jiffies(JITTER_DELAY));
}

static int gather_samples(struct s3c_adc_client *client, int num, int channel)
{
	int value, i;

	/* default to 1 if nothing is set */
	if (num < 1)
		num = 1;

	value = 0;
	for (i = 0; i < num; i++)
		value += s3c_adc_read(client, channel);
	value /= num;

	return value;
}

static enum power_supply_property s3c_adc_backup_bat_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static int s3c_adc_backup_bat_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s3c_adc_bat *bat = container_of(psy, struct s3c_adc_bat, psy);

	if (!bat) {
		dev_err(psy->dev, "%s: no battery infos ?!\n", __func__);
		return -EINVAL;
	}

	if (bat->volt_value < 0 ||
		jiffies_to_msecs(jiffies - bat->timestamp) >
			BAT_POLL_INTERVAL) {
		bat->volt_value = gather_samples(bat->client,
			bat->pdata->backup_volt_samples,
			bat->pdata->backup_volt_channel);
		bat->volt_value *= bat->pdata->backup_volt_mult;
		bat->timestamp = jiffies;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bat->volt_value;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = bat->pdata->backup_volt_min;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = bat->pdata->backup_volt_max;
		return 0;
	default:
		return -EINVAL;
	}
}

static struct s3c_adc_bat backup_bat = {
	.psy = {
		.name		= "backup-battery",
		.type		= POWER_SUPPLY_TYPE_BATTERY,
		.properties	= s3c_adc_backup_bat_props,
		.num_properties = ARRAY_SIZE(s3c_adc_backup_bat_props),
		.get_property	= s3c_adc_backup_bat_get_property,
		.use_for_apm	= 1,
	},
};

static enum power_supply_property s3c_adc_main_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int calc_full_volt(int volt_val, int cur_val, int impedance)
{
	return volt_val + cur_val * impedance / 1000;
}

static int charge_finished(struct s3c_adc_bat *bat)
{
	return bat->pdata->gpio_inverted ?
		!gpio_get_value(bat->pdata->gpio_charge_finished) :
		gpio_get_value(bat->pdata->gpio_charge_finished);
}

static int s3c_adc_bat_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct s3c_adc_bat *bat = container_of(psy, struct s3c_adc_bat, psy);

	int new_level;
	int full_volt;
	const struct s3c_adc_bat_thresh *lut;
	unsigned int lut_size;

	if (!bat) {
		dev_err(psy->dev, "no battery infos ?!\n");
		return -EINVAL;
	}

	lut = bat->pdata->lut_noac;
	lut_size = bat->pdata->lut_noac_cnt;

	if (bat->volt_value < 0 || bat->cur_value < 0 ||
		jiffies_to_msecs(jiffies - bat->timestamp) >
			BAT_POLL_INTERVAL) {
		bat->volt_value = gather_samples(bat->client,
			bat->pdata->volt_samples,
			bat->pdata->volt_channel) * bat->pdata->volt_mult;
		bat->cur_value = gather_samples(bat->client,
			bat->pdata->current_samples,
			bat->pdata->current_channel) * bat->pdata->current_mult;
		bat->timestamp = jiffies;
	}

	if (bat->cable_plugged &&
		((bat->pdata->gpio_charge_finished < 0) ||
		!charge_finished(bat))) {
		lut = bat->pdata->lut_acin;
		lut_size = bat->pdata->lut_acin_cnt;
	}

	new_level = 100000;
	full_volt = calc_full_volt((bat->volt_value / 1000),
		(bat->cur_value / 1000), bat->pdata->internal_impedance);

	if (full_volt < calc_full_volt(lut->volt, lut->cur,
		bat->pdata->internal_impedance)) {
		lut_size--;
		while (lut_size--) {
			int lut_volt1;
			int lut_volt2;

			lut_volt1 = calc_full_volt(lut[0].volt, lut[0].cur,
				bat->pdata->internal_impedance);
			lut_volt2 = calc_full_volt(lut[1].volt, lut[1].cur,
				bat->pdata->internal_impedance);
			if (full_volt < lut_volt1 && full_volt >= lut_volt2) {
				new_level = (lut[1].level +
					(lut[0].level - lut[1].level) *
					(full_volt - lut_volt2) /
					(lut_volt1 - lut_volt2)) * 1000;
				break;
			}
			new_level = lut[1].level * 1000;
			lut++;
		}
	}

	bat->level = new_level;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (bat->pdata->gpio_charge_finished < 0)
			val->intval = bat->level == 100000 ?
				POWER_SUPPLY_STATUS_FULL : bat->status;
		else
			val->intval = bat->status;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = 100000;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
		val->intval = 0;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = bat->level;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bat->volt_value;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bat->cur_value;
		return 0;
	default:
		return -EINVAL;
	}
}

static struct s3c_adc_bat main_bat = {
	.psy = {
		.name			= "main-battery",
		.type			= POWER_SUPPLY_TYPE_BATTERY,
		.properties		= s3c_adc_main_bat_props,
		.num_properties		= ARRAY_SIZE(s3c_adc_main_bat_props),
		.get_property		= s3c_adc_bat_get_property,
		.external_power_changed = s3c_adc_bat_ext_power_changed,
		.use_for_apm		= 1,
	},
};

static void s3c_adc_bat_work(struct work_struct *work)
{
	struct s3c_adc_bat *bat = &main_bat;
	int is_charged;
	int is_plugged;
	static int was_plugged;

	is_plugged = power_supply_am_i_supplied(&bat->psy);
	bat->cable_plugged = is_plugged;
	if (is_plugged != was_plugged) {
		was_plugged = is_plugged;
		if (is_plugged) {
			if (bat->pdata->enable_charger)
				bat->pdata->enable_charger();
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
		} else {
			if (bat->pdata->disable_charger)
				bat->pdata->disable_charger();
			bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
		}
	} else {
		if ((bat->pdata->gpio_charge_finished >= 0) && is_plugged) {
			is_charged = charge_finished(&main_bat);
			if (is_charged) {
				if (bat->pdata->disable_charger)
					bat->pdata->disable_charger();
				bat->status = POWER_SUPPLY_STATUS_FULL;
			} else {
				if (bat->pdata->enable_charger)
					bat->pdata->enable_charger();
				bat->status = POWER_SUPPLY_STATUS_CHARGING;
			}
		}
	}

	power_supply_changed(&bat->psy);
}

static irqreturn_t s3c_adc_bat_charged(int irq, void *dev_id)
{
	schedule_delayed_work(&bat_work,
		msecs_to_jiffies(JITTER_DELAY));
	return IRQ_HANDLED;
}

static int s3c_adc_bat_probe(struct platform_device *pdev)
{
	struct s3c_adc_client	*client;
	struct s3c_adc_bat_pdata *pdata = pdev->dev.platform_data;
	int ret;

	client = s3c_adc_register(pdev, NULL, NULL, 0);
	if (IS_ERR(client)) {
		dev_err(&pdev->dev, "cannot register adc\n");
		return PTR_ERR(client);
	}

	platform_set_drvdata(pdev, client);

	main_bat.client = client;
	main_bat.pdata = pdata;
	main_bat.volt_value = -1;
	main_bat.cur_value = -1;
	main_bat.cable_plugged = 0;
	main_bat.status = POWER_SUPPLY_STATUS_DISCHARGING;

	ret = power_supply_register(&pdev->dev, &main_bat.psy);
	if (ret)
		goto err_reg_main;
	if (pdata->backup_volt_mult) {
		backup_bat.client = client;
		backup_bat.pdata = pdev->dev.platform_data;
		backup_bat.volt_value = -1;
		ret = power_supply_register(&pdev->dev, &backup_bat.psy);
		if (ret)
			goto err_reg_backup;
	}

	INIT_DELAYED_WORK(&bat_work, s3c_adc_bat_work);

	if (pdata->gpio_charge_finished >= 0) {
		ret = gpio_request(pdata->gpio_charge_finished, "charged");
		if (ret)
			goto err_gpio;

		ret = request_irq(gpio_to_irq(pdata->gpio_charge_finished),
				s3c_adc_bat_charged,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"battery charged", NULL);
		if (ret)
			goto err_irq;
	}

	if (pdata->init) {
		ret = pdata->init();
		if (ret)
			goto err_platform;
	}

	dev_info(&pdev->dev, "successfully loaded\n");
	device_init_wakeup(&pdev->dev, 1);

	/* Schedule timer to check current status */
	schedule_delayed_work(&bat_work,
		msecs_to_jiffies(JITTER_DELAY));

	return 0;

err_platform:
	if (pdata->gpio_charge_finished >= 0)
		free_irq(gpio_to_irq(pdata->gpio_charge_finished), NULL);
err_irq:
	if (pdata->gpio_charge_finished >= 0)
		gpio_free(pdata->gpio_charge_finished);
err_gpio:
	if (pdata->backup_volt_mult)
		power_supply_unregister(&backup_bat.psy);
err_reg_backup:
	power_supply_unregister(&main_bat.psy);
err_reg_main:
	return ret;
}

static int s3c_adc_bat_remove(struct platform_device *pdev)
{
	struct s3c_adc_client *client = platform_get_drvdata(pdev);
	struct s3c_adc_bat_pdata *pdata = pdev->dev.platform_data;

	power_supply_unregister(&main_bat.psy);
	if (pdata->backup_volt_mult)
		power_supply_unregister(&backup_bat.psy);

	s3c_adc_release(client);

	if (pdata->gpio_charge_finished >= 0) {
		free_irq(gpio_to_irq(pdata->gpio_charge_finished), NULL);
		gpio_free(pdata->gpio_charge_finished);
	}

	cancel_delayed_work(&bat_work);

	if (pdata->exit)
		pdata->exit();

	return 0;
}

#ifdef CONFIG_PM
static int s3c_adc_bat_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct s3c_adc_bat_pdata *pdata = pdev->dev.platform_data;

	if (pdata->gpio_charge_finished >= 0) {
		if (device_may_wakeup(&pdev->dev))
			enable_irq_wake(
				gpio_to_irq(pdata->gpio_charge_finished));
		else {
			disable_irq(gpio_to_irq(pdata->gpio_charge_finished));
			main_bat.pdata->disable_charger();
		}
	}

	return 0;
}

static int s3c_adc_bat_resume(struct platform_device *pdev)
{
	struct s3c_adc_bat_pdata *pdata = pdev->dev.platform_data;

	if (pdata->gpio_charge_finished >= 0) {
		if (device_may_wakeup(&pdev->dev))
			disable_irq_wake(
				gpio_to_irq(pdata->gpio_charge_finished));
		else
			enable_irq(gpio_to_irq(pdata->gpio_charge_finished));
	}

	/* Schedule timer to check current status */
	schedule_delayed_work(&bat_work,
		msecs_to_jiffies(JITTER_DELAY));

	return 0;
}
#else
#define s3c_adc_bat_suspend NULL
#define s3c_adc_bat_resume NULL
#endif

static struct platform_driver s3c_adc_bat_driver = {
	.driver		= {
		.name	= "s3c-adc-battery",
	},
	.probe		= s3c_adc_bat_probe,
	.remove		= s3c_adc_bat_remove,
	.suspend	= s3c_adc_bat_suspend,
	.resume		= s3c_adc_bat_resume,
};

module_platform_driver(s3c_adc_bat_driver);

MODULE_AUTHOR("Vasily Khoruzhick <anarsoul@gmail.com>");
MODULE_DESCRIPTION("iPAQ H1930/H1940/RX1950 battery controller driver");
MODULE_LICENSE("GPL");
