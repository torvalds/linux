// SPDX-License-Identifier: GPL-2.0
//
//  max17040_battery.c
//  fuel-gauge systems for lithium-ion (Li+) batteries
//
//  Copyright (C) 2009 Samsung Electronics
//  Minkyu Kang <mk7.kang@samsung.com>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/max17040_battery.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define MAX17040_VCELL	0x02
#define MAX17040_SOC	0x04
#define MAX17040_MODE	0x06
#define MAX17040_VER	0x08
#define MAX17040_CONFIG	0x0C
#define MAX17040_CMD	0xFE


#define MAX17040_DELAY		1000
#define MAX17040_BATTERY_FULL	95

#define MAX17040_ATHD_MASK		0x3f
#define MAX17040_ATHD_DEFAULT_POWER_UP	4

struct max17040_chip {
	struct i2c_client		*client;
	struct regmap			*regmap;
	struct delayed_work		work;
	struct power_supply		*battery;
	struct max17040_platform_data	*pdata;

	/* battery capacity */
	int soc;
	/* State Of Charge */
	int status;
	/* Low alert threshold from 32% to 1% of the State of Charge */
	u32 low_soc_alert;
};

static int max17040_reset(struct max17040_chip *chip)
{
	return regmap_write(chip->regmap, MAX17040_CMD, 0x0054);
}

static int max17040_set_low_soc_alert(struct max17040_chip *chip, u32 level)
{
	level = 32 - level;
	return regmap_update_bits(chip->regmap, MAX17040_CONFIG,
			MAX17040_ATHD_MASK, level);
}

static int max17040_get_vcell(struct max17040_chip *chip)
{
	u32 vcell;

	regmap_read(chip->regmap, MAX17040_VCELL, &vcell);

	return (vcell >> 4) * 1250;
}

static int max17040_get_soc(struct max17040_chip *chip)
{
	u32 soc;

	regmap_read(chip->regmap, MAX17040_SOC, &soc);

	return soc >> 8;
}

static int max17040_get_version(struct max17040_chip *chip)
{
	int ret;
	u32 version;

	ret = regmap_read(chip->regmap, MAX17040_VER, &version);

	return ret ? ret : version;
}

static int max17040_get_online(struct max17040_chip *chip)
{
	return chip->pdata && chip->pdata->battery_online ?
		chip->pdata->battery_online() : 1;
}

static int max17040_get_status(struct max17040_chip *chip)
{
	if (!chip->pdata || !chip->pdata->charger_online
			|| !chip->pdata->charger_enable)
		return POWER_SUPPLY_STATUS_UNKNOWN;

	if (max17040_get_soc(chip) > MAX17040_BATTERY_FULL)
		return POWER_SUPPLY_STATUS_FULL;

	if (chip->pdata->charger_online())
		if (chip->pdata->charger_enable())
			return POWER_SUPPLY_STATUS_CHARGING;
		else
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
	else
		return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int max17040_get_of_data(struct max17040_chip *chip)
{
	struct device *dev = &chip->client->dev;

	chip->low_soc_alert = MAX17040_ATHD_DEFAULT_POWER_UP;
	device_property_read_u32(dev,
				 "maxim,alert-low-soc-level",
				 &chip->low_soc_alert);

	if (chip->low_soc_alert <= 0 || chip->low_soc_alert >= 33) {
		dev_err(dev, "maxim,alert-low-soc-level out of bounds\n");
		return -EINVAL;
	}

	return 0;
}

static void max17040_check_changes(struct max17040_chip *chip)
{
	chip->soc = max17040_get_soc(chip);
	chip->status = max17040_get_status(chip);
}

static void max17040_queue_work(struct max17040_chip *chip)
{
	queue_delayed_work(system_power_efficient_wq, &chip->work,
			   MAX17040_DELAY);
}

static void max17040_stop_work(void *data)
{
	struct max17040_chip *chip = data;

	cancel_delayed_work_sync(&chip->work);
}

static void max17040_work(struct work_struct *work)
{
	struct max17040_chip *chip;
	int last_soc, last_status;

	chip = container_of(work, struct max17040_chip, work.work);

	/* store SOC and status to check changes */
	last_soc = chip->soc;
	last_status = chip->status;
	max17040_check_changes(chip);

	/* check changes and send uevent */
	if (last_soc != chip->soc || last_status != chip->status)
		power_supply_changed(chip->battery);

	max17040_queue_work(chip);
}

static irqreturn_t max17040_thread_handler(int id, void *dev)
{
	struct max17040_chip *chip = dev;

	dev_warn(&chip->client->dev, "IRQ: Alert battery low level");

	/* read registers */
	max17040_check_changes(chip);

	/* send uevent */
	power_supply_changed(chip->battery);

	/* reset alert bit */
	max17040_set_low_soc_alert(chip, chip->low_soc_alert);

	return IRQ_HANDLED;
}

static int max17040_enable_alert_irq(struct max17040_chip *chip)
{
	struct i2c_client *client = chip->client;
	unsigned int flags;
	int ret;

	flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					max17040_thread_handler, flags,
					chip->battery->desc->name, chip);

	return ret;
}

