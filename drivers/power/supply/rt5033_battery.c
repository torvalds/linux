// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fuel gauge driver for Richtek RT5033
 *
 * Copyright (C) 2014 Samsung Electronics, Co., Ltd.
 * Author: Beomho Seo <beomho.seo@samsung.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/mfd/rt5033-private.h>

struct rt5033_battery {
	struct i2c_client	*client;
	struct regmap		*regmap;
	struct power_supply	*psy;
};

static int rt5033_battery_get_status(struct i2c_client *client)
{
	struct rt5033_battery *battery = i2c_get_clientdata(client);
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property_from_supplier(battery->psy,
						POWER_SUPPLY_PROP_STATUS,
						&val);
	if (ret)
		val.intval = POWER_SUPPLY_STATUS_UNKNOWN;

	return val.intval;
}

static int rt5033_battery_get_capacity(struct i2c_client *client)
{
	struct rt5033_battery *battery = i2c_get_clientdata(client);
	u32 msb;

	regmap_read(battery->regmap, RT5033_FUEL_REG_SOC_H, &msb);

	return msb;
}

static int rt5033_battery_get_present(struct i2c_client *client)
{
	struct rt5033_battery *battery = i2c_get_clientdata(client);
	u32 val;

	regmap_read(battery->regmap, RT5033_FUEL_REG_CONFIG_L, &val);

	return (val & RT5033_FUEL_BAT_PRESENT) ? true : false;
}

static int rt5033_battery_get_watt_prop(struct i2c_client *client,
		enum power_supply_property psp)
{
	struct rt5033_battery *battery = i2c_get_clientdata(client);
	unsigned int regh, regl;
	int ret;
	u32 msb, lsb;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		regh = RT5033_FUEL_REG_VBAT_H;
		regl = RT5033_FUEL_REG_VBAT_L;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		regh = RT5033_FUEL_REG_AVG_VOLT_H;
		regl = RT5033_FUEL_REG_AVG_VOLT_L;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		regh = RT5033_FUEL_REG_OCV_H;
		regl = RT5033_FUEL_REG_OCV_L;
		break;
	default:
		return -EINVAL;
	}

	regmap_read(battery->regmap, regh, &msb);
	regmap_read(battery->regmap, regl, &lsb);

	ret = ((msb << 4) + (lsb >> 4)) * 1250;

	return ret;
}

static int rt5033_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct rt5033_battery *battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = rt5033_battery_get_watt_prop(battery->client,
									psp);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = rt5033_battery_get_present(battery->client);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = rt5033_battery_get_capacity(battery->client);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = rt5033_battery_get_status(battery->client);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property rt5033_battery_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
};

static const struct regmap_config rt5033_battery_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= RT5033_FUEL_REG_END,
};

static const struct power_supply_desc rt5033_battery_desc = {
	.name		= "rt5033-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= rt5033_battery_get_property,
	.properties	= rt5033_battery_props,
	.num_properties	= ARRAY_SIZE(rt5033_battery_props),
};

static int rt5033_battery_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct power_supply_config psy_cfg = {};
	struct rt5033_battery *battery;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	battery = devm_kzalloc(&client->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->client = client;
	battery->regmap = devm_regmap_init_i2c(client,
			&rt5033_battery_regmap_config);
	if (IS_ERR(battery->regmap)) {
		dev_err(&client->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	i2c_set_clientdata(client, battery);
	psy_cfg.of_node = client->dev.of_node;
	psy_cfg.drv_data = battery;

	battery->psy = power_supply_register(&client->dev,
					     &rt5033_battery_desc, &psy_cfg);
	if (IS_ERR(battery->psy))
		return dev_err_probe(&client->dev, PTR_ERR(battery->psy),
				     "Failed to register power supply\n");

	return 0;
}

static void rt5033_battery_remove(struct i2c_client *client)
{
	struct rt5033_battery *battery = i2c_get_clientdata(client);

	power_supply_unregister(battery->psy);
}

static const struct i2c_device_id rt5033_battery_id[] = {
	{ "rt5033-battery", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5033_battery_id);

static const struct of_device_id rt5033_battery_of_match[] = {
	{ .compatible = "richtek,rt5033-battery", },
	{ }
};
MODULE_DEVICE_TABLE(of, rt5033_battery_of_match);

static struct i2c_driver rt5033_battery_driver = {
	.driver = {
		.name = "rt5033-battery",
		.of_match_table = rt5033_battery_of_match,
	},
	.probe_new = rt5033_battery_probe,
	.remove = rt5033_battery_remove,
	.id_table = rt5033_battery_id,
};
module_i2c_driver(rt5033_battery_driver);

MODULE_DESCRIPTION("Richtek RT5033 fuel gauge driver");
MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_LICENSE("GPL");
