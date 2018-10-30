/*
 * Generic battery driver code using IIO
 * Copyright (C) 2012, Anish Kumar <anish198519851985@gmail.com>
 * based on jz4740-battery.c
 * based on s3c_adc_battery.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/power/generic-adc-battery.h>

#define JITTER_DEFAULT 10 /* hope 10ms is enough */

enum gab_chan_type {
	GAB_VOLTAGE = 0,
	GAB_CURRENT,
	GAB_POWER,
	GAB_MAX_CHAN_TYPE
};

/*
 * gab_chan_name suggests the standard channel names for commonly used
 * channel types.
 */
static const char *const gab_chan_name[] = {
	[GAB_VOLTAGE]	= "voltage",
	[GAB_CURRENT]	= "current",
	[GAB_POWER]		= "power",
};

struct gab {
	struct power_supply		*psy;
	struct power_supply_desc	psy_desc;
	struct iio_channel	*channel[GAB_MAX_CHAN_TYPE];
	struct gab_platform_data	*pdata;
	struct delayed_work bat_work;
	int	level;
	int	status;
	bool cable_plugged;
};

static struct gab *to_generic_bat(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static void gab_ext_power_changed(struct power_supply *psy)
{
	struct gab *adc_bat = to_generic_bat(psy);

	schedule_delayed_work(&adc_bat->bat_work, msecs_to_jiffies(0));
}

static const enum power_supply_property gab_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

/*
 * This properties are set based on the received platform data and this
 * should correspond one-to-one with enum chan_type.
 */
static const enum power_supply_property gab_dyn_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
};

static bool gab_charge_finished(struct gab *adc_bat)
{
	struct gab_platform_data *pdata = adc_bat->pdata;
	bool ret = gpio_get_value(pdata->gpio_charge_finished);
	bool inv = pdata->gpio_inverted;

	if (!gpio_is_valid(pdata->gpio_charge_finished))
		return false;
	return ret ^ inv;
}

static int gab_get_status(struct gab *adc_bat)
{
	struct gab_platform_data *pdata = adc_bat->pdata;
	struct power_supply_info *bat_info;

	bat_info = &pdata->battery_info;
	if (adc_bat->level == bat_info->charge_full_design)
		return POWER_SUPPLY_STATUS_FULL;
	return adc_bat->status;
}

static enum gab_chan_type gab_prop_to_chan(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_POWER_NOW:
		return GAB_POWER;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return GAB_VOLTAGE;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return GAB_CURRENT;
	default:
		WARN_ON(1);
		break;
	}
	return GAB_POWER;
}

static int read_channel(struct gab *adc_bat, enum power_supply_property psp,
		int *result)
{
	int ret;
	int chan_index;

	chan_index = gab_prop_to_chan(psp);
	ret = iio_read_channel_processed(adc_bat->channel[chan_index],
			result);
	if (ret < 0)
		pr_err("read channel error\n");
	return ret;
}

static int gab_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct gab *adc_bat;
	struct gab_platform_data *pdata;
	struct power_supply_info *bat_info;
	int result = 0;
	int ret = 0;

	adc_bat = to_generic_bat(psy);
	if (!adc_bat) {
		dev_err(&psy->dev, "no battery infos ?!\n");
		return -EINVAL;
	}
	pdata = adc_bat->pdata;
	bat_info = &pdata->battery_info;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = gab_get_status(adc_bat);
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = pdata->cal_charge(result);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		ret = read_channel(adc_bat, psp, &result);
		if (ret < 0)
			goto err;
		val->intval = result;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = bat_info->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = bat_info->voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = bat_info->voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = bat_info->charge_full_design;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bat_info->name;
		break;
	default:
		return -EINVAL;
	}
err:
	return ret;
}

static void gab_work(struct work_struct *work)
{
	struct gab *adc_bat;
	struct delayed_work *delayed_work;
	bool is_plugged;
	int status;

	delayed_work = to_delayed_work(work);
	adc_bat = container_of(delayed_work, struct gab, bat_work);
	status = adc_bat->status;

	is_plugged = power_supply_am_i_supplied(adc_bat->psy);
	adc_bat->cable_plugged = is_plugged;

	if (!is_plugged)
		adc_bat->status =  POWER_SUPPLY_STATUS_DISCHARGING;
	else if (gab_charge_finished(adc_bat))
		adc_bat->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	else
		adc_bat->status = POWER_SUPPLY_STATUS_CHARGING;

	if (status != adc_bat->status)
		power_supply_changed(adc_bat->psy);
}

static irqreturn_t gab_charged(int irq, void *dev_id)
{
	struct gab *adc_bat = dev_id;
	struct gab_platform_data *pdata = adc_bat->pdata;
	int delay;

	delay = pdata->jitter_delay ? pdata->jitter_delay : JITTER_DEFAULT;
	schedule_delayed_work(&adc_bat->bat_work,
			msecs_to_jiffies(delay));
	return IRQ_HANDLED;
}

