// SPDX-License-Identifier: GPL-2.0+
//
// max8998_charger.c - Power supply consumer driver for the Maxim 8998/LP3974
//
//  Copyright (C) 2009-2010 Samsung Electronics
//  MyungJoo Ham <myungjoo.ham@samsung.com>

#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/max8998.h>
#include <linux/mfd/max8998-private.h>

struct max8998_battery_data {
	struct device *dev;
	struct max8998_dev *iodev;
	struct power_supply *battery;
};

static enum power_supply_property max8998_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT, /* the presence of battery */
	POWER_SUPPLY_PROP_ONLINE, /* charger is active or not */
	POWER_SUPPLY_PROP_STATUS, /* charger is charging/discharging/full */
};

/* Note that the charger control is done by a current regulator "CHARGER" */
static int max8998_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8998_battery_data *max8998 = power_supply_get_drvdata(psy);
	struct i2c_client *i2c = max8998->iodev->i2c;
	int ret;
	u8 reg;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		ret = max8998_read_reg(i2c, MAX8998_REG_STATUS2, &reg);
		if (ret)
			return ret;
		if (reg & (1 << 4))
			val->intval = 0;
		else
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = max8998_read_reg(i2c, MAX8998_REG_STATUS2, &reg);
		if (ret)
			return ret;

		if (reg & (1 << 5))
			val->intval = 1;
		else
			val->intval = 0;

		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = max8998_read_reg(i2c, MAX8998_REG_STATUS2, &reg);
		if (ret)
			return ret;

		if (!(reg & (1 << 5))) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			if (reg & (1 << 6))
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (reg & (1 << 3))
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc max8998_battery_desc = {
	.name		= "max8998_pmic",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= max8998_battery_get_property,
	.properties	= max8998_battery_props,
	.num_properties	= ARRAY_SIZE(max8998_battery_props),
};

static int max8998_battery_probe(struct platform_device *pdev)
{
	struct max8998_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8998_platform_data *pdata = iodev->pdata;
	struct power_supply_config psy_cfg = {};
	struct max8998_battery_data *max8998;
	struct i2c_client *i2c;
	int ret = 0;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied\n");
		return -ENODEV;
	}

	max8998 = devm_kzalloc(&pdev->dev, sizeof(struct max8998_battery_data),
				GFP_KERNEL);
	if (!max8998)
		return -ENOMEM;

	max8998->dev = &pdev->dev;
	max8998->iodev = iodev;
	platform_set_drvdata(pdev, max8998);
	i2c = max8998->iodev->i2c;

	/* Setup "End of Charge" */
	/* If EOC value equals 0,
	 * remain value set from bootloader or default value */
	if (pdata->eoc >= 10 && pdata->eoc <= 45) {
		max8998_update_reg(i2c, MAX8998_REG_CHGR1,
				(pdata->eoc / 5 - 2) << 5, 0x7 << 5);
	} else if (pdata->eoc == 0) {
		dev_dbg(max8998->dev,
			"EOC value not set: leave it unchanged.\n");
	} else {
		dev_err(max8998->dev, "Invalid EOC value\n");
		return -EINVAL;
	}

	/* Setup Charge Restart Level */
	switch (pdata->restart) {
	case 100:
		max8998_update_reg(i2c, MAX8998_REG_CHGR1, 0x1 << 3, 0x3 << 3);
		break;
	case 150:
		max8998_update_reg(i2c, MAX8998_REG_CHGR1, 0x0 << 3, 0x3 << 3);
		break;
	case 200:
		max8998_update_reg(i2c, MAX8998_REG_CHGR1, 0x2 << 3, 0x3 << 3);
		break;
	case -1:
		max8998_update_reg(i2c, MAX8998_REG_CHGR1, 0x3 << 3, 0x3 << 3);
		break;
	case 0:
		dev_dbg(max8998->dev,
			"Restart Level not set: leave it unchanged.\n");
		break;
	default:
		dev_err(max8998->dev, "Invalid Restart Level\n");
		return -EINVAL;
	}

	/* Setup Charge Full Timeout */
	switch (pdata->timeout) {
	case 5:
		max8998_update_reg(i2c, MAX8998_REG_CHGR2, 0x0 << 4, 0x3 << 4);
		break;
	case 6:
		max8998_update_reg(i2c, MAX8998_REG_CHGR2, 0x1 << 4, 0x3 << 4);
		break;
	case 7:
		max8998_update_reg(i2c, MAX8998_REG_CHGR2, 0x2 << 4, 0x3 << 4);
		break;
	case -1:
		max8998_update_reg(i2c, MAX8998_REG_CHGR2, 0x3 << 4, 0x3 << 4);
		break;
	case 0:
		dev_dbg(max8998->dev,
			"Full Timeout not set: leave it unchanged.\n");
		break;
	default:
		dev_err(max8998->dev, "Invalid Full Timeout value\n");
		return -EINVAL;
	}

	psy_cfg.drv_data = max8998;

	max8998->battery = devm_power_supply_register(max8998->dev,
						      &max8998_battery_desc,
						      &psy_cfg);
	if (IS_ERR(max8998->battery)) {
		ret = PTR_ERR(max8998->battery);
		dev_err(max8998->dev, "failed: power supply register: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id max8998_battery_id[] = {
	{ "max8998-battery", TYPE_MAX8998 },
	{ }
};
MODULE_DEVICE_TABLE(platform, max8998_battery_id);

static struct platform_driver max8998_battery_driver = {
	.driver = {
		.name = "max8998-battery",
	},
	.probe = max8998_battery_probe,
	.id_table = max8998_battery_id,
};

module_platform_driver(max8998_battery_driver);

MODULE_DESCRIPTION("MAXIM 8998 battery control driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:max8998-battery");
