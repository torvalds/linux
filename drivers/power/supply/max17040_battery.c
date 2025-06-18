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
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>

#define MAX17040_VCELL	0x02
#define MAX17040_SOC	0x04
#define MAX17040_MODE	0x06
#define MAX17040_VER	0x08
#define MAX17040_CONFIG	0x0C
#define MAX17040_STATUS	0x1A
#define MAX17040_CMD	0xFE


#define MAX17040_DELAY		1000
#define MAX17040_BATTERY_FULL	95
#define MAX17040_RCOMP_DEFAULT  0x9700

#define MAX17040_ATHD_MASK		0x3f
#define MAX17040_ALSC_MASK		0x40
#define MAX17040_ATHD_DEFAULT_POWER_UP	4
#define MAX17040_STATUS_HD_MASK		0x1000
#define MAX17040_STATUS_SC_MASK		0x2000
#define MAX17040_CFG_RCOMP_MASK		0xff00

enum chip_id {
	ID_MAX17040,
	ID_MAX17041,
	ID_MAX17043,
	ID_MAX17044,
	ID_MAX17048,
	ID_MAX17049,
	ID_MAX17058,
	ID_MAX17059,
};

/* values that differ by chip_id */
struct chip_data {
	u16 reset_val;
	u16 vcell_shift;
	u16 vcell_mul;
	u16 vcell_div;
	u8  has_low_soc_alert;
	u8  rcomp_bytes;
	u8  has_soc_alert;
};

static struct chip_data max17040_family[] = {
	[ID_MAX17040] = {
		.reset_val = 0x0054,
		.vcell_shift = 4,
		.vcell_mul = 1250,
		.vcell_div = 1,
		.has_low_soc_alert = 0,
		.rcomp_bytes = 2,
		.has_soc_alert = 0,
	},
	[ID_MAX17041] = {
		.reset_val = 0x0054,
		.vcell_shift = 4,
		.vcell_mul = 2500,
		.vcell_div = 1,
		.has_low_soc_alert = 0,
		.rcomp_bytes = 2,
		.has_soc_alert = 0,
	},
	[ID_MAX17043] = {
		.reset_val = 0x0054,
		.vcell_shift = 4,
		.vcell_mul = 1250,
		.vcell_div = 1,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 0,
	},
	[ID_MAX17044] = {
		.reset_val = 0x0054,
		.vcell_shift = 4,
		.vcell_mul = 2500,
		.vcell_div = 1,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 0,
	},
	[ID_MAX17048] = {
		.reset_val = 0x5400,
		.vcell_shift = 0,
		.vcell_mul = 625,
		.vcell_div = 8,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 1,
	},
	[ID_MAX17049] = {
		.reset_val = 0x5400,
		.vcell_shift = 0,
		.vcell_mul = 625,
		.vcell_div = 4,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 1,
	},
	[ID_MAX17058] = {
		.reset_val = 0x5400,
		.vcell_shift = 0,
		.vcell_mul = 625,
		.vcell_div = 8,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 0,
	},
	[ID_MAX17059] = {
		.reset_val = 0x5400,
		.vcell_shift = 0,
		.vcell_mul = 625,
		.vcell_div = 4,
		.has_low_soc_alert = 1,
		.rcomp_bytes = 1,
		.has_soc_alert = 0,
	},
};

struct max17040_chip {
	struct i2c_client		*client;
	struct regmap			*regmap;
	struct delayed_work		work;
	struct power_supply		*battery;
	struct chip_data		data;
	struct iio_channel		*channel_temp;

	/* battery capacity */
	int soc;
	/* Low alert threshold from 32% to 1% of the State of Charge */
	u32 low_soc_alert;
	/* some devices return twice the capacity */
	bool quirk_double_soc;
	/* higher 8 bits for 17043+, 16 bits for 17040,41 */
	u16 rcomp;
};

static int max17040_reset(struct max17040_chip *chip)
{
	return regmap_write(chip->regmap, MAX17040_CMD, chip->data.reset_val);
}

static int max17040_set_low_soc_alert(struct max17040_chip *chip, u32 level)
{
	level = 32 - level * (chip->quirk_double_soc ? 2 : 1);
	return regmap_update_bits(chip->regmap, MAX17040_CONFIG,
			MAX17040_ATHD_MASK, level);
}

static int max17040_set_soc_alert(struct max17040_chip *chip, bool enable)
{
	return regmap_update_bits(chip->regmap, MAX17040_CONFIG,
			MAX17040_ALSC_MASK, enable ? MAX17040_ALSC_MASK : 0);
}

