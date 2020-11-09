// SPDX-License-Identifier: GPL-2.0+
//
// max8997_charger.c - Power supply consumer driver for the Maxim 8997/8966
//
//  Copyright (C) 2011 Samsung Electronics
//  MyungJoo Ham <myungjoo.ham@samsung.com>

#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>

/* MAX8997_REG_STATUS4 */
#define DCINOK_SHIFT		1
#define DCINOK_MASK		(1 << DCINOK_SHIFT)
#define DETBAT_SHIFT		2
#define DETBAT_MASK		(1 << DETBAT_SHIFT)

/* MAX8997_REG_MBCCTRL1 */
#define TFCH_SHIFT		4
#define TFCH_MASK		(7 << TFCH_SHIFT)

/* MAX8997_REG_MBCCTRL5 */
#define ITOPOFF_SHIFT		0
#define ITOPOFF_MASK		(0xF << ITOPOFF_SHIFT)

struct charger_data {
	struct device *dev;
	struct max8997_dev *iodev;
	struct power_supply *battery;
};

static enum power_supply_property max8997_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* "FULL", "CHARGING" or "DISCHARGING". */
	POWER_SUPPLY_PROP_PRESENT, /* the presence of battery */
	POWER_SUPPLY_PROP_ONLINE, /* charger is active or not */
};

/* Note that the charger control is done by a current regulator "CHARGER" */
static int max8997_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct charger_data *charger = power_supply_get_drvdata(psy);
	struct i2c_client *i2c = charger->iodev->i2c;
	int ret;
	u8 reg;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 0;
		ret = max8997_read_reg(i2c, MAX8997_REG_STATUS4, &reg);
		if (ret)
			return ret;
		if ((reg & (1 << 0)) == 0x1)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if ((reg & DCINOK_MASK))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;

		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 0;
		ret = max8997_read_reg(i2c, MAX8997_REG_STATUS4, &reg);
		if (ret)
			return ret;
		if ((reg & DETBAT_MASK) == 0x0)
			val->intval = 1;

		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		ret = max8997_read_reg(i2c, MAX8997_REG_STATUS4, &reg);
		if (ret)
			return ret;
		if (reg & DCINOK_MASK)
			val->intval = 1;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc max8997_battery_desc = {
	.name		= "max8997_pmic",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= max8997_battery_get_property,
	.properties	= max8997_battery_props,
	.num_properties	= ARRAY_SIZE(max8997_battery_props),
};

static int max8997_battery_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct charger_data *charger;
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *i2c = iodev->i2c;
	struct max8997_platform_data *pdata = iodev->pdata;
	struct power_supply_config psy_cfg = {};

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data supplied.\n");
		return -EINVAL;
	}

	if (pdata->eoc_mA) {
		int val = (pdata->eoc_mA - 50) / 10;
		if (val < 0)
			val = 0;
		if (val > 0xf)
			val = 0xf;

		ret = max8997_update_reg(i2c, MAX8997_REG_MBCCTRL5,
				val << ITOPOFF_SHIFT, ITOPOFF_MASK);
		if (ret < 0) {
			dev_err(&pdev->dev, "Cannot use i2c bus.\n");
			return ret;
		}
	}
	switch (pdata->timeout) {
	case 5:
		ret = max8997_update_reg(i2c, MAX8997_REG_MBCCTRL1,
				0x2 << TFCH_SHIFT, TFCH_MASK);
		break;
	case 6:
		ret = max8997_update_reg(i2c, MAX8997_REG_MBCCTRL1,
				0x3 << TFCH_SHIFT, TFCH_MASK);
		break;
	case 7:
		ret = max8997_update_reg(i2c, MAX8997_REG_MBCCTRL1,
				0x4 << TFCH_SHIFT, TFCH_MASK);
		break;
	case 0:
		ret = max8997_update_reg(i2c, MAX8997_REG_MBCCTRL1,
				0x7 << TFCH_SHIFT, TFCH_MASK);
		break;
	default:
		dev_err(&pdev->dev, "incorrect timeout value (%d)\n",
				pdata->timeout);
		return -EINVAL;
	}
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot use i2c bus.\n");
		return ret;
	}

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	platform_set_drvdata(pdev, charger);

	charger->dev = &pdev->dev;
	charger->iodev = iodev;

	psy_cfg.drv_data = charger;

	charger->battery = devm_power_supply_register(&pdev->dev,
						 &max8997_battery_desc,
						 &psy_cfg);
	if (IS_ERR(charger->battery)) {
		dev_err(&pdev->dev, "failed: power supply register\n");
		return PTR_ERR(charger->battery);
	}

	return 0;
}

static const struct platform_device_id max8997_battery_id[] = {
	{ "max8997-battery", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, max8997_battery_id);

static struct platform_driver max8997_battery_driver = {
	.driver = {
		.name = "max8997-battery",
	},
	.probe = max8997_battery_probe,
	.id_table = max8997_battery_id,
};
module_platform_driver(max8997_battery_driver);

MODULE_DESCRIPTION("MAXIM 8997/8966 battery control driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
