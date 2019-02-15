/*
 * Maxim MAX14656 / AL32 USB Charger Detector driver
 *
 * Copyright (C) 2014 LG Electronics, Inc
 * Copyright (C) 2016 Alexander Kurz <akurz@blala.de>
 *
 * Components from Maxim AL32 Charger detection Driver for MX50 Yoshi Board
 * Copyright (C) Amazon Technologies Inc. All rights reserved.
 * Manish Lachwani (lachwani@lab126.com)
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>

#define MAX14656_MANUFACTURER	"Maxim Integrated"
#define MAX14656_NAME		"max14656"

#define MAX14656_DEVICE_ID	0x00
#define MAX14656_INTERRUPT_1	0x01
#define MAX14656_INTERRUPT_2	0x02
#define MAX14656_STATUS_1	0x03
#define MAX14656_STATUS_2	0x04
#define MAX14656_INTMASK_1	0x05
#define MAX14656_INTMASK_2	0x06
#define MAX14656_CONTROL_1	0x07
#define MAX14656_CONTROL_2	0x08
#define MAX14656_CONTROL_3	0x09

#define DEVICE_VENDOR_MASK	0xf0
#define DEVICE_REV_MASK		0x0f
#define INT_EN_REG_MASK		BIT(4)
#define CHG_TYPE_INT_MASK	BIT(0)
#define STATUS1_VB_VALID_MASK	BIT(4)
#define STATUS1_CHG_TYPE_MASK	0xf
#define INT1_DCD_TIMEOUT_MASK	BIT(7)
#define CONTROL1_DEFAULT	0x0d
#define CONTROL1_INT_EN		BIT(4)
#define CONTROL1_INT_ACTIVE_HIGH	BIT(5)
#define CONTROL1_EDGE		BIT(7)
#define CONTROL2_DEFAULT	0x8e
#define CONTROL2_ADC_EN		BIT(0)
#define CONTROL3_DEFAULT	0x8d

enum max14656_chg_type {
	MAX14656_NO_CHARGER	= 0,
	MAX14656_SDP_CHARGER,
	MAX14656_CDP_CHARGER,
	MAX14656_DCP_CHARGER,
	MAX14656_APPLE_500MA_CHARGER,
	MAX14656_APPLE_1A_CHARGER,
	MAX14656_APPLE_2A_CHARGER,
	MAX14656_SPECIAL_500MA_CHARGER,
	MAX14656_APPLE_12W,
	MAX14656_CHARGER_LAST
};

static const struct max14656_chg_type_props {
	enum power_supply_type type;
} chg_type_props[] = {
	{ POWER_SUPPLY_TYPE_UNKNOWN },
	{ POWER_SUPPLY_TYPE_USB },
	{ POWER_SUPPLY_TYPE_USB_CDP },
	{ POWER_SUPPLY_TYPE_USB_DCP },
	{ POWER_SUPPLY_TYPE_USB_DCP },
	{ POWER_SUPPLY_TYPE_USB_DCP },
	{ POWER_SUPPLY_TYPE_USB_DCP },
	{ POWER_SUPPLY_TYPE_USB_DCP },
	{ POWER_SUPPLY_TYPE_USB },
};

struct max14656_chip {
	struct i2c_client	*client;
	struct power_supply	*detect_psy;
	struct power_supply_desc psy_desc;
	struct delayed_work	irq_work;

	int irq;
	int online;
};

static int max14656_read_reg(struct i2c_client *client, int reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev,
			"i2c read fail: can't read from %02x: %d\n",
			reg, ret);
		return ret;
	}
	*val = ret;
	return 0;
}

static int max14656_write_reg(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int max14656_read_block_reg(struct i2c_client *client, u8 reg,
				  u8 length, u8 *val)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, length, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed to block read reg 0x%x: %d\n",
				reg, ret);
		return ret;
	}

	return 0;
}

#define        REG_TOTAL_NUM   5
static void max14656_irq_worker(struct work_struct *work)
{
	struct max14656_chip *chip =
		container_of(work, struct max14656_chip, irq_work.work);

	u8 buf[REG_TOTAL_NUM];
	u8 chg_type;
	int ret = 0;

	ret = max14656_read_block_reg(chip->client, MAX14656_DEVICE_ID,
				      REG_TOTAL_NUM, buf);

	if ((buf[MAX14656_STATUS_1] & STATUS1_VB_VALID_MASK) &&
		(buf[MAX14656_STATUS_1] & STATUS1_CHG_TYPE_MASK)) {
		chg_type = buf[MAX14656_STATUS_1] & STATUS1_CHG_TYPE_MASK;
		if (chg_type < MAX14656_CHARGER_LAST)
			chip->psy_desc.type = chg_type_props[chg_type].type;
		else
			chip->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->online = 1;
	} else {
		chip->online = 0;
		chip->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	power_supply_changed(chip->detect_psy);
}

static irqreturn_t max14656_irq(int irq, void *dev_id)
{
	struct max14656_chip *chip = dev_id;

	schedule_delayed_work(&chip->irq_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

static int max14656_hw_init(struct max14656_chip *chip)
{
	uint8_t val = 0;
	uint8_t rev;
	struct i2c_client *client = chip->client;

	if (max14656_read_reg(client, MAX14656_DEVICE_ID, &val))
		return -ENODEV;

	if ((val & DEVICE_VENDOR_MASK) != 0x20) {
		dev_err(&client->dev, "wrong vendor ID %d\n",
			((val & DEVICE_VENDOR_MASK) >> 4));
		return -ENODEV;
	}
	rev = val & DEVICE_REV_MASK;

	/* Turn on ADC_EN */
	if (max14656_write_reg(client, MAX14656_CONTROL_2, CONTROL2_ADC_EN))
		return -EINVAL;

	/* turn on interrupts and low power mode */
	if (max14656_write_reg(client, MAX14656_CONTROL_1,
		CONTROL1_DEFAULT |
		CONTROL1_INT_EN |
		CONTROL1_INT_ACTIVE_HIGH |
		CONTROL1_EDGE))
		return -EINVAL;

	if (max14656_write_reg(client, MAX14656_INTMASK_1, 0x3))
		return -EINVAL;

	if (max14656_write_reg(client, MAX14656_INTMASK_2, 0x1))
		return -EINVAL;

	dev_info(&client->dev, "detected revision %d\n", rev);
	return 0;
}

