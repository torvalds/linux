// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Rockchip Co.,Ltd.
 * Author: Wang Jie <dave.wang@rock-chips.com>
 *
 * Hynetek Husb311 Type-C Chip Driver
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/usb/tcpm.h>
#include "tcpci.h"

#define HUSB311_VID		0x2E99
#define HUSB311_PID		0x0311
#define HUSB311_TCPC_I2C_RESET	0x9E
#define HUSB311_TCPC_SOFTRESET	0xA0
#define HUSB311_TCPC_FILTER	0xA1
#define HUSB311_TCPC_TDRP	0xA2
#define HUSB311_TCPC_DCSRCDRP	0xA3

struct husb311_chip {
	struct tcpci_data data;
	struct tcpci *tcpci;
	struct device *dev;
};

static int husb311_write8(struct husb311_chip *chip, unsigned int reg, u8 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u8));
}

static int husb311_write16(struct husb311_chip *chip, unsigned int reg, u16 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u16));
}

static const struct regmap_config husb311_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF, /* 0x80 .. 0xFF are vendor defined */
};

static struct husb311_chip *tdata_to_husb311(struct tcpci_data *tdata)
{
	return container_of(tdata, struct husb311_chip, data);
}

static int husb311_sw_reset(struct husb311_chip *chip)
{
	/* soft reset */
	return husb311_write8(chip, HUSB311_TCPC_SOFTRESET, 0x01);
}

static int husb311_init(struct tcpci *tcpci, struct tcpci_data *tdata)
{
	int ret;
	struct husb311_chip *chip = tdata_to_husb311(tdata);

	/* I2C reset : (val + 1) * 12.5ms */
	ret = husb311_write8(chip, HUSB311_TCPC_I2C_RESET, 0x8F);
	/* tTCPCfilter : (26.7 * val) us */
	ret |= husb311_write8(chip, HUSB311_TCPC_FILTER, 0x0F);
	/* tDRP : (51.2 + 6.4 * val) ms */
	ret |= husb311_write8(chip, HUSB311_TCPC_TDRP, 0x04);
	/* dcSRC.DRP : 33% */
	ret |= husb311_write16(chip, HUSB311_TCPC_DCSRCDRP, 330);

	if (ret < 0)
		dev_err(chip->dev, "fail to init registers(%d)\n", ret);

	return ret;
}

static irqreturn_t husb311_irq(int irq, void *dev_id)
{
	struct husb311_chip *chip = dev_id;

	return tcpci_irq(chip->tcpci);
}

static int husb311_check_revision(struct i2c_client *i2c)
{
	int ret;

	ret = i2c_smbus_read_word_data(i2c, TCPC_VENDOR_ID);
	if (ret < 0) {
		dev_err(&i2c->dev, "fail to read Vendor id(%d)\n", ret);
		return ret;
	}

	if (ret != HUSB311_VID) {
		dev_err(&i2c->dev, "vid is not correct, 0x%04x\n", ret);
		return -ENODEV;
	}

	ret = i2c_smbus_read_word_data(i2c, TCPC_PRODUCT_ID);
	if (ret < 0) {
		dev_err(&i2c->dev, "fail to read Product id(%d)\n", ret);
		return ret;
	}

	if (ret != HUSB311_PID) {
		dev_err(&i2c->dev, "pid is not correct, 0x%04x\n", ret);
		return -ENODEV;
	}

	return 0;
}

static int husb311_probe(struct i2c_client *client,
			 const struct i2c_device_id *i2c_id)
{
	int ret;
	struct husb311_chip *chip;

	ret = husb311_check_revision(client);
	if (ret < 0) {
		dev_err(&client->dev, "check vid/pid fail(%d)\n", ret);
		return ret;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->data.regmap = devm_regmap_init_i2c(client,
						 &husb311_regmap_config);
	if (IS_ERR(chip->data.regmap))
		return PTR_ERR(chip->data.regmap);

	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	ret = husb311_sw_reset(chip);
	if (ret < 0) {
		dev_err(chip->dev, "fail to soft reset, ret = %d\n", ret);
		return ret;
	}

	chip->data.init = husb311_init;
	chip->tcpci = tcpci_register_port(chip->dev, &chip->data);
	if (IS_ERR(chip->tcpci))
		return PTR_ERR(chip->tcpci);

	ret = devm_request_threaded_irq(chip->dev, client->irq, NULL,
					husb311_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					client->name, chip);
	if (ret < 0) {
		tcpci_unregister_port(chip->tcpci);
		return ret;
	}

	enable_irq_wake(client->irq);

	return 0;
}

static int husb311_remove(struct i2c_client *client)
{
	struct husb311_chip *chip = i2c_get_clientdata(client);

	tcpci_unregister_port(chip->tcpci);
	return 0;
}

static const struct i2c_device_id husb311_id[] = {
	{ "husb311", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, husb311_id);

#ifdef CONFIG_OF
static const struct of_device_id husb311_of_match[] = {
	{ .compatible = "hynetek,husb311" },
	{},
};
MODULE_DEVICE_TABLE(of, husb311_of_match);
#endif

static struct i2c_driver husb311_i2c_driver = {
	.driver = {
		.name = "husb311",
		.of_match_table = of_match_ptr(husb311_of_match),
	},
	.probe = husb311_probe,
	.remove = husb311_remove,
	.id_table = husb311_id,
};
module_i2c_driver(husb311_i2c_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("Husb311 USB Type-C Port Controller Interface Driver");
MODULE_LICENSE("GPL v2");
