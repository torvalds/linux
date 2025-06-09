// SPDX-License-Identifier: GPL-2.0
/*
 * Generic battery driver using IIO
 * Copyright (C) 2012, Anish Kumar <yesanishhere@gmail.com>
 * Copyright (c) 2023, Sebastian Reichel <sre@kernel.org>
 */
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/of.h>
#include <linux/devm-helpers.h>

#define JITTER_DEFAULT 10 /* hope 10ms is enough */

enum gab_chan_type {
	GAB_VOLTAGE = 0,
	GAB_CURRENT,
	GAB_POWER,
	GAB_TEMP,
	GAB_MAX_CHAN_TYPE
};

/*
 * gab_chan_name suggests the standard channel names for commonly used
 * channel types.
 */
static const char *const gab_chan_name[] = {
	[GAB_VOLTAGE]	= "voltage",
	[GAB_CURRENT]	= "current",
	[GAB_POWER]	= "power",
	[GAB_TEMP]	= "temperature",
};

struct gab {
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	struct iio_channel *channel[GAB_MAX_CHAN_TYPE];
	struct delayed_work bat_work;
	int status;
	struct gpio_desc *charge_finished;
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
};

/*
 * This properties are set based on the received platform data and this
 * should correspond one-to-one with enum chan_type.
 */
static const enum power_supply_property gab_dyn_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static bool gab_charge_finished(struct gab *adc_bat)
{
	if (!adc_bat->charge_finished)
		return false;
	return gpiod_get_value(adc_bat->charge_finished);
}

static int gab_read_channel(struct gab *adc_bat, enum gab_chan_type channel,
		int *result)
{
	int ret;

	ret = iio_read_channel_processed(adc_bat->channel[channel], result);
	if (ret < 0)
		dev_err(&adc_bat->psy->dev, "read channel error: %d\n", ret);
	else
		*result *= 1000;

	return ret;
}

static int gab_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct gab *adc_bat = to_generic_bat(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = adc_bat->status;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return gab_read_channel(adc_bat, GAB_VOLTAGE, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return gab_read_channel(adc_bat, GAB_CURRENT, &val->intval);
	case POWER_SUPPLY_PROP_POWER_NOW:
		return gab_read_channel(adc_bat, GAB_POWER, &val->intval);
	case POWER_SUPPLY_PROP_TEMP:
		return gab_read_channel(adc_bat, GAB_TEMP, &val->intval);
	default:
		return -EINVAL;
	}
}

static void gab_work(struct work_struct *work)
{
	struct gab *adc_bat;
	struct delayed_work *delayed_work;
	int status;

	delayed_work = to_delayed_work(work);
	adc_bat = container_of(delayed_work, struct gab, bat_work);
	status = adc_bat->status;

	if (!power_supply_am_i_supplied(adc_bat->psy))
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

	schedule_delayed_work(&adc_bat->bat_work,
			      msecs_to_jiffies(JITTER_DEFAULT));

	return IRQ_HANDLED;
}

static int gab_probe(struct platform_device *pdev)
{
	struct gab *adc_bat;
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {};
	enum power_supply_property *properties;
	int ret = 0;
	int chan;
	int index = ARRAY_SIZE(gab_props);
	bool any = false;

	adc_bat = devm_kzalloc(&pdev->dev, sizeof(*adc_bat), GFP_KERNEL);
	if (!adc_bat)
		return -ENOMEM;

	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	psy_cfg.drv_data = adc_bat;
	psy_desc = &adc_bat->psy_desc;
	psy_desc->name = dev_name(&pdev->dev);

	/* bootup default values for the battery */
	adc_bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->get_property = gab_get_property;
	psy_desc->external_power_changed = gab_ext_power_changed;

	/*
	 * copying the static properties and allocating extra memory for holding
	 * the extra configurable properties received from platform data.
	 */
	properties = devm_kcalloc(&pdev->dev,
				  ARRAY_SIZE(gab_props) +
				  ARRAY_SIZE(gab_chan_name),
				  sizeof(*properties),
				  GFP_KERNEL);
	if (!properties)
		return -ENOMEM;

	memcpy(properties, gab_props, sizeof(gab_props));

	/*
	 * getting channel from iio and copying the battery properties
	 * based on the channel supported by consumer device.
	 */
	for (chan = 0; chan < ARRAY_SIZE(gab_chan_name); chan++) {
		adc_bat->channel[chan] = devm_iio_channel_get(&pdev->dev, gab_chan_name[chan]);
		if (IS_ERR(adc_bat->channel[chan])) {
			ret = PTR_ERR(adc_bat->channel[chan]);
			if (ret != -ENODEV)
				return dev_err_probe(&pdev->dev, ret, "Failed to get ADC channel %s\n", gab_chan_name[chan]);
			adc_bat->channel[chan] = NULL;
		} else if (adc_bat->channel[chan]) {
			/* copying properties for supported channels only */
			int index2;

			for (index2 = 0; index2 < index; index2++) {
				if (properties[index2] == gab_dyn_props[chan])
					break;	/* already known */
			}
			if (index2 == index)	/* really new */
				properties[index++] = gab_dyn_props[chan];
			any = true;
		}
	}

	/* none of the channels are supported so let's bail out */
	if (!any)
		return dev_err_probe(&pdev->dev, -ENODEV, "Failed to get any ADC channel\n");

	/*
	 * Total number of properties is equal to static properties
	 * plus the dynamic properties.Some properties may not be set
	 * as come channels may be not be supported by the device.So
	 * we need to take care of that.
	 */
	psy_desc->properties = properties;
	psy_desc->num_properties = index;

	adc_bat->psy = devm_power_supply_register(&pdev->dev, psy_desc, &psy_cfg);
	if (IS_ERR(adc_bat->psy))
		return dev_err_probe(&pdev->dev, PTR_ERR(adc_bat->psy), "Failed to register power-supply device\n");

	ret = devm_delayed_work_autocancel(&pdev->dev, &adc_bat->bat_work, gab_work);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register delayed work\n");

	adc_bat->charge_finished = devm_gpiod_get_optional(&pdev->dev, "charged", GPIOD_IN);
	if (adc_bat->charge_finished) {
		int irq;

		irq = gpiod_to_irq(adc_bat->charge_finished);
		ret = devm_request_any_context_irq(&pdev->dev, irq, gab_charged,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"battery charged", adc_bat);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret, "Failed to register irq\n");
	}

	platform_set_drvdata(pdev, adc_bat);

	/* Schedule timer to check current status */
	schedule_delayed_work(&adc_bat->bat_work,
			msecs_to_jiffies(0));
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

	/* Schedule timer to check current status */
	schedule_delayed_work(&adc_bat->bat_work,
			      msecs_to_jiffies(JITTER_DEFAULT));

	return 0;
}

static SIMPLE_DEV_PM_OPS(gab_pm_ops, gab_suspend, gab_resume);

static const struct of_device_id gab_match[] = {
	{ .compatible = "adc-battery" },
	{ }
};
MODULE_DEVICE_TABLE(of, gab_match);

static struct platform_driver gab_driver = {
	.driver		= {
		.name	= "generic-adc-battery",
		.pm	= &gab_pm_ops,
		.of_match_table = gab_match,
	},
	.probe		= gab_probe,
};
module_platform_driver(gab_driver);

MODULE_AUTHOR("anish kumar <yesanishhere@gmail.com>");
MODULE_DESCRIPTION("generic battery driver using IIO");
MODULE_LICENSE("GPL");
