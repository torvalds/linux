/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Driver for chargers which report their online status through a GPIO pin
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/power/gpio-charger.h>

struct gpio_charger {
	const struct gpio_charger_platform_data *pdata;
	unsigned int irq;
	bool wakeup_enabled;

	struct power_supply charger;
};

static irqreturn_t gpio_charger_irq(int irq, void *devid)
{
	struct power_supply *charger = devid;

	power_supply_changed(charger);

	return IRQ_HANDLED;
}

static inline struct gpio_charger *psy_to_gpio_charger(struct power_supply *psy)
{
	return container_of(psy, struct gpio_charger, charger);
}

static int gpio_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct gpio_charger *gpio_charger = psy_to_gpio_charger(psy);
	const struct gpio_charger_platform_data *pdata = gpio_charger->pdata;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!gpio_get_value_cansleep(pdata->gpio);
		val->intval ^= pdata->gpio_active_low;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property gpio_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static
struct gpio_charger_platform_data *gpio_charger_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct gpio_charger_platform_data *pdata;
	const char *chargetype;
	enum of_gpio_flags flags;
	int ret;

	if (!np)
		return ERR_PTR(-ENOENT);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->name = np->name;

	pdata->gpio = of_get_gpio_flags(np, 0, &flags);
	if (pdata->gpio < 0) {
		if (pdata->gpio != -EPROBE_DEFER)
			dev_err(dev, "could not get charger gpio\n");
		return ERR_PTR(pdata->gpio);
	}

	pdata->gpio_active_low = !!(flags & OF_GPIO_ACTIVE_LOW);

	pdata->type = POWER_SUPPLY_TYPE_UNKNOWN;
	ret = of_property_read_string(np, "charger-type", &chargetype);
	if (ret >= 0) {
		if (!strncmp("unknown", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_UNKNOWN;
		else if (!strncmp("battery", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_BATTERY;
		else if (!strncmp("ups", chargetype, 3))
			pdata->type = POWER_SUPPLY_TYPE_UPS;
		else if (!strncmp("mains", chargetype, 5))
			pdata->type = POWER_SUPPLY_TYPE_MAINS;
		else if (!strncmp("usb-sdp", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_USB;
		else if (!strncmp("usb-dcp", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_USB_DCP;
		else if (!strncmp("usb-cdp", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_USB_CDP;
		else if (!strncmp("usb-aca", chargetype, 7))
			pdata->type = POWER_SUPPLY_TYPE_USB_ACA;
		else
			dev_warn(dev, "unknown charger type %s\n", chargetype);
	}

	return pdata;
}

static int gpio_charger_probe(struct platform_device *pdev)
{
	const struct gpio_charger_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_charger *gpio_charger;
	struct power_supply *charger;
	int ret;
	int irq;

	if (!pdata) {
		pdata = gpio_charger_parse_dt(&pdev->dev);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "No platform data\n");
			return ret;
		}
	}

	if (!gpio_is_valid(pdata->gpio)) {
		dev_err(&pdev->dev, "Invalid gpio pin\n");
		return -EINVAL;
	}

	gpio_charger = devm_kzalloc(&pdev->dev, sizeof(*gpio_charger),
					GFP_KERNEL);
	if (!gpio_charger) {
		dev_err(&pdev->dev, "Failed to alloc driver structure\n");
		return -ENOMEM;
	}

	charger = &gpio_charger->charger;

	charger->name = pdata->name ? pdata->name : "gpio-charger";
	charger->type = pdata->type;
	charger->properties = gpio_charger_properties;
	charger->num_properties = ARRAY_SIZE(gpio_charger_properties);
	charger->get_property = gpio_charger_get_property;
	charger->supplied_to = pdata->supplied_to;
	charger->num_supplicants = pdata->num_supplicants;
	charger->of_node = pdev->dev.of_node;

	ret = gpio_request(pdata->gpio, dev_name(&pdev->dev));
	if (ret) {
		dev_err(&pdev->dev, "Failed to request gpio pin: %d\n", ret);
		goto err_free;
	}
	ret = gpio_direction_input(pdata->gpio);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set gpio to input: %d\n", ret);
		goto err_gpio_free;
	}

	gpio_charger->pdata = pdata;

	ret = power_supply_register(&pdev->dev, charger);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register power supply: %d\n",
			ret);
		goto err_gpio_free;
	}

	irq = gpio_to_irq(pdata->gpio);
	if (irq > 0) {
		ret = request_any_context_irq(irq, gpio_charger_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				dev_name(&pdev->dev), charger);
		if (ret < 0)
			dev_warn(&pdev->dev, "Failed to request irq: %d\n", ret);
		else
			gpio_charger->irq = irq;
	}

	platform_set_drvdata(pdev, gpio_charger);

	device_init_wakeup(&pdev->dev, 1);

	return 0;

err_gpio_free:
	gpio_free(pdata->gpio);
err_free:
	return ret;
}

static int gpio_charger_remove(struct platform_device *pdev)
{
	struct gpio_charger *gpio_charger = platform_get_drvdata(pdev);

	if (gpio_charger->irq)
		free_irq(gpio_charger->irq, &gpio_charger->charger);

	power_supply_unregister(&gpio_charger->charger);

	gpio_free(gpio_charger->pdata->gpio);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_charger_suspend(struct device *dev)
{
	struct gpio_charger *gpio_charger = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		gpio_charger->wakeup_enabled =
			!enable_irq_wake(gpio_charger->irq);

	return 0;
}

static int gpio_charger_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_charger *gpio_charger = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev) && gpio_charger->wakeup_enabled)
		disable_irq_wake(gpio_charger->irq);
	power_supply_changed(&gpio_charger->charger);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(gpio_charger_pm_ops,
		gpio_charger_suspend, gpio_charger_resume);

static const struct of_device_id gpio_charger_match[] = {
	{ .compatible = "gpio-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_charger_match);

static struct platform_driver gpio_charger_driver = {
	.probe = gpio_charger_probe,
	.remove = gpio_charger_remove,
	.driver = {
		.name = "gpio-charger",
		.pm = &gpio_charger_pm_ops,
		.of_match_table = gpio_charger_match,
	},
};

module_platform_driver(gpio_charger_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Driver for chargers which report their online status through a GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-charger");
