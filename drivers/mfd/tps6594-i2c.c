// SPDX-License-Identifier: GPL-2.0
/*
 * I2C access driver for TI TPS6594/TPS6593/LP8764 PMICs
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#include <linux/crc8.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <linux/mfd/tps6594.h>

static bool enable_crc;
module_param(enable_crc, bool, 0444);
MODULE_PARM_DESC(enable_crc, "Enable CRC feature for I2C interface");

DECLARE_CRC8_TABLE(tps6594_i2c_crc_table);

static int tps6594_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int ret = i2c_transfer(adap, msgs, num);

	if (ret == num)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int tps6594_i2c_reg_read_with_crc(struct i2c_client *client, u8 page, u8 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 buf_rx[] = { 0, 0 };
	/* I2C address = I2C base address + Page index */
	const u8 addr = client->addr + page;
	/*
	 * CRC is calculated from every bit included in the protocol
	 * except the ACK bits from the target. Byte stream is:
	 * - B0: (I2C_addr_7bits << 1) | WR_bit, with WR_bit = 0
	 * - B1: reg
	 * - B2: (I2C_addr_7bits << 1) | RD_bit, with RD_bit = 1
	 * - B3: val
	 * - B4: CRC from B0-B1-B2-B3
	 */
	u8 crc_data[] = { addr << 1, reg, addr << 1 | 1, 0 };
	int ret;

	/* Write register */
	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	/* Read data and CRC */
	msgs[1].addr = msgs[0].addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].buf = buf_rx;

	ret = tps6594_i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		return ret;

	crc_data[sizeof(crc_data) - 1] = *val = buf_rx[0];
	if (buf_rx[1] != crc8(tps6594_i2c_crc_table, crc_data, sizeof(crc_data), CRC8_INIT_VALUE))
		return -EIO;

	return ret;
}

static int tps6594_i2c_reg_write_with_crc(struct i2c_client *client, u8 page, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[] = { reg, val, 0 };
	/* I2C address = I2C base address + Page index */
	const u8 addr = client->addr + page;
	/*
	 * CRC is calculated from every bit included in the protocol
	 * except the ACK bits from the target. Byte stream is:
	 * - B0: (I2C_addr_7bits << 1) | WR_bit, with WR_bit = 0
	 * - B1: reg
	 * - B2: val
	 * - B3: CRC from B0-B1-B2
	 */
	const u8 crc_data[] = { addr << 1, reg, val };

	/* Write register, data and CRC */
	msg.addr = addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = sizeof(buf);
	msg.buf = buf;

	buf[msg.len - 1] = crc8(tps6594_i2c_crc_table, crc_data, sizeof(crc_data), CRC8_INIT_VALUE);

	return tps6594_i2c_transfer(client->adapter, &msg, 1);
}

static int tps6594_i2c_read(void *context, const void *reg_buf, size_t reg_size,
			    void *val_buf, size_t val_size)
{
	struct i2c_client *client = context;
	struct tps6594 *tps = i2c_get_clientdata(client);
	struct i2c_msg msgs[2];
	const u8 *reg_bytes = reg_buf;
	u8 *val_bytes = val_buf;
	const u8 page = reg_bytes[1];
	u8 reg = reg_bytes[0];
	int ret = 0;
	int i;

	if (tps->use_crc) {
		/*
		 * Auto-increment feature does not support CRC protocol.
		 * Converts the bulk read operation into a series of single read operations.
		 */
		for (i = 0 ; ret == 0 && i < val_size ; i++)
			ret = tps6594_i2c_reg_read_with_crc(client, page, reg + i, val_bytes + i);

		return ret;
	}

	/* Write register: I2C address = I2C base address + Page index */
	msgs[0].addr = client->addr + page;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	/* Read data */
	msgs[1].addr = msgs[0].addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = val_size;
	msgs[1].buf = val_bytes;

	return tps6594_i2c_transfer(client->adapter, msgs, 2);
}

static int tps6594_i2c_write(void *context, const void *data, size_t count)
{
	struct i2c_client *client = context;
	struct tps6594 *tps = i2c_get_clientdata(client);
	struct i2c_msg msg;
	const u8 *bytes = data;
	u8 *buf;
	const u8 page = bytes[1];
	const u8 reg = bytes[0];
	int ret = 0;
	int i;

	if (tps->use_crc) {
		/*
		 * Auto-increment feature does not support CRC protocol.
		 * Converts the bulk write operation into a series of single write operations.
		 */
		for (i = 0 ; ret == 0 && i < count - 2 ; i++)
			ret = tps6594_i2c_reg_write_with_crc(client, page, reg + i, bytes[i + 2]);

		return ret;
	}

	/* Setup buffer: page byte is not sent */
	buf = kzalloc(--count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = reg;
	for (i = 0 ; i < count - 1 ; i++)
		buf[i + 1] = bytes[i + 2];

	/* Write register and data: I2C address = I2C base address + Page index */
	msg.addr = client->addr + page;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = buf;

	ret = tps6594_i2c_transfer(client->adapter, &msg, 1);

	kfree(buf);
	return ret;
}

static const struct regmap_config tps6594_i2c_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = TPS6594_REG_DWD_FAIL_CNT_REG,
	.volatile_reg = tps6594_is_volatile_reg,
	.read = tps6594_i2c_read,
	.write = tps6594_i2c_write,
};

static const struct of_device_id tps6594_i2c_of_match_table[] = {
	{ .compatible = "ti,tps6594-q1", .data = (void *)TPS6594, },
	{ .compatible = "ti,tps6593-q1", .data = (void *)TPS6593, },
	{ .compatible = "ti,lp8764-q1",  .data = (void *)LP8764,  },
	{}
};
MODULE_DEVICE_TABLE(of, tps6594_i2c_of_match_table);

static int tps6594_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tps6594 *tps;
	const struct of_device_id *match;

	tps = devm_kzalloc(dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	i2c_set_clientdata(client, tps);

	tps->dev = dev;
	tps->reg = client->addr;
	tps->irq = client->irq;

	tps->regmap = devm_regmap_init(dev, NULL, client, &tps6594_i2c_regmap_config);
	if (IS_ERR(tps->regmap))
		return dev_err_probe(dev, PTR_ERR(tps->regmap), "Failed to init regmap\n");

	match = of_match_device(tps6594_i2c_of_match_table, dev);
	if (!match)
		return dev_err_probe(dev, -EINVAL, "Failed to find matching chip ID\n");
	tps->chip_id = (unsigned long)match->data;

	crc8_populate_msb(tps6594_i2c_crc_table, TPS6594_CRC8_POLYNOMIAL);

	return tps6594_device_init(tps, enable_crc);
}

static struct i2c_driver tps6594_i2c_driver = {
	.driver	= {
		.name = "tps6594",
		.of_match_table = tps6594_i2c_of_match_table,
	},
	.probe = tps6594_i2c_probe,
};
module_i2c_driver(tps6594_i2c_driver);

MODULE_AUTHOR("Julien Panis <jpanis@baylibre.com>");
MODULE_DESCRIPTION("TPS6594 I2C Interface Driver");
MODULE_LICENSE("GPL");
