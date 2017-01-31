/*
 *  max17040_battery.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2009 Samsung Electronics
 *  Minkyu Kang <mk7.kang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/max17040_battery.h>
#include <linux/slab.h>

#define MAX17040_VCELL	0x02
#define MAX17040_SOC	0x04
#define MAX17040_MODE	0x06
#define MAX17040_VER	0x08
#define MAX17040_RCOMP	0x0C
#define MAX17040_CMD	0xFE


#define MAX17040_DELAY		1000
#define MAX17040_BATTERY_FULL	95

struct max17040_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct power_supply		*battery;
	struct max17040_platform_data	*pdata;

	/* State Of Connect */
	int online;
	/* battery voltage */
	int vcell;
	/* battery capacity */
	int soc;
	/* State Of Charge */
	int status;
};

static int max17040_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17040_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->vcell;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int max17040_write_reg(struct i2c_client *client, int reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_swapped(client, reg, value);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int max17040_read_reg(struct i2c_client *client, int reg)
{
	int ret;

	ret = i2c_smbus_read_word_swapped(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static void max17040_reset(struct i2c_client *client)
{
	max17040_write_reg(client, MAX17040_CMD, 0x0054);
}

static void max17040_get_vcell(struct i2c_client *client)
{
	struct max17040_chip *chip = i2c_get_clientdata(client);
	u16 vcell;

	vcell = max17040_read_reg(client, MAX17040_VCELL);

	chip->vcell = vcell;
}

static void max17040_get_soc(struct i2c_client *client)
{
	struct max17040_chip *chip = i2c_get_clientdata(client);
	u16 soc;

	soc = max17040_read_reg(client, MAX17040_SOC);

	chip->soc = (soc >> 8);
}

static void max17040_get_version(struct i2c_client *client)
{
	u16 version;

	version = max17040_read_reg(client, MAX17040_VER);

	dev_info(&client->dev, "MAX17040 Fuel-Gauge Ver 0x%x\n", version);
}

static void max17040_get_online(struct i2c_client *client)
{
	struct max17040_chip *chip = i2c_get_clientdata(client);

	if (chip->pdata && chip->pdata->battery_online)
		chip->online = chip->pdata->battery_online();
	else
		chip->online = 1;
}

static void max17040_get_status(struct i2c_client *client)
{
	struct max17040_chip *chip = i2c_get_clientdata(client);

	if (!chip->pdata || !chip->pdata->charger_online
			|| !chip->pdata->charger_enable) {
		chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
		return;
	}

	if (chip->pdata->charger_online()) {
		if (chip->pdata->charger_enable())
			chip->status = POWER_SUPPLY_STATUS_CHARGING;
		else
			chip->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (chip->soc > MAX17040_BATTERY_FULL)
		chip->status = POWER_SUPPLY_STATUS_FULL;
}

static void max17040_work(struct work_struct *work)
{
	struct max17040_chip *chip;

	chip = container_of(work, struct max17040_chip, work.work);

	max17040_get_vcell(chip->client);
	max17040_get_soc(chip->client);
	max17040_get_online(chip->client);
	max17040_get_status(chip->client);

	queue_delayed_work(system_power_efficient_wq, &chip->work,
			   MAX17040_DELAY);
}

static enum power_supply_property max17040_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static const struct power_supply_desc max17040_battery_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= max17040_get_property,
	.properties	= max17040_battery_props,
	.num_properties	= ARRAY_SIZE(max17040_battery_props),
};

static int max17040_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct max17040_chip *chip;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, chip);
	psy_cfg.drv_data = chip;

	chip->battery = power_supply_register(&client->dev,
				&max17040_battery_desc, &psy_cfg);
	if (IS_ERR(chip->battery)) {
		dev_err(&client->dev, "failed: power supply register\n");
		return PTR_ERR(chip->battery);
	}

	max17040_reset(client);
	max17040_get_version(client);

	INIT_DEFERRABLE_WORK(&chip->work, max17040_work);
	queue_delayed_work(system_power_efficient_wq, &chip->work,
			   MAX17040_DELAY);

	return 0;
}

static int max17040_remove(struct i2c_client *client)
{
	struct max17040_chip *chip = i2c_get_clientdata(client);

	power_supply_unregister(chip->battery);
	cancel_delayed_work(&chip->work);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int max17040_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max17040_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work(&chip->work);
	return 0;
}

static int max17040_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max17040_chip *chip = i2c_get_clientdata(client);

	queue_delayed_work(system_power_efficient_wq, &chip->work,
			   MAX17040_DELAY);
	return 0;
}

static SIMPLE_DEV_PM_OPS(max17040_pm_ops, max17040_suspend, max17040_resume);
#define MAX17040_PM_OPS (&max17040_pm_ops)

#else

#define MAX17040_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

static const struct i2c_device_id max17040_id[] = {
	{ "max17040" },
	{ "max77836-battery" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17040_id);

static struct i2c_driver max17040_i2c_driver = {
	.driver	= {
		.name	= "max17040",
		.pm	= MAX17040_PM_OPS,
	},
	.probe		= max17040_probe,
	.remove		= max17040_remove,
	.id_table	= max17040_id,
};
module_i2c_driver(max17040_i2c_driver);

MODULE_AUTHOR("Minkyu Kang <mk7.kang@samsung.com>");
MODULE_DESCRIPTION("MAX17040 Fuel Gauge");
MODULE_LICENSE("GPL");