static int max17040_set_rcomp(struct max17040_chip *chip, u16 rcomp)
{
	u16 mask = chip->data.rcomp_bytes == 2 ?
		0xffff : MAX17040_CFG_RCOMP_MASK;

	return regmap_update_bits(chip->regmap, MAX17040_CONFIG, mask, rcomp);
}

static int max17040_raw_vcell_to_uvolts(struct max17040_chip *chip, u16 vcell)
{
	struct chip_data *d = &chip->data;

	return (vcell >> d->vcell_shift) * d->vcell_mul / d->vcell_div;
}


static int max17040_get_vcell(struct max17040_chip *chip)
{
	u32 vcell;

	regmap_read(chip->regmap, MAX17040_VCELL, &vcell);

	return max17040_raw_vcell_to_uvolts(chip, vcell);
}

static int max17040_get_soc(struct max17040_chip *chip)
{
	u32 soc;

	regmap_read(chip->regmap, MAX17040_SOC, &soc);

	return soc >> (chip->quirk_double_soc ? 9 : 8);
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
	return 1;
}

static int max17040_get_of_data(struct max17040_chip *chip)
{
	struct device *dev = &chip->client->dev;
	struct chip_data *data = &max17040_family[
		(uintptr_t) of_device_get_match_data(dev)];
	int rcomp_len;
	u8 rcomp[2];

	chip->quirk_double_soc = device_property_read_bool(dev,
							   "maxim,double-soc");

	chip->low_soc_alert = MAX17040_ATHD_DEFAULT_POWER_UP;
	device_property_read_u32(dev,
				 "maxim,alert-low-soc-level",
				 &chip->low_soc_alert);

	if (chip->low_soc_alert <= 0 ||
	    chip->low_soc_alert > (chip->quirk_double_soc ? 16 : 32)) {
		dev_err(dev, "maxim,alert-low-soc-level out of bounds\n");
		return -EINVAL;
	}

	rcomp_len = device_property_count_u8(dev, "maxim,rcomp");
	chip->rcomp = MAX17040_RCOMP_DEFAULT;
	if (rcomp_len == data->rcomp_bytes) {
		if (!device_property_read_u8_array(dev, "maxim,rcomp",
						   rcomp, rcomp_len))
			chip->rcomp = rcomp_len == 2 ? rcomp[0] << 8 | rcomp[1] :
				      rcomp[0] << 8;
	} else if (rcomp_len > 0) {
		dev_err(dev, "maxim,rcomp has incorrect length\n");
		return -EINVAL;
	}

	return 0;
}

static void max17040_check_changes(struct max17040_chip *chip)
{
	chip->soc = max17040_get_soc(chip);
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
	int last_soc;

	chip = container_of(work, struct max17040_chip, work.work);

	/* store SOC to check changes */
	last_soc = chip->soc;
	max17040_check_changes(chip);

	/* check changes and send uevent */
	if (last_soc != chip->soc)
		power_supply_changed(chip->battery);

	max17040_queue_work(chip);
}

/* Returns true if alert cause was SOC change, not low SOC */
static bool max17040_handle_soc_alert(struct max17040_chip *chip)
{
	bool ret = true;
	u32 data;

	regmap_read(chip->regmap, MAX17040_STATUS, &data);

	if (data & MAX17040_STATUS_HD_MASK) {
		// this alert was caused by low soc
		ret = false;
	}
	if (data & MAX17040_STATUS_SC_MASK) {
		// soc change bit -- deassert to mark as handled
		regmap_write(chip->regmap, MAX17040_STATUS,
				data & ~MAX17040_STATUS_SC_MASK);
	}

	return ret;
}