static int max14656_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max14656_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = MAX14656_NAME;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = MAX14656_MANUFACTURER;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property max14656_battery_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static int max14656_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct power_supply_config psy_cfg = {};
	struct max14656_chip *chip;
	int irq = client->irq;
	int ret = 0;

	if (irq <= 0) {
		dev_err(dev, "invalid irq number: %d\n", irq);
		return -ENODEV;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	psy_cfg.drv_data = chip;
	chip->client = client;
	chip->online = 0;
	chip->psy_desc.name = MAX14656_NAME;
	chip->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->psy_desc.properties = max14656_battery_props;
	chip->psy_desc.num_properties = ARRAY_SIZE(max14656_battery_props);
	chip->psy_desc.get_property = max14656_get_property;
	chip->irq = irq;

	ret = max14656_hw_init(chip);
	if (ret)
		return -ENODEV;

	INIT_DELAYED_WORK(&chip->irq_work, max14656_irq_worker);

	chip->detect_psy = devm_power_supply_register(dev,
		       &chip->psy_desc, &psy_cfg);
	if (IS_ERR(chip->detect_psy)) {
		dev_err(dev, "power_supply_register failed\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, chip->irq, max14656_irq,
			       IRQF_TRIGGER_FALLING,
			       MAX14656_NAME, chip);
	if (ret) {
		dev_err(dev, "request_irq %d failed\n", chip->irq);
		return -EINVAL;
	}
	enable_irq_wake(chip->irq);

	schedule_delayed_work(&chip->irq_work, msecs_to_jiffies(2000));

	return 0;
}

static const struct i2c_device_id max14656_id[] = {
	{ "max14656", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, max14656_id);

static const struct of_device_id max14656_match_table[] = {
	{ .compatible = "maxim,max14656", },
	{}
};
MODULE_DEVICE_TABLE(of, max14656_match_table);

static struct i2c_driver max14656_i2c_driver = {
	.driver = {
		.name	= "max14656",
		.of_match_table = max14656_match_table,
	},
	.probe		= max14656_probe,
	.id_table	= max14656_id,
};
module_i2c_driver(max14656_i2c_driver);

MODULE_DESCRIPTION("MAX14656 USB charger detector");
MODULE_LICENSE("GPL v2");
