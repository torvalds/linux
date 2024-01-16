/*
 * Battery charger driver for TI BQ24735
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#include <linux/power/bq24735-charger.h>

/* BQ24735 available commands and their respective masks */
#define BQ24735_CHARGE_OPT		0x12
#define BQ24735_CHARGE_CURRENT		0x14
#define BQ24735_CHARGE_CURRENT_MASK	0x1fc0
#define BQ24735_CHARGE_VOLTAGE		0x15
#define BQ24735_CHARGE_VOLTAGE_MASK	0x7ff0
#define BQ24735_INPUT_CURRENT		0x3f
#define BQ24735_INPUT_CURRENT_MASK	0x1f80
#define BQ24735_MANUFACTURER_ID		0xfe
#define BQ24735_DEVICE_ID		0xff

/* ChargeOptions bits of interest */
#define BQ24735_CHARGE_OPT_CHG_DISABLE	(1 << 0)
#define BQ24735_CHARGE_OPT_AC_PRESENT	(1 << 4)

struct bq24735 {
	struct power_supply		*charger;
	struct power_supply_desc	charger_desc;
	struct i2c_client		*client;
	struct bq24735_platform		*pdata;
	struct mutex			lock;
	struct gpio_desc		*status_gpio;
	struct delayed_work		poll;
	u32				poll_interval;
	bool				charging;
};

static inline struct bq24735 *to_bq24735(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static enum power_supply_property bq24735_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
};

static int bq24735_charger_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return 1;
	default:
		break;
	}

	return 0;
}

static inline int bq24735_write_word(struct i2c_client *client, u8 reg,
				     u16 value)
{
	return i2c_smbus_write_word_data(client, reg, value);
}

static inline int bq24735_read_word(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_word_data(client, reg);
}

static int bq24735_update_word(struct i2c_client *client, u8 reg,
			       u16 mask, u16 value)
{
	unsigned int tmp;
	int ret;

	ret = bq24735_read_word(client, reg);
	if (ret < 0)
		return ret;

	tmp = ret & ~mask;
	tmp |= value & mask;

	return bq24735_write_word(client, reg, tmp);
}