static int gab_probe(struct platform_device *pdev)
{
	struct gab *adc_bat;
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {};
	struct gab_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;
	int chan;
	int index = ARRAY_SIZE(gab_props);
	bool any = false;

	adc_bat = devm_kzalloc(&pdev->dev, sizeof(*adc_bat), GFP_KERNEL);
	if (!adc_bat) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	psy_cfg.drv_data = adc_bat;
	psy_desc = &adc_bat->psy_desc;
	psy_desc->name = pdata->battery_info.name;

	/* bootup default values for the battery */
	adc_bat->cable_plugged = false;
	adc_bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->get_property = gab_get_property;
	psy_desc->external_power_changed = gab_ext_power_changed;
	adc_bat->pdata = pdata;

	/*
	 * copying the static properties and allocating extra memory for holding
	 * the extra configurable properties received from platform data.
	 */
	psy_desc->properties = kcalloc(ARRAY_SIZE(gab_props) +
					ARRAY_SIZE(gab_chan_name),
					sizeof(*psy_desc->properties),
					GFP_KERNEL);
	if (!psy_desc->properties) {
		ret = -ENOMEM;
		goto first_mem_fail;
	}

	memcpy(psy_desc->properties, gab_props, sizeof(gab_props));

	/*
	 * getting channel from iio and copying the battery properties
	 * based on the channel supported by consumer device.
	 */
	for (chan = 0; chan < ARRAY_SIZE(gab_chan_name); chan++) {
		adc_bat->channel[chan] = iio_channel_get(&pdev->dev,
							 gab_chan_name[chan]);
		if (IS_ERR(adc_bat->channel[chan])) {
			ret = PTR_ERR(adc_bat->channel[chan]);
			adc_bat->channel[chan] = NULL;
		} else {
			/* copying properties for supported channels only */
			int index2;

			for (index2 = 0; index2 < index; index2++) {
				if (psy_desc->properties[index2] ==
				    gab_dyn_props[chan])
					break;	/* already known */
			}
			if (index2 == index)	/* really new */
				psy_desc->properties[index++] =
					gab_dyn_props[chan];
			any = true;
		}
	}

	/* none of the channels are supported so let's bail out */
	if (!any) {
		ret = -ENODEV;
		goto second_mem_fail;
	}

	/*
	 * Total number of properties is equal to static properties
	 * plus the dynamic properties.Some properties may not be set
	 * as come channels may be not be supported by the device.So
	 * we need to take care of that.
	 */
	psy_desc->num_properties = index;

	adc_bat->psy = power_supply_register(&pdev->dev, psy_desc, &psy_cfg);
	if (IS_ERR(adc_bat->psy)) {
		ret = PTR_ERR(adc_bat->psy);
		goto err_reg_fail;
	}

	INIT_DELAYED_WORK(&adc_bat->bat_work, gab_work);

	if (gpio_is_valid(pdata->gpio_charge_finished)) {
		int irq;
		ret = gpio_request(pdata->gpio_charge_finished, "charged");
		if (ret)
			goto gpio_req_fail;

		irq = gpio_to_irq(pdata->gpio_charge_finished);
		ret = request_any_context_irq(irq, gab_charged,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"battery charged", adc_bat);
		if (ret < 0)
			goto err_gpio;
	}

	platform_set_drvdata(pdev, adc_bat);

	/* Schedule timer to check current status */
	schedule_delayed_work(&adc_bat->bat_work,
			msecs_to_jiffies(0));
	return 0;

err_gpio:
	gpio_free(pdata->gpio_charge_finished);
gpio_req_fail:
	power_supply_unregister(adc_bat->psy);
err_reg_fail:
	for (chan = 0; chan < ARRAY_SIZE(gab_chan_name); chan++) {
		if (adc_bat->channel[chan])
			iio_channel_release(adc_bat->channel[chan]);
	}
second_mem_fail:
	kfree(psy_desc->properties);
first_mem_fail:
	return ret;
}

static int gab_remove(struct platform_device *pdev)
{
	int chan;
	struct gab *adc_bat = platform_get_drvdata(pdev);
	struct gab_platform_data *pdata = adc_bat->pdata;

	power_supply_unregister(adc_bat->psy);

	if (gpio_is_valid(pdata->gpio_charge_finished)) {
		free_irq(gpio_to_irq(pdata->gpio_charge_finished), adc_bat);
		gpio_free(pdata->gpio_charge_finished);
	}

	for (chan = 0; chan < ARRAY_SIZE(gab_chan_name); chan++) {
		if (adc_bat->channel[chan])
			iio_channel_release(adc_bat->channel[chan]);
	}

	kfree(adc_bat->psy_desc.properties);
	cancel_delayed_work(&adc_bat->bat_work);
	return 0;
}

static int __maybe_unused gab_suspend(struct device *dev)
{
	struct gab *adc_bat = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&adc_bat->bat_work);
	adc_bat->status = POWER_SUPPLY_STATUS_UNKNOWN;
	return 0;
}

static int __maybe_unused gab_resume(struct device *dev)
{
	struct gab *adc_bat = dev_get_drvdata(dev);
	struct gab_platform_data *pdata = adc_bat->pdata;
	int delay;

	delay = pdata->jitter_delay ? pdata->jitter_delay : JITTER_DEFAULT;

	/* Schedule timer to check current status */
	schedule_delayed_work(&adc_bat->bat_work,
			msecs_to_jiffies(delay));
	return 0;
}

static SIMPLE_DEV_PM_OPS(gab_pm_ops, gab_suspend, gab_resume);

static struct platform_driver gab_driver = {
	.driver		= {
		.name	= "generic-adc-battery",
		.pm	= &gab_pm_ops,
	},
	.probe		= gab_probe,
	.remove		= gab_remove,
};
module_platform_driver(gab_driver);

MODULE_AUTHOR("anish kumar <anish198519851985@gmail.com>");
MODULE_DESCRIPTION("generic battery driver using IIO");
MODULE_LICENSE("GPL");
