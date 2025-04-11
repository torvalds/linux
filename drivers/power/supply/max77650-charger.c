// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// Battery charger driver for MAXIM 77650/77651 charger/power-supply.

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define MAX77650_CHARGER_ENABLED		BIT(0)
#define MAX77650_CHARGER_DISABLED		0x00
#define MAX77650_CHARGER_CHG_EN_MASK		BIT(0)

#define MAX77650_CHG_DETAILS_MASK		GENMASK(7, 4)
#define MAX77650_CHG_DETAILS_BITS(_reg) \
		(((_reg) & MAX77650_CHG_DETAILS_MASK) >> 4)

/* Charger is OFF. */
#define MAX77650_CHG_OFF			0x00
/* Charger is in prequalification mode. */
#define MAX77650_CHG_PREQ			0x01
/* Charger is in fast-charge constant current mode. */
#define MAX77650_CHG_ON_CURR			0x02
/* Charger is in JEITA modified fast-charge constant-current mode. */
#define MAX77650_CHG_ON_CURR_JEITA		0x03
/* Charger is in fast-charge constant-voltage mode. */
#define MAX77650_CHG_ON_VOLT			0x04
/* Charger is in JEITA modified fast-charge constant-voltage mode. */
#define MAX77650_CHG_ON_VOLT_JEITA		0x05
/* Charger is in top-off mode. */
#define MAX77650_CHG_ON_TOPOFF			0x06
/* Charger is in JEITA modified top-off mode. */
#define MAX77650_CHG_ON_TOPOFF_JEITA		0x07
/* Charger is done. */
#define MAX77650_CHG_DONE			0x08
/* Charger is JEITA modified done. */
#define MAX77650_CHG_DONE_JEITA			0x09
/* Charger is suspended due to a prequalification timer fault. */
#define MAX77650_CHG_SUSP_PREQ_TIM_FAULT	0x0a
/* Charger is suspended due to a fast-charge timer fault. */
#define MAX77650_CHG_SUSP_FAST_CHG_TIM_FAULT	0x0b
/* Charger is suspended due to a battery temperature fault. */
#define MAX77650_CHG_SUSP_BATT_TEMP_FAULT	0x0c

#define MAX77650_CHGIN_DETAILS_MASK		GENMASK(3, 2)
#define MAX77650_CHGIN_DETAILS_BITS(_reg) \
		(((_reg) & MAX77650_CHGIN_DETAILS_MASK) >> 2)

#define MAX77650_CHGIN_UNDERVOLTAGE_LOCKOUT	0x00
#define MAX77650_CHGIN_OVERVOLTAGE_LOCKOUT	0x01
#define MAX77650_CHGIN_OKAY			0x11

#define MAX77650_CHARGER_CHG_MASK	BIT(1)
#define MAX77650_CHARGER_CHG_CHARGING(_reg) \
		(((_reg) & MAX77650_CHARGER_CHG_MASK) > 1)

#define MAX77650_CHARGER_VCHGIN_MIN_MASK	0xc0
#define MAX77650_CHARGER_VCHGIN_MIN_SHIFT(_val)	((_val) << 5)

#define MAX77650_CHARGER_ICHGIN_LIM_MASK	0x1c
#define MAX77650_CHARGER_ICHGIN_LIM_SHIFT(_val)	((_val) << 2)

struct max77650_charger_data {
	struct regmap *map;
	struct device *dev;
};

static enum power_supply_property max77650_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_TYPE
};

static const unsigned int max77650_charger_vchgin_min_table[] = {
	4000000, 4100000, 4200000, 4300000, 4400000, 4500000, 4600000, 4700000
};

static const unsigned int max77650_charger_ichgin_lim_table[] = {
	95000, 190000, 285000, 380000, 475000
};

