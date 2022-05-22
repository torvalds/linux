// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PMIC driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 changhuang <changhuang.liang@starfivetech.com>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <soc/starfive/jh7110_pmic.h>

static struct pmic_dev *pmic_dev;

static int pmic_read_reg(struct pmic_dev *pmic_dev, u8 reg)
{
	struct i2c_client *client = pmic_dev->i2c_client;
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "Read Error\n");

	return ret;
}

static int pmic_write_reg(struct pmic_dev *pmic_dev, u8 reg, u8 val)
{
	struct i2c_client *client = pmic_dev->i2c_client;
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "Write Error\n");

	return ret;
}

static void pmic_set_bit(struct pmic_dev *pmic_dev, u8 reg, u8 mask, u8 val)
{
	u8 value;

	value = pmic_read_reg(pmic_dev, reg) & ~mask;
	val &= mask;
	val |= value;
	pmic_write_reg(pmic_dev, reg, val);
}

void pmic_set_domain(u8 reg, u8 domain, u8 on)
{
	pmic_set_bit(pmic_dev, reg, BIT(domain), on<<domain);
}
EXPORT_SYMBOL(pmic_set_domain);

static int pmic_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	pmic_dev = devm_kzalloc(dev, sizeof(*pmic_dev), GFP_KERNEL);
	if (!pmic_dev)
		return -ENOMEM;

	pmic_dev->i2c_client = client;

	dev_info(dev, "pmic init success!");

	return 0;
}

static int pmic_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id pmic_id[] = {
	{"pmic", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, pmic_id);

static const struct of_device_id pmic_dt_ids[] = {
	{ .compatible = "starfive,pmic" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pmic_dt_ids);

static struct i2c_driver pmic_i2c_driver = {
	.driver = {
		.name  = "pmic",
		.of_match_table	= pmic_dt_ids,
	},
	.id_table = pmic_id,
	.probe_new = pmic_probe,
	.remove   = pmic_remove,
};

static __init int pmic_init(void)
{
	return i2c_add_driver(&pmic_i2c_driver);
}

static __exit void pmic_exit(void)
{
	i2c_del_driver(&pmic_i2c_driver);
}

fs_initcall(pmic_init);
module_exit(pmic_exit);

MODULE_AUTHOR("changhuang <changhuang.liang@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 PMIC Device Driver");
MODULE_LICENSE("GPL v2");
