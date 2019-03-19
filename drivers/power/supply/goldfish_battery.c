// SPDX-License-Identifier: GPL
/*
 * Power supply driver for the goldfish emulator
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 * Copyright (C) 2013 Intel, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/acpi.h>

struct goldfish_battery_data {
	void __iomem *reg_base;
	int irq;
	spinlock_t lock;

	struct power_supply *battery;
	struct power_supply *ac;
};

#define GOLDFISH_BATTERY_READ(data, addr) \
	(readl(data->reg_base + addr))
#define GOLDFISH_BATTERY_WRITE(data, addr, x) \
	(writel(x, data->reg_base + addr))

enum {
	/* status register */
	BATTERY_INT_STATUS	= 0x00,
	/* set this to enable IRQ */
	BATTERY_INT_ENABLE	= 0x04,

	BATTERY_AC_ONLINE	= 0x08,
	BATTERY_STATUS		= 0x0C,
	BATTERY_HEALTH		= 0x10,
	BATTERY_PRESENT		= 0x14,
	BATTERY_CAPACITY	= 0x18,
	BATTERY_VOLTAGE		= 0x1C,
	BATTERY_TEMP		= 0x20,
	BATTERY_CHARGE_COUNTER	= 0x24,
	BATTERY_VOLTAGE_MAX	= 0x28,
	BATTERY_CURRENT_MAX	= 0x2C,
	BATTERY_CURRENT_NOW	= 0x30,
	BATTERY_CURRENT_AVG	= 0x34,
	BATTERY_CHARGE_FULL_UAH	= 0x38,
	BATTERY_CYCLE_COUNT	= 0x40,

	BATTERY_STATUS_CHANGED	= 1U << 0,
	AC_STATUS_CHANGED	= 1U << 1,
	BATTERY_INT_MASK	= BATTERY_STATUS_CHANGED | AC_STATUS_CHANGED,
};


static int goldfish_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct goldfish_battery_data *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_AC_ONLINE);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_VOLTAGE_MAX);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_CURRENT_MAX);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int goldfish_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct goldfish_battery_data *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_STATUS);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_HEALTH);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_PRESENT);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_CAPACITY);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_VOLTAGE);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_TEMP);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = GOLDFISH_BATTERY_READ(data,
						    BATTERY_CHARGE_COUNTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_CURRENT_NOW);
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_CURRENT_AVG);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = GOLDFISH_BATTERY_READ(data,
						    BATTERY_CHARGE_FULL_UAH);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = GOLDFISH_BATTERY_READ(data, BATTERY_CYCLE_COUNT);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property goldfish_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
};

static enum power_supply_property goldfish_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static irqreturn_t goldfish_battery_interrupt(int irq, void *dev_id)
{
	unsigned long irq_flags;
	struct goldfish_battery_data *data = dev_id;
	uint32_t status;

	spin_lock_irqsave(&data->lock, irq_flags);

	/* read status flags, which will clear the interrupt */
	status = GOLDFISH_BATTERY_READ(data, BATTERY_INT_STATUS);
	status &= BATTERY_INT_MASK;

	if (status & BATTERY_STATUS_CHANGED)
		power_supply_changed(data->battery);
	if (status & AC_STATUS_CHANGED)
		power_supply_changed(data->ac);

	spin_unlock_irqrestore(&data->lock, irq_flags);
	return status ? IRQ_HANDLED : IRQ_NONE;
}

static const struct power_supply_desc battery_desc = {
	.properties	= goldfish_battery_props,
	.num_properties	= ARRAY_SIZE(goldfish_battery_props),
	.get_property	= goldfish_battery_get_property,
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
};

static const struct power_supply_desc ac_desc = {
	.properties	= goldfish_ac_props,
	.num_properties	= ARRAY_SIZE(goldfish_ac_props),
	.get_property	= goldfish_ac_get_property,
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
};

static int goldfish_battery_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct goldfish_battery_data *data;
	struct power_supply_config psy_cfg = {};

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	spin_lock_init(&data->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "platform_get_resource failed\n");
		return -ENODEV;
	}

	data->reg_base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (data->reg_base == NULL) {
		dev_err(&pdev->dev, "unable to remap MMIO\n");
		return -ENOMEM;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		return -ENODEV;
	}

	ret = devm_request_irq(&pdev->dev, data->irq,
			       goldfish_battery_interrupt,
			       IRQF_SHARED, pdev->name, data);
	if (ret)
		return ret;

	psy_cfg.drv_data = data;

	data->ac = power_supply_register(&pdev->dev, &ac_desc, &psy_cfg);
	if (IS_ERR(data->ac))
		return PTR_ERR(data->ac);

	data->battery = power_supply_register(&pdev->dev, &battery_desc,
						&psy_cfg);
	if (IS_ERR(data->battery)) {
		power_supply_unregister(data->ac);
		return PTR_ERR(data->battery);
	}

	platform_set_drvdata(pdev, data);

	GOLDFISH_BATTERY_WRITE(data, BATTERY_INT_ENABLE, BATTERY_INT_MASK);
	return 0;
}

static int goldfish_battery_remove(struct platform_device *pdev)
{
	struct goldfish_battery_data *data = platform_get_drvdata(pdev);

	power_supply_unregister(data->battery);
	power_supply_unregister(data->ac);
	return 0;
}

static const struct of_device_id goldfish_battery_of_match[] = {
	{ .compatible = "google,goldfish-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, goldfish_battery_of_match);

static const struct acpi_device_id goldfish_battery_acpi_match[] = {
	{ "GFSH0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, goldfish_battery_acpi_match);

static struct platform_driver goldfish_battery_device = {
	.probe		= goldfish_battery_probe,
	.remove		= goldfish_battery_remove,
	.driver = {
		.name = "goldfish-battery",
		.of_match_table = goldfish_battery_of_match,
		.acpi_match_table = ACPI_PTR(goldfish_battery_acpi_match),
	}
};
module_platform_driver(goldfish_battery_device);

MODULE_AUTHOR("Mike Lockwood lockwood@android.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Battery driver for the Goldfish emulator");