static int bq24735_config_charger(struct bq24735 *charger)
{
	struct bq24735_platform *pdata = charger->pdata;
	int ret;
	u16 value;

	if (pdata->ext_control)
		return 0;

	if (pdata->charge_current) {
		value = pdata->charge_current & BQ24735_CHARGE_CURRENT_MASK;

		ret = bq24735_write_word(charger->client,
					 BQ24735_CHARGE_CURRENT, value);
		if (ret < 0) {
			dev_err(&charger->client->dev,
				"Failed to write charger current : %d\n",
				ret);
			return ret;
		}
	}

	if (pdata->charge_voltage) {
		value = pdata->charge_voltage & BQ24735_CHARGE_VOLTAGE_MASK;

		ret = bq24735_write_word(charger->client,
					 BQ24735_CHARGE_VOLTAGE, value);
		if (ret < 0) {
			dev_err(&charger->client->dev,
				"Failed to write charger voltage : %d\n",
				ret);
			return ret;
		}
	}

	if (pdata->input_current) {
		value = pdata->input_current & BQ24735_INPUT_CURRENT_MASK;

		ret = bq24735_write_word(charger->client,
					 BQ24735_INPUT_CURRENT, value);
		if (ret < 0) {
			dev_err(&charger->client->dev,
				"Failed to write input current : %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static inline int bq24735_enable_charging(struct bq24735 *charger)
{
	int ret;

	if (charger->pdata->ext_control)
		return 0;

	ret = bq24735_config_charger(charger);
	if (ret)
		return ret;

	return bq24735_update_word(charger->client, BQ24735_CHARGE_OPT,
				   BQ24735_CHARGE_OPT_CHG_DISABLE, 0);
}

static inline int bq24735_disable_charging(struct bq24735 *charger)
{
	if (charger->pdata->ext_control)
		return 0;

	return bq24735_update_word(charger->client, BQ24735_CHARGE_OPT,
				   BQ24735_CHARGE_OPT_CHG_DISABLE,
				   BQ24735_CHARGE_OPT_CHG_DISABLE);
}

static bool bq24735_charger_is_present(struct bq24735 *charger)
{
	if (charger->status_gpio) {
		return !gpiod_get_value_cansleep(charger->status_gpio);
	} else {
		int ac = 0;

		ac = bq24735_read_word(charger->client, BQ24735_CHARGE_OPT);
		if (ac < 0) {
			dev_dbg(&charger->client->dev,
				"Failed to read charger options : %d\n",
				ac);
			return false;
		}
		return (ac & BQ24735_CHARGE_OPT_AC_PRESENT) ? true : false;
	}

	return false;
}

static int bq24735_charger_is_charging(struct bq24735 *charger)
{
	int ret;

	if (!bq24735_charger_is_present(charger))
		return 0;

	ret  = bq24735_read_word(charger->client, BQ24735_CHARGE_OPT);
	if (ret < 0)
		return ret;

	return !(ret & BQ24735_CHARGE_OPT_CHG_DISABLE);
}

static void bq24735_update(struct bq24735 *charger)
{
	mutex_lock(&charger->lock);

	if (charger->charging && bq24735_charger_is_present(charger))
		bq24735_enable_charging(charger);
	else
		bq24735_disable_charging(charger);

	mutex_unlock(&charger->lock);

	power_supply_changed(charger->charger);
}

static irqreturn_t bq24735_charger_isr(int irq, void *devid)
{
	struct power_supply *psy = devid;
	struct bq24735 *charger = to_bq24735(psy);

	bq24735_update(charger);

	return IRQ_HANDLED;
}

static void bq24735_poll(struct work_struct *work)
{
	struct bq24735 *charger = container_of(work, struct bq24735, poll.work);

	bq24735_update(charger);

	schedule_delayed_work(&charger->poll,
			      msecs_to_jiffies(charger->poll_interval));
}

static int bq24735_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct bq24735 *charger = to_bq24735(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = bq24735_charger_is_present(charger) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		switch (bq24735_charger_is_charging(charger)) {
		case 1:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 0:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bq24735_charger_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct bq24735 *charger = to_bq24735(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		switch (val->intval) {
		case POWER_SUPPLY_STATUS_CHARGING:
			mutex_lock(&charger->lock);
			charger->charging = true;
			ret = bq24735_enable_charging(charger);
			mutex_unlock(&charger->lock);
			if (ret)
				return ret;
			break;
		case POWER_SUPPLY_STATUS_DISCHARGING:
		case POWER_SUPPLY_STATUS_NOT_CHARGING:
			mutex_lock(&charger->lock);
			charger->charging = false;
			ret = bq24735_disable_charging(charger);
			mutex_unlock(&charger->lock);
			if (ret)
				return ret;
			break;
		default:
			return -EINVAL;
		}
		power_supply_changed(psy);
		break;
	default:
		return -EPERM;
	}

	return 0;
}

static struct bq24735_platform *bq24735_parse_dt_data(struct i2c_client *client)
{
	struct bq24735_platform *pdata;
	struct device_node *np = client->dev.of_node;
	u32 val;
	int ret;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->dev,
			"Memory alloc for bq24735 pdata failed\n");
		return NULL;
	}

	ret = of_property_read_u32(np, "ti,charge-current", &val);
	if (!ret)
		pdata->charge_current = val;

	ret = of_property_read_u32(np, "ti,charge-voltage", &val);
	if (!ret)
		pdata->charge_voltage = val;

	ret = of_property_read_u32(np, "ti,input-current", &val);
	if (!ret)
		pdata->input_current = val;

	pdata->ext_control = of_property_read_bool(np, "ti,external-control");

	return pdata;
}

static int bq24735_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret;
	struct bq24735 *charger;
	struct power_supply_desc *supply_desc;
	struct power_supply_config psy_cfg = {};
	char *name;

	charger = devm_kzalloc(&client->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	mutex_init(&charger->lock);
	charger->charging = true;
	charger->pdata = client->dev.platform_data;

	if (IS_ENABLED(CONFIG_OF) && !charger->pdata && client->dev.of_node)
		charger->pdata = bq24735_parse_dt_data(client);

	if (!charger->pdata) {
		dev_err(&client->dev, "no platform data provided\n");
		return -EINVAL;
	}

	name = (char *)charger->pdata->name;
	if (!name) {
		name = devm_kasprintf(&client->dev, GFP_KERNEL,
				      "bq24735@%s",
				      dev_name(&client->dev));
		if (!name) {
			dev_err(&client->dev, "Failed to alloc device name\n");
			return -ENOMEM;
		}
	}

	charger->client = client;

	supply_desc = &charger->charger_desc;

	supply_desc->name = name;
	supply_desc->type = POWER_SUPPLY_TYPE_MAINS;
	supply_desc->properties = bq24735_charger_properties;
	supply_desc->num_properties = ARRAY_SIZE(bq24735_charger_properties);
	supply_desc->get_property = bq24735_charger_get_property;
	supply_desc->set_property = bq24735_charger_set_property;
	supply_desc->property_is_writeable =
				bq24735_charger_property_is_writeable;

	psy_cfg.supplied_to = charger->pdata->supplied_to;
	psy_cfg.num_supplicants = charger->pdata->num_supplicants;
	psy_cfg.of_node = client->dev.of_node;
	psy_cfg.drv_data = charger;

	i2c_set_clientdata(client, charger);

	charger->status_gpio = devm_gpiod_get_optional(&client->dev,
						       "ti,ac-detect",
						       GPIOD_IN);
	if (IS_ERR(charger->status_gpio)) {
		ret = PTR_ERR(charger->status_gpio);
		dev_err(&client->dev, "Getting gpio failed: %d\n", ret);
		return ret;
	}

	if (bq24735_charger_is_present(charger)) {
		ret = bq24735_read_word(client, BQ24735_MANUFACTURER_ID);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to read manufacturer id : %d\n",
				ret);
			return ret;
		} else if (ret != 0x0040) {
			dev_err(&client->dev,
				"manufacturer id mismatch. 0x0040 != 0x%04x\n", ret);
			return -ENODEV;
		}

		ret = bq24735_read_word(client, BQ24735_DEVICE_ID);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to read device id : %d\n", ret);
			return ret;
		} else if (ret != 0x000B) {
			dev_err(&client->dev,
				"device id mismatch. 0x000b != 0x%04x\n", ret);
			return -ENODEV;
		}

		ret = bq24735_enable_charging(charger);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable charging\n");
			return ret;
		}
	}

	charger->charger = devm_power_supply_register(&client->dev, supply_desc,
						      &psy_cfg);
	if (IS_ERR(charger->charger)) {
		ret = PTR_ERR(charger->charger);
		dev_err(&client->dev, "Failed to register power supply: %d\n",
			ret);
		return ret;
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, bq24735_charger_isr,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						supply_desc->name,
						charger->charger);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register IRQ %d err %d\n",
				client->irq, ret);
			return ret;
		}
	} else {
		ret = device_property_read_u32(&client->dev, "poll-interval",
					       &charger->poll_interval);
		if (ret)
			return 0;
		if (!charger->poll_interval)
			return 0;

		ret = devm_delayed_work_autocancel(&client->dev, &charger->poll,
						   bq24735_poll);
		if (ret)
			return ret;

		schedule_delayed_work(&charger->poll,
				      msecs_to_jiffies(charger->poll_interval));
	}

	return 0;
}

static const struct i2c_device_id bq24735_charger_id[] = {
	{ "bq24735-charger", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bq24735_charger_id);

static const struct of_device_id bq24735_match_ids[] = {
	{ .compatible = "ti,bq24735", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, bq24735_match_ids);

static struct i2c_driver bq24735_charger_driver = {
	.driver = {
		.name = "bq24735-charger",
		.of_match_table = bq24735_match_ids,
	},
	.probe = bq24735_charger_probe,
	.id_table = bq24735_charger_id,
};

module_i2c_driver(bq24735_charger_driver);

MODULE_DESCRIPTION("bq24735 battery charging driver");
MODULE_AUTHOR("Darbha Sriharsha <dsriharsha@nvidia.com>");
MODULE_LICENSE("GPL v2");
