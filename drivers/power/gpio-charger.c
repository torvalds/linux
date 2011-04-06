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

#include <linux/power/gpio-charger.h>

struct gpio_charger {
	const struct gpio_charger_platform_data *pdata;
	unsigned int irq;

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
		val->intval = gpio_get_value(pdata->gpio);
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

static int __devinit gpio_charger_probe(struct platform_device *pdev)
{
	const struct gpio_charger_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_charger *gpio_charger;
	struct power_supply *charger;
	int ret;
	int irq;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->gpio)) {
		dev_err(&pdev->dev, "Invalid gpio pin\n");
		return -EINVAL;
	}

	gpio_charger = kzalloc(sizeof(*gpio_charger), GFP_KERNEL);
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
		if (ret)
			dev_warn(&pdev->dev, "Failed to request irq: %d\n", ret);
		else
			gpio_charger->irq = irq;
	}

	platform_set_drvdata(pdev, gpio_charger);

	return 0;

err_gpio_free:
	gpio_free(pdata->gpio);
err_free:
	kfree(gpio_charger);
	return ret;
}

static int __devexit gpio_charger_remove(struct platform_device *pdev)
{
	struct gpio_charger *gpio_charger = platform_get_drvdata(pdev);

	if (gpio_charger->irq)
		free_irq(gpio_charger->irq, &gpio_charger->charger);

	power_supply_unregister(&gpio_charger->charger);

	gpio_free(gpio_charger->pdata->gpio);

	platform_set_drvdata(pdev, NULL);
	kfree(gpio_charger);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_charger_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_charger *gpio_charger = platform_get_drvdata(pdev);

	power_supply_changed(&gpio_charger->charger);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(gpio_charger_pm_ops, NULL, gpio_charger_resume);

static struct platform_driver gpio_charger_driver = {
	.probe = gpio_charger_probe,
	.remove = __devexit_p(gpio_charger_remove),
	.driver = {
		.name = "gpio-charger",
		.owner = THIS_MODULE,
		.pm = &gpio_charger_pm_ops,
	},
};

static int __init gpio_charger_init(void)
{
	return platform_driver_register(&gpio_charger_driver);
}
module_init(gpio_charger_init);

static void __exit gpio_charger_exit(void)
{
	platform_driver_unregister(&gpio_charger_driver);
}
module_exit(gpio_charger_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Driver for chargers which report their online status through a GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-charger");
