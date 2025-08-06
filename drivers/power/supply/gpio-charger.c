// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Driver for chargers which report their online status through a GPIO pin
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>

#include <linux/power/gpio-charger.h>

struct gpio_mapping {
	u32 limit_ua;
	u32 gpiodata;
} __packed;

struct gpio_charger {
	struct device *dev;
	unsigned int irq;
	unsigned int charge_status_irq;
	bool wakeup_enabled;

	struct power_supply *charger;
	struct power_supply_desc charger_desc;
	struct gpio_desc *gpiod;
	struct gpio_desc *charge_status;

	struct gpio_descs *current_limit_gpios;
	struct gpio_mapping *current_limit_map;
	u32 current_limit_map_size;
	u32 charge_current_limit;
};

static irqreturn_t gpio_charger_irq(int irq, void *devid)
{
	struct power_supply *charger = devid;

	power_supply_changed(charger);

	return IRQ_HANDLED;
}

static inline struct gpio_charger *psy_to_gpio_charger(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static int set_charge_current_limit(struct gpio_charger *gpio_charger, int val)
{
	struct gpio_mapping mapping;
	int ndescs = gpio_charger->current_limit_gpios->ndescs;
	struct gpio_desc **gpios = gpio_charger->current_limit_gpios->desc;
	int i;

	if (!gpio_charger->current_limit_map_size)
		return -EINVAL;

	for (i = 0; i < gpio_charger->current_limit_map_size; i++) {
		if (gpio_charger->current_limit_map[i].limit_ua <= val)
			break;
	}

	/*
	 * If a valid charge current limit isn't found, default to smallest
	 * current limitation for safety reasons.
	 */
	if (i >= gpio_charger->current_limit_map_size)
		i = gpio_charger->current_limit_map_size - 1;

	mapping = gpio_charger->current_limit_map[i];

	for (i = 0; i < ndescs; i++) {
		bool val = (mapping.gpiodata >> i) & 1;
		gpiod_set_value_cansleep(gpios[ndescs-i-1], val);
	}

	gpio_charger->charge_current_limit = mapping.limit_ua;

	dev_dbg(gpio_charger->dev, "set charge current limit to %d (requested: %d)\n",
		gpio_charger->charge_current_limit, val);

	return 0;
}

static int gpio_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct gpio_charger *gpio_charger = psy_to_gpio_charger(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = gpiod_get_value_cansleep(gpio_charger->gpiod);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (gpiod_get_value_cansleep(gpio_charger->charge_status))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = gpio_charger->charge_current_limit;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int gpio_charger_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct gpio_charger *gpio_charger = psy_to_gpio_charger(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return set_charge_current_limit(gpio_charger, val->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int gpio_charger_property_is_writeable(struct power_supply *psy,
					      enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_type gpio_charger_get_type(struct device *dev)
{
	const char *chargetype;

	if (!device_property_read_string(dev, "charger-type", &chargetype)) {
		if (!strcmp("unknown", chargetype))
			return POWER_SUPPLY_TYPE_UNKNOWN;
		if (!strcmp("battery", chargetype))
			return POWER_SUPPLY_TYPE_BATTERY;
		if (!strcmp("ups", chargetype))
			return POWER_SUPPLY_TYPE_UPS;
		if (!strcmp("mains", chargetype))
			return POWER_SUPPLY_TYPE_MAINS;
		if (!strcmp("usb-sdp", chargetype))
			return POWER_SUPPLY_TYPE_USB;
		if (!strcmp("usb-dcp", chargetype))
			return POWER_SUPPLY_TYPE_USB;
		if (!strcmp("usb-cdp", chargetype))
			return POWER_SUPPLY_TYPE_USB;
		if (!strcmp("usb-aca", chargetype))
			return POWER_SUPPLY_TYPE_USB;
	}
	dev_warn(dev, "unknown charger type %s\n", chargetype);

	return POWER_SUPPLY_TYPE_UNKNOWN;
}

static int gpio_charger_get_irq(struct device *dev, void *dev_id,
				struct gpio_desc *gpio)
{
	int ret, irq = gpiod_to_irq(gpio);

	if (irq > 0) {
		ret = devm_request_any_context_irq(dev, irq, gpio_charger_irq,
						   IRQF_TRIGGER_RISING |
						   IRQF_TRIGGER_FALLING,
						   dev_name(dev),
						   dev_id);
		if (ret < 0) {
			dev_warn(dev, "Failed to request irq: %d\n", ret);
			irq = 0;
		}
	}

	return irq;
}

static int init_charge_current_limit(struct device *dev,
				    struct gpio_charger *gpio_charger)
{
	int i, len;
	u32 cur_limit = U32_MAX;
	bool set_def_limit;
	u32 def_limit;

	gpio_charger->current_limit_gpios = devm_gpiod_get_array_optional(dev,
		"charge-current-limit", GPIOD_OUT_LOW);
	if (IS_ERR(gpio_charger->current_limit_gpios)) {
		dev_err(dev, "error getting current-limit GPIOs\n");
		return PTR_ERR(gpio_charger->current_limit_gpios);
	}

	if (!gpio_charger->current_limit_gpios)
		return 0;

	len = device_property_read_u32_array(dev, "charge-current-limit-mapping",
		NULL, 0);
	if (len < 0)
		return len;

	if (len == 0 || len % 2) {
		dev_err(dev, "invalid charge-current-limit-mapping length\n");
		return -EINVAL;
	}

	gpio_charger->current_limit_map = devm_kmalloc_array(dev,
		len / 2, sizeof(*gpio_charger->current_limit_map), GFP_KERNEL);
	if (!gpio_charger->current_limit_map)
		return -ENOMEM;

	gpio_charger->current_limit_map_size = len / 2;

	len = device_property_read_u32_array(dev, "charge-current-limit-mapping",
		(u32*) gpio_charger->current_limit_map, len);
	if (len < 0)
		return len;

	set_def_limit = !device_property_read_u32(dev,
						  "charge-current-limit-default-microamp",
						  &def_limit);
	for (i=0; i < gpio_charger->current_limit_map_size; i++) {
		if (gpio_charger->current_limit_map[i].limit_ua > cur_limit) {
			dev_err(dev, "charge-current-limit-mapping not sorted by current in descending order\n");
			return -EINVAL;
		}

		cur_limit = gpio_charger->current_limit_map[i].limit_ua;
		if (set_def_limit && def_limit == cur_limit) {
			set_charge_current_limit(gpio_charger, cur_limit);
			return 0;
		}
	}

	if (set_def_limit)
		dev_warn(dev, "charge-current-limit-default-microamp %u not listed in charge-current-limit-mapping\n",
			 def_limit);

	/* default to smallest current limitation for safety reasons */
	len = gpio_charger->current_limit_map_size - 1;
	set_charge_current_limit(gpio_charger,
		gpio_charger->current_limit_map[len].limit_ua);

	return 0;
}

/*
 * The entries will be overwritten by driver's probe routine depending
 * on the available features. This list ensures, that the array is big
 * enough for all optional features.
 */
static enum power_supply_property gpio_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
};

static int gpio_charger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct gpio_charger_platform_data *pdata = dev->platform_data;
	struct power_supply_config psy_cfg = {};
	struct gpio_charger *gpio_charger;
	struct power_supply_desc *charger_desc;
	struct gpio_desc *charge_status;
	int charge_status_irq;
	int ret;
	int num_props = 0;

	if (!pdata && !dev->of_node) {
		dev_err(dev, "No platform data\n");
		return -ENOENT;
	}

	gpio_charger = devm_kzalloc(dev, sizeof(*gpio_charger), GFP_KERNEL);
	if (!gpio_charger)
		return -ENOMEM;
	gpio_charger->dev = dev;

	/*
	 * This will fetch a GPIO descriptor from device tree, ACPI or
	 * boardfile descriptor tables. It's good to try this first.
	 */
	gpio_charger->gpiod = devm_gpiod_get_optional(dev, NULL, GPIOD_IN);
	if (IS_ERR(gpio_charger->gpiod)) {
		/* Just try again if this happens */
		return dev_err_probe(dev, PTR_ERR(gpio_charger->gpiod),
				     "error getting GPIO descriptor\n");
	}

	if (gpio_charger->gpiod) {
		gpio_charger_properties[num_props] = POWER_SUPPLY_PROP_ONLINE;
		num_props++;
	}

	charge_status = devm_gpiod_get_optional(dev, "charge-status", GPIOD_IN);
	if (IS_ERR(charge_status))
		return PTR_ERR(charge_status);
	if (charge_status) {
		gpio_charger->charge_status = charge_status;
		gpio_charger_properties[num_props] = POWER_SUPPLY_PROP_STATUS;
		num_props++;
	}

	ret = init_charge_current_limit(dev, gpio_charger);
	if (ret < 0)
		return ret;
	if (gpio_charger->current_limit_map) {
		gpio_charger_properties[num_props] =
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX;
		num_props++;
	}

	charger_desc = &gpio_charger->charger_desc;
	charger_desc->properties = gpio_charger_properties;
	charger_desc->num_properties = num_props;
	charger_desc->get_property = gpio_charger_get_property;
	charger_desc->set_property = gpio_charger_set_property;
	charger_desc->property_is_writeable =
					gpio_charger_property_is_writeable;

	psy_cfg.fwnode = dev_fwnode(dev);
	psy_cfg.drv_data = gpio_charger;

	if (pdata) {
		charger_desc->name = pdata->name;
		charger_desc->type = pdata->type;
		psy_cfg.supplied_to = pdata->supplied_to;
		psy_cfg.num_supplicants = pdata->num_supplicants;
	} else {
		charger_desc->name = dev->of_node->name;
		charger_desc->type = gpio_charger_get_type(dev);
	}

	if (!charger_desc->name)
		charger_desc->name = pdev->name;

	gpio_charger->charger = devm_power_supply_register(dev, charger_desc,
							   &psy_cfg);
	if (IS_ERR(gpio_charger->charger)) {
		ret = PTR_ERR(gpio_charger->charger);
		dev_err(dev, "Failed to register power supply: %d\n", ret);
		return ret;
	}

	gpio_charger->irq = gpio_charger_get_irq(dev, gpio_charger->charger,
						 gpio_charger->gpiod);

	charge_status_irq = gpio_charger_get_irq(dev, gpio_charger->charger,
						 gpio_charger->charge_status);
	gpio_charger->charge_status_irq = charge_status_irq;

	platform_set_drvdata(pdev, gpio_charger);

	ret = devm_device_init_wakeup(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init wakeup\n");

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
	struct gpio_charger *gpio_charger = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && gpio_charger->wakeup_enabled)
		disable_irq_wake(gpio_charger->irq);
	power_supply_changed(gpio_charger->charger);

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
	.driver = {
		.name = "gpio-charger",
		.pm = &gpio_charger_pm_ops,
		.of_match_table = gpio_charger_match,
	},
};

module_platform_driver(gpio_charger_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Driver for chargers only communicating via GPIO(s)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-charger");
