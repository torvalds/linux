// SPDX-License-Identifier: GPL-2.0
/*
 * I2C driver for Bosch BMI323 6-Axis IMU.
 *
 * Copyright (C) 2023, Jagath Jog J <jagathjog1996@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bmi323.h"

struct bmi323_i2c_priv {
	struct i2c_client *i2c;
	u8 i2c_rx_buffer[BMI323_FIFO_LENGTH_IN_BYTES + BMI323_I2C_DUMMY];
};

/*
 * From BMI323 datasheet section 4: Notes on the Serial Interface Support.
 * Each I2C register read operation requires to read two dummy bytes before
 * the actual payload.
 */
static int bmi323_regmap_i2c_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	struct bmi323_i2c_priv *priv = context;
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].addr = priv->i2c->addr;
	msgs[0].flags = priv->i2c->flags;
	msgs[0].len = reg_size;
	msgs[0].buf = (u8 *)reg_buf;

	msgs[1].addr = priv->i2c->addr;
	msgs[1].len = val_size + BMI323_I2C_DUMMY;
	msgs[1].buf = priv->i2c_rx_buffer;
	msgs[1].flags = priv->i2c->flags | I2C_M_RD;

	ret = i2c_transfer(priv->i2c->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return -EIO;

	memcpy(val_buf, priv->i2c_rx_buffer + BMI323_I2C_DUMMY, val_size);

	return 0;
}

static int bmi323_regmap_i2c_write(void *context, const void *data,
				   size_t count)
{
	struct bmi323_i2c_priv *priv = context;
	u8 reg;

	reg = *(u8 *)data;
	return i2c_smbus_write_i2c_block_data(priv->i2c, reg,
					      count - sizeof(u8),
					      data + sizeof(u8));
}

static struct regmap_bus bmi323_regmap_bus = {
	.read = bmi323_regmap_i2c_read,
	.write = bmi323_regmap_i2c_write,
};

static const struct regmap_config bmi323_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = BMI323_CFG_RES_REG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int bmi323_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct bmi323_i2c_priv *priv;
	struct regmap *regmap;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->i2c = i2c;
	regmap = devm_regmap_init(dev, &bmi323_regmap_bus, priv,
				  &bmi323_i2c_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize I2C Regmap\n");

	return bmi323_core_probe(dev);
}

static const struct i2c_device_id bmi323_i2c_ids[] = {
	{ "bmi323" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bmi323_i2c_ids);

static const struct of_device_id bmi323_of_i2c_match[] = {
	{ .compatible = "bosch,bmi323" },
	{ }
};
MODULE_DEVICE_TABLE(of, bmi323_of_i2c_match);

static struct i2c_driver bmi323_i2c_driver = {
	.driver = {
		.name = "bmi323",
		.of_match_table = bmi323_of_i2c_match,
	},
	.probe = bmi323_i2c_probe,
	.id_table = bmi323_i2c_ids,
};
module_i2c_driver(bmi323_i2c_driver);

MODULE_DESCRIPTION("Bosch BMI323 IMU driver");
MODULE_AUTHOR("Jagath Jog J <jagathjog1996@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_BMI323);
