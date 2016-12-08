/*
 * max8997_charger.c - Power supply consumer driver for the Maxim 8997/8966
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
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
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>

struct charger_data {
	struct device *dev;
	struct max8997_dev *iodev;
	struct power_supply *battery;
};

static enum power_supply_property max8997_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* "FULL" or "NOT FULL" only. */
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

		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 0;
		ret = max8997_read_reg(i2c, MAX8997_REG_STATUS4, &reg);
		if (ret)
			return ret;
		if ((reg & (1 << 2)) == 0x0)
			val->intval = 1;

		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		ret = max8997_read_reg(i2c, MAX8997_REG_STATUS4, &reg);
		if (ret)
			return ret;
		/* DCINOK */
		if (reg & (1 << 1))
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
	struct max8997_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct power_supply_config psy_cfg = {};

	if (!pdata)
		return -EINVAL;

	if (pdata->eoc_mA) {
		int val = (pdata->eoc_mA - 50) / 10;
		if (val < 0)
			val = 0;
		if (val > 0xf)
			val = 0xf;

		ret = max8997_update_reg(iodev->i2c,
				MAX8997_REG_MBCCTRL5, val, 0xf);
		if (ret < 0) {
			dev_err(&pdev->dev, "Cannot use i2c bus.\n");
			return ret;
		}
	}

	switch (pdata->timeout) {
	case 5:
		ret = max8997_update_reg(iodev->i2c, MAX8997_REG_MBCCTRL1,
				0x2 << 4, 0x7 << 4);
		break;
	case 6:
		ret = max8997_update_reg(iodev->i2c, MAX8997_REG_MBCCTRL1,
				0x3 << 4, 0x7 << 4);
		break;
	case 7:
		ret = max8997_update_reg(iodev->i2c, MAX8997_REG_MBCCTRL1,
				0x4 << 4, 0x7 << 4);
		break;
	case 0:
		ret = max8997_update_reg(iodev->i2c, MAX8997_REG_MBCCTRL1,
				0x7 << 4, 0x7 << 4);
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

	charger = devm_kzalloc(&pdev->dev, sizeof(struct charger_data),
				GFP_KERNEL);
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

static int __init max8997_battery_init(void)
{
	return platform_driver_register(&max8997_battery_driver);
}
subsys_initcall(max8997_battery_init);

static void __exit max8997_battery_cleanup(void)
{
	platform_driver_unregister(&max8997_battery_driver);
}
module_exit(max8997_battery_cleanup);

MODULE_DESCRIPTION("MAXIM 8997/8966 battery control driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