static int max77650_charger_set_vchgin_min(struct max77650_charger_data *chg,
					   unsigned int val)
{
	int i, rv;

	for (i = 0; i < ARRAY_SIZE(max77650_charger_vchgin_min_table); i++) {
		if (val == max77650_charger_vchgin_min_table[i]) {
			rv = regmap_update_bits(chg->map,
					MAX77650_REG_CNFG_CHG_B,
					MAX77650_CHARGER_VCHGIN_MIN_MASK,
					MAX77650_CHARGER_VCHGIN_MIN_SHIFT(i));
			if (rv)
				return rv;

			return 0;
		}
	}

	return -EINVAL;
}

static int max77650_charger_set_ichgin_lim(struct max77650_charger_data *chg,
					   unsigned int val)
{
	int i, rv;

	for (i = 0; i < ARRAY_SIZE(max77650_charger_ichgin_lim_table); i++) {
		if (val == max77650_charger_ichgin_lim_table[i]) {
			rv = regmap_update_bits(chg->map,
					MAX77650_REG_CNFG_CHG_B,
					MAX77650_CHARGER_ICHGIN_LIM_MASK,
					MAX77650_CHARGER_ICHGIN_LIM_SHIFT(i));
			if (rv)
				return rv;

			return 0;
		}
	}

	return -EINVAL;
}

static int max77650_charger_enable(struct max77650_charger_data *chg)
{
	int rv;

	rv = regmap_update_bits(chg->map,
				MAX77650_REG_CNFG_CHG_B,
				MAX77650_CHARGER_CHG_EN_MASK,
				MAX77650_CHARGER_ENABLED);
	if (rv)
		dev_err(chg->dev, "unable to enable the charger: %d\n", rv);

	return rv;
}

static void max77650_charger_disable(struct max77650_charger_data *chg)
{
	int rv;

	rv = regmap_update_bits(chg->map,
				MAX77650_REG_CNFG_CHG_B,
				MAX77650_CHARGER_CHG_EN_MASK,
				MAX77650_CHARGER_DISABLED);
	if (rv)
		dev_err(chg->dev, "unable to disable the charger: %d\n", rv);
}

static irqreturn_t max77650_charger_check_status(int irq, void *data)
{
	struct max77650_charger_data *chg = data;
	int rv, reg;

	rv = regmap_read(chg->map, MAX77650_REG_STAT_CHG_B, &reg);
	if (rv) {
		dev_err(chg->dev,
			"unable to read the charger status: %d\n", rv);
		return IRQ_HANDLED;
	}

	switch (MAX77650_CHGIN_DETAILS_BITS(reg)) {
	case MAX77650_CHGIN_UNDERVOLTAGE_LOCKOUT:
		dev_err(chg->dev, "undervoltage lockout detected, disabling charger\n");
		max77650_charger_disable(chg);
		break;
	case MAX77650_CHGIN_OVERVOLTAGE_LOCKOUT:
		dev_err(chg->dev, "overvoltage lockout detected, disabling charger\n");
		max77650_charger_disable(chg);
		break;
	case MAX77650_CHGIN_OKAY:
		max77650_charger_enable(chg);
		break;
	default:
		/* May be 0x10 - debouncing */
		break;
	}

	return IRQ_HANDLED;
}