static irqreturn_t max17040_thread_handler(int id, void *dev)
{
	struct max17040_chip *chip = dev;

	if (!(chip->data.has_soc_alert && max17040_handle_soc_alert(chip)))
		dev_warn(&chip->client->dev, "IRQ: Alert battery low level\n");

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
	int ret;

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					max17040_thread_handler, IRQF_ONESHOT,
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
		/* alert threshold can be programmed from 1% up to 16/32% */
		if ((val->intval < 1) ||
		    (val->intval > (chip->quirk_double_soc ? 16 : 32))) {
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
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
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
	case POWER_SUPPLY_PROP_STATUS:
		power_supply_get_property_from_supplier(psy, psp, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!chip->channel_temp)
			return -ENODATA;

		iio_read_channel_processed(chip->channel_temp, &val->intval);
		val->intval /= 100; /* Convert from milli- to deci-degree */

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
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
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

static int max17040_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct i2c_adapter *adapter = client->adapter;
	struct power_supply_config psy_cfg = {};
	struct max17040_chip *chip;
	enum chip_id chip_id;
	bool enable_irq = false;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->regmap = devm_regmap_init_i2c(client, &max17040_regmap);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);
	chip_id = (enum chip_id) id->driver_data;
	if (client->dev.of_node) {
		ret = max17040_get_of_data(chip);
		if (ret)
			return ret;
		chip_id = (uintptr_t)of_device_get_match_data(&client->dev);
	}
	chip->data = max17040_family[chip_id];

	i2c_set_clientdata(client, chip);
	psy_cfg.drv_data = chip;

	/* Switch to devm_iio_channel_get_optional when available  */
	chip->channel_temp = devm_iio_channel_get(&client->dev, "temp");
	if (IS_ERR(chip->channel_temp)) {
		ret = PTR_ERR(chip->channel_temp);
		if (ret != -ENODEV)
			return dev_err_probe(&client->dev, PTR_ERR(chip->channel_temp),
					     "failed to get temp\n");
		else
			chip->channel_temp = NULL;
	}

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

	if (chip_id == ID_MAX17040 || chip_id == ID_MAX17041)
		max17040_reset(chip);

	max17040_set_rcomp(chip, chip->rcomp);

	/* check interrupt */
	if (client->irq && chip->data.has_low_soc_alert) {
		ret = max17040_set_low_soc_alert(chip, chip->low_soc_alert);
		if (ret) {
			dev_err(&client->dev,
				"Failed to set low SOC alert: err %d\n", ret);
			return ret;
		}

		enable_irq = true;
	}

	if (client->irq && chip->data.has_soc_alert) {
		ret = max17040_set_soc_alert(chip, 1);
		if (ret) {
			dev_err(&client->dev,
				"Failed to set SOC alert: err %d\n", ret);
			return ret;
		}
		enable_irq = true;
	} else {
		/* soc alerts negate the need for polling */
		INIT_DEFERRABLE_WORK(&chip->work, max17040_work);
		ret = devm_add_action(&client->dev, max17040_stop_work, chip);
		if (ret)
			return ret;
		max17040_queue_work(chip);
	}

	if (enable_irq) {
		ret = max17040_enable_alert_irq(chip);
		if (ret) {
			client->irq = 0;
			dev_warn(&client->dev,
				 "Failed to get IRQ err %d\n", ret);
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int max17040_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max17040_chip *chip = i2c_get_clientdata(client);

	if (client->irq && chip->data.has_soc_alert)
		// disable soc alert to prevent wakeup
		max17040_set_soc_alert(chip, 0);
	else
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

	if (client->irq && chip->data.has_soc_alert)
		max17040_set_soc_alert(chip, 1);
	else
		max17040_queue_work(chip);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max17040_pm_ops, max17040_suspend, max17040_resume);
#define MAX17040_PM_OPS (&max17040_pm_ops)

#else

#define MAX17040_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

static const struct i2c_device_id max17040_id[] = {
	{ "max17040", ID_MAX17040 },
	{ "max17041", ID_MAX17041 },
	{ "max17043", ID_MAX17043 },
	{ "max77836-battery", ID_MAX17043 },
	{ "max17044", ID_MAX17044 },
	{ "max17048", ID_MAX17048 },
	{ "max17049", ID_MAX17049 },
	{ "max17058", ID_MAX17058 },
	{ "max17059", ID_MAX17059 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, max17040_id);

static const struct of_device_id max17040_of_match[] = {
	{ .compatible = "maxim,max17040", .data = (void *) ID_MAX17040 },
	{ .compatible = "maxim,max17041", .data = (void *) ID_MAX17041 },
	{ .compatible = "maxim,max17043", .data = (void *) ID_MAX17043 },
	{ .compatible = "maxim,max77836-battery", .data = (void *) ID_MAX17043 },
	{ .compatible = "maxim,max17044", .data = (void *) ID_MAX17044 },
	{ .compatible = "maxim,max17048", .data = (void *) ID_MAX17048 },
	{ .compatible = "maxim,max17049", .data = (void *) ID_MAX17049 },
	{ .compatible = "maxim,max17058", .data = (void *) ID_MAX17058 },
	{ .compatible = "maxim,max17059", .data = (void *) ID_MAX17059 },
	{ /* sentinel */ },
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
