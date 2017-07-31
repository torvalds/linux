/*
 * max8903_charger.c - Maxim 8903 USB/Adapter Charger Driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/power/max8903_charger.h>

struct max8903_data {
	struct max8903_pdata *pdata;
	struct device *dev;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	bool fault;
	bool usb_in;
	bool ta_in;
};

static enum power_supply_property max8903_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* Charger status output */
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
	POWER_SUPPLY_PROP_HEALTH, /* Fault or OK */
};

static int max8903_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (gpio_is_valid(data->pdata->chg)) {
			if (gpio_get_value(data->pdata->chg) == 0)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (data->usb_in || data->ta_in)
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (data->usb_in || data->ta_in)
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (data->fault)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t max8903_dcin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool ta_in;
	enum power_supply_type old_type;

	ta_in = gpio_get_value(pdata->dok) ? false : true;

	if (ta_in == data->ta_in)
		return IRQ_HANDLED;

	data->ta_in = ta_in;

	/* Set Current-Limit-Mode 1:DC 0:USB */
	if (gpio_is_valid(pdata->dcm))
		gpio_set_value(pdata->dcm, ta_in ? 1 : 0);

	/* Charger Enable / Disable (cen is negated) */
	if (gpio_is_valid(pdata->cen))
		gpio_set_value(pdata->cen, ta_in ? 0 :
				(data->usb_in ? 0 : 1));

	dev_dbg(data->dev, "TA(DC-IN) Charger %s.\n", ta_in ?
			"Connected" : "Disconnected");

	old_type = data->psy_desc.type;

	if (data->ta_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	else if (data->usb_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	else
		data->psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;

	if (old_type != data->psy_desc.type)
		power_supply_changed(data->psy);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_usbin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool usb_in;
	enum power_supply_type old_type;

	usb_in = gpio_get_value(pdata->uok) ? false : true;

	if (usb_in == data->usb_in)
		return IRQ_HANDLED;

	data->usb_in = usb_in;

	/* Do not touch Current-Limit-Mode */

	/* Charger Enable / Disable (cen is negated) */
	if (gpio_is_valid(pdata->cen))
		gpio_set_value(pdata->cen, usb_in ? 0 :
				(data->ta_in ? 0 : 1));

	dev_dbg(data->dev, "USB Charger %s.\n", usb_in ?
			"Connected" : "Disconnected");

	old_type = data->psy_desc.type;

	if (data->ta_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	else if (data->usb_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	else
		data->psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;

	if (old_type != data->psy_desc.type)
		power_supply_changed(data->psy);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_fault(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool fault;

	fault = gpio_get_value(pdata->flt) ? false : true;

	if (fault == data->fault)
		return IRQ_HANDLED;

	data->fault = fault;

	if (fault)
		dev_err(data->dev, "Charger suffers a fault and stops.\n");
	else
		dev_err(data->dev, "Charger recovered from a fault.\n");

	return IRQ_HANDLED;
}

static struct max8903_pdata *max8903_parse_dt_data(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct max8903_pdata *pdata = NULL;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->dc_valid = false;
	pdata->usb_valid = false;

	pdata->cen = of_get_named_gpio(np, "cen-gpios", 0);
	if (!gpio_is_valid(pdata->cen))
		pdata->cen = -EINVAL;

	pdata->chg = of_get_named_gpio(np, "chg-gpios", 0);
	if (!gpio_is_valid(pdata->chg))
		pdata->chg = -EINVAL;

	pdata->flt = of_get_named_gpio(np, "flt-gpios", 0);
	if (!gpio_is_valid(pdata->flt))
		pdata->flt = -EINVAL;

	pdata->usus = of_get_named_gpio(np, "usus-gpios", 0);
	if (!gpio_is_valid(pdata->usus))
		pdata->usus = -EINVAL;

	pdata->dcm = of_get_named_gpio(np, "dcm-gpios", 0);
	if (!gpio_is_valid(pdata->dcm))
		pdata->dcm = -EINVAL;

	pdata->dok = of_get_named_gpio(np, "dok-gpios", 0);
	if (!gpio_is_valid(pdata->dok))
		pdata->dok = -EINVAL;
	else
		pdata->dc_valid = true;

	pdata->uok = of_get_named_gpio(np, "uok-gpios", 0);
	if (!gpio_is_valid(pdata->uok))
		pdata->uok = -EINVAL;
	else
		pdata->usb_valid = true;

	return pdata;
}

static int max8903_setup_gpios(struct platform_device *pdev)
{
	struct max8903_data *data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct max8903_pdata *pdata = pdev->dev.platform_data;
	int ret = 0;
	int gpio;
	int ta_in = 0;
	int usb_in = 0;

	if (pdata->dc_valid) {
		if (gpio_is_valid(pdata->dok)) {
			ret = devm_gpio_request(dev, pdata->dok,
						data->psy_desc.name);
			if (ret) {
				dev_err(dev,
					"Failed GPIO request for dok: %d err %d\n",
					pdata->dok, ret);
				return ret;
			}

			gpio = pdata->dok; /* PULL_UPed Interrupt */
			ta_in = gpio_get_value(gpio) ? 0 : 1;
		} else {
			dev_err(dev, "When DC is wired, DOK should be wired as well.\n");
			return -EINVAL;
		}
	}

	if (gpio_is_valid(pdata->dcm)) {
		ret = devm_gpio_request(dev, pdata->dcm, data->psy_desc.name);
		if (ret) {
			dev_err(dev,
				"Failed GPIO request for dcm: %d err %d\n",
				pdata->dcm, ret);
			return ret;
		}

		gpio = pdata->dcm; /* Output */
		gpio_set_value(gpio, ta_in);
	}

	if (pdata->usb_valid) {
		if (gpio_is_valid(pdata->uok)) {
			ret = devm_gpio_request(dev, pdata->uok,
						data->psy_desc.name);
			if (ret) {
				dev_err(dev,
					"Failed GPIO request for uok: %d err %d\n",
					pdata->uok, ret);
				return ret;
			}

			gpio = pdata->uok;
			usb_in = gpio_get_value(gpio) ? 0 : 1;
		} else {
			dev_err(dev, "When USB is wired, UOK should be wired."
					"as well.\n");
			return -EINVAL;
		}
	}

	if (gpio_is_valid(pdata->cen)) {
		ret = devm_gpio_request(dev, pdata->cen, data->psy_desc.name);
		if (ret) {
			dev_err(dev,
				"Failed GPIO request for cen: %d err %d\n",
				pdata->cen, ret);
			return ret;
		}

		gpio_set_value(pdata->cen, (ta_in || usb_in) ? 0 : 1);
	}

	if (gpio_is_valid(pdata->chg)) {
		ret = devm_gpio_request(dev, pdata->chg, data->psy_desc.name);
		if (ret) {
			dev_err(dev,
				"Failed GPIO request for chg: %d err %d\n",
				pdata->chg, ret);
			return ret;
		}
	}

	if (gpio_is_valid(pdata->flt)) {
		ret = devm_gpio_request(dev, pdata->flt, data->psy_desc.name);
		if (ret) {
			dev_err(dev,
				"Failed GPIO request for flt: %d err %d\n",
				pdata->flt, ret);
			return ret;
		}
	}

	if (gpio_is_valid(pdata->usus)) {
		ret = devm_gpio_request(dev, pdata->usus, data->psy_desc.name);
		if (ret) {
			dev_err(dev,
				"Failed GPIO request for usus: %d err %d\n",
				pdata->usus, ret);
			return ret;
		}
	}

	data->fault = false;
	data->ta_in = ta_in;
	data->usb_in = usb_in;

	return 0;
}

static int max8903_probe(struct platform_device *pdev)
{
	struct max8903_data *data;
	struct device *dev = &pdev->dev;
	struct max8903_pdata *pdata = pdev->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(struct max8903_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF) && !pdata && dev->of_node)
		pdata = max8903_parse_dt_data(dev);

	if (!pdata) {
		dev_err(dev, "No platform data.\n");
		return -EINVAL;
	}

	pdev->dev.platform_data = pdata;
	data->pdata = pdata;
	data->dev = dev;
	platform_set_drvdata(pdev, data);

	if (pdata->dc_valid == false && pdata->usb_valid == false) {
		dev_err(dev, "No valid power sources.\n");
		return -EINVAL;
	}

	ret = max8903_setup_gpios(pdev);
	if (ret)
		return ret;

	data->psy_desc.name = "max8903_charger";
	data->psy_desc.type = (data->ta_in) ? POWER_SUPPLY_TYPE_MAINS :
			((data->usb_in) ? POWER_SUPPLY_TYPE_USB :
			 POWER_SUPPLY_TYPE_BATTERY);
	data->psy_desc.get_property = max8903_get_property;
	data->psy_desc.properties = max8903_charger_props;
	data->psy_desc.num_properties = ARRAY_SIZE(max8903_charger_props);

	psy_cfg.of_node = dev->of_node;
	psy_cfg.drv_data = data;

	data->psy = devm_power_supply_register(dev, &data->psy_desc, &psy_cfg);
	if (IS_ERR(data->psy)) {
		dev_err(dev, "failed: power supply register.\n");
		return PTR_ERR(data->psy);
	}

	if (pdata->dc_valid) {
		ret = devm_request_threaded_irq(dev, gpio_to_irq(pdata->dok),
					NULL, max8903_dcin,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 DC IN", data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for DC (%d)\n",
					gpio_to_irq(pdata->dok), ret);
			return ret;
		}
	}

	if (pdata->usb_valid) {
		ret = devm_request_threaded_irq(dev, gpio_to_irq(pdata->uok),
					NULL, max8903_usbin,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 USB IN", data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for USB (%d)\n",
					gpio_to_irq(pdata->uok), ret);
			return ret;
		}
	}

	if (gpio_is_valid(pdata->flt)) {
		ret = devm_request_threaded_irq(dev, gpio_to_irq(pdata->flt),
					NULL, max8903_fault,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 Fault", data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for Fault (%d)\n",
					gpio_to_irq(pdata->flt), ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id max8903_match_ids[] = {
	{ .compatible = "maxim,max8903", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max8903_match_ids);

static struct platform_driver max8903_driver = {
	.probe	= max8903_probe,
	.driver = {
		.name	= "max8903-charger",
		.of_match_table = max8903_match_ids
	},
};

module_platform_driver(max8903_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX8903 Charger Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_ALIAS("platform:max8903-charger");