static int max77650_charger_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct max77650_charger_data *chg = power_supply_get_drvdata(psy);
	int rv, reg;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		rv = regmap_read(chg->map, MAX77650_REG_STAT_CHG_B, &reg);
		if (rv)
			return rv;

		if (MAX77650_CHARGER_CHG_CHARGING(reg)) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		}

		switch (MAX77650_CHG_DETAILS_BITS(reg)) {
		case MAX77650_CHG_OFF:
		case MAX77650_CHG_SUSP_PREQ_TIM_FAULT:
		case MAX77650_CHG_SUSP_FAST_CHG_TIM_FAULT:
		case MAX77650_CHG_SUSP_BATT_TEMP_FAULT:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case MAX77650_CHG_PREQ:
		case MAX77650_CHG_ON_CURR:
		case MAX77650_CHG_ON_CURR_JEITA:
		case MAX77650_CHG_ON_VOLT:
		case MAX77650_CHG_ON_VOLT_JEITA:
		case MAX77650_CHG_ON_TOPOFF:
		case MAX77650_CHG_ON_TOPOFF_JEITA:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case MAX77650_CHG_DONE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rv = regmap_read(chg->map, MAX77650_REG_STAT_CHG_B, &reg);
		if (rv)
			return rv;

		val->intval = MAX77650_CHARGER_CHG_CHARGING(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rv = regmap_read(chg->map, MAX77650_REG_STAT_CHG_B, &reg);
		if (rv)
			return rv;

		if (!MAX77650_CHARGER_CHG_CHARGING(reg)) {
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		}

		switch (MAX77650_CHG_DETAILS_BITS(reg)) {
		case MAX77650_CHG_PREQ:
		case MAX77650_CHG_ON_CURR:
		case MAX77650_CHG_ON_CURR_JEITA:
		case MAX77650_CHG_ON_VOLT:
		case MAX77650_CHG_ON_VOLT_JEITA:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case MAX77650_CHG_ON_TOPOFF:
		case MAX77650_CHG_ON_TOPOFF_JEITA:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc max77650_battery_desc = {
	.name		= "max77650",
	.type		= POWER_SUPPLY_TYPE_USB,
	.get_property	= max77650_charger_get_property,
	.properties	= max77650_charger_properties,
	.num_properties	= ARRAY_SIZE(max77650_charger_properties),
};

static int max77650_charger_probe(struct platform_device *pdev)
{
	struct power_supply_config pscfg = {};
	struct max77650_charger_data *chg;
	struct power_supply *battery;
	struct device *dev, *parent;
	int rv, chg_irq, chgin_irq;
	unsigned int prop;

	dev = &pdev->dev;
	parent = dev->parent;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	platform_set_drvdata(pdev, chg);

	chg->map = dev_get_regmap(parent, NULL);
	if (!chg->map)
		return -ENODEV;

	chg->dev = dev;

	pscfg.fwnode = dev_fwnode(dev);
	pscfg.drv_data = chg;

	chg_irq = platform_get_irq_byname(pdev, "CHG");
	if (chg_irq < 0)
		return chg_irq;

	chgin_irq = platform_get_irq_byname(pdev, "CHGIN");
	if (chgin_irq < 0)
		return chgin_irq;

	rv = devm_request_any_context_irq(dev, chg_irq,
					  max77650_charger_check_status,
					  IRQF_ONESHOT, "chg", chg);
	if (rv < 0)
		return rv;

	rv = devm_request_any_context_irq(dev, chgin_irq,
					  max77650_charger_check_status,
					  IRQF_ONESHOT, "chgin", chg);
	if (rv < 0)
		return rv;

	battery = devm_power_supply_register(dev,
					     &max77650_battery_desc, &pscfg);
	if (IS_ERR(battery))
		return PTR_ERR(battery);

	rv = of_property_read_u32(dev->of_node,
				  "input-voltage-min-microvolt", &prop);
	if (rv == 0) {
		rv = max77650_charger_set_vchgin_min(chg, prop);
		if (rv)
			return rv;
	}

	rv = of_property_read_u32(dev->of_node,
				  "input-current-limit-microamp", &prop);
	if (rv == 0) {
		rv = max77650_charger_set_ichgin_lim(chg, prop);
		if (rv)
			return rv;
	}

	return max77650_charger_enable(chg);
}

static void max77650_charger_remove(struct platform_device *pdev)
{
	struct max77650_charger_data *chg = platform_get_drvdata(pdev);

	max77650_charger_disable(chg);
}

static const struct of_device_id max77650_charger_of_match[] = {
	{ .compatible = "maxim,max77650-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77650_charger_of_match);

static struct platform_driver max77650_charger_driver = {
	.driver = {
		.name = "max77650-charger",
		.of_match_table = max77650_charger_of_match,
	},
	.probe = max77650_charger_probe,
	.remove = max77650_charger_remove,
};
module_platform_driver(max77650_charger_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 charger driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:max77650-charger");