static int max17040_prop_writeable(struct power_supply *psy,
				   enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		return 1;
	default:
		return 0;
	}
}

static int max17040_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val)
{
	struct max17040_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		/* alert threshold can be programmed from 1% up to 32% */
		if ((val->intval < 1) || (val->intval > 32)) {
			ret = -EINVAL;
			break;
		}
		ret = max17040_set_low_soc_alert(chip, val->intval);
		chip->low_soc_alert = val->intval;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int max17040_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17040_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = max17040_get_status(chip);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = max17040_get_online(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = max17040_get_vcell(chip);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = max17040_get_soc(chip);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		val->intval = chip->low_soc_alert;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct regmap_config max17040_regmap = {
	.reg_bits	= 8,
	.reg_stride	= 2,
	.val_bits	= 16,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static enum power_supply_property max17040_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN,
};

static const struct power_supply_desc max17040_battery_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.get_property		= max17040_get_property,
	.set_property		= max17040_set_property,
	.property_is_writeable  = max17040_prop_writeable,
	.properties		= max17040_battery_props,
	.num_properties		= ARRAY_SIZE(max17040_battery_props),
};

static int max17040_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct power_supply_config psy_cfg = {};
	struct max17040_chip *chip;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->regmap = devm_regmap_init_i2c(client, &max17040_regmap);
	chip->pdata = client->dev.platform_data;
	ret = max17040_get_of_data(chip);
	if (ret)
		return ret;

	i2c_set_clientdata(client, chip);
	psy_cfg.drv_data = chip;

	chip->battery = devm_power_supply_register(&client->dev,
				&max17040_battery_desc, &psy_cfg);
	if (IS_ERR(chip->battery)) {
		dev_err(&client->dev, "failed: power supply register\n");
		return PTR_ERR(chip->battery);
	}

	ret = max17040_get_version(chip);
	if (ret < 0)
		return ret;
	dev_dbg(&chip->client->dev, "MAX17040 Fuel-Gauge Ver 0x%x\n", ret);

	max17040_reset(chip);

	/* check interrupt */
	if (client->irq && of_device_is_compatible(client->dev.of_node,
						   "maxim,max77836-battery")) {
		ret = max17040_set_low_soc_alert(chip, chip->low_soc_alert);
		if (ret) {
			dev_err(&client->dev,
				"Failed to set low SOC alert: err %d\n", ret);
			return ret;
		}

		ret = max17040_enable_alert_irq(chip);
		if (ret) {
			client->irq = 0;
			dev_warn(&client->dev,
				 "Failed to get IRQ err %d\n", ret);
		}
	}

	INIT_DEFERRABLE_WORK(&chip->work, max17040_work);
	ret = devm_add_action(&client->dev, max17040_stop_work, chip);
	if (ret)
		return ret;
	max17040_queue_work(chip);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int max17040_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max17040_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work(&chip->work);

	if (client->irq && device_may_wakeup(dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int max17040_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max17040_chip *chip = i2c_get_clientdata(client);

	if (client->irq && device_may_wakeup(dev))
		disable_irq_wake(client->irq);

	max17040_queue_work(chip);

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

static const struct of_device_id max17040_of_match[] = {
	{ .compatible = "maxim,max17040" },
	{ .compatible = "maxim,max77836-battery" },
	{ },
};
MODULE_DEVICE_TABLE(of, max17040_of_match);

static struct i2c_driver max17040_i2c_driver = {
	.driver	= {
		.name	= "max17040",
		.of_match_table = max17040_of_match,
		.pm	= MAX17040_PM_OPS,
	},
	.probe		= max17040_probe,
	.id_table	= max17040_id,
};
module_i2c_driver(max17040_i2c_driver);

MODULE_AUTHOR("Minkyu Kang <mk7.kang@samsung.com>");
MODULE_DESCRIPTION("MAX17040 Fuel Gauge");
MODULE_LICENSE("GPL");
