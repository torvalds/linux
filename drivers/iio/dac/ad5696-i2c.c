// SPDX-License-Identifier: GPL-2.0+
/*
 * AD5671R, AD5675R, AD5691R, AD5692R, AD5693, AD5693R,
 * AD5694, AD5694R, AD5695R, AD5696, AD5696R
 * Digital to analog converters driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include "ad5686.h"

#include <linux/module.h>
#include <linux/i2c.h>

static int ad5686_i2c_read(struct ad5686_state *st, u8 addr)
{
	struct i2c_client *i2c = to_i2c_client(st->dev);
	struct i2c_msg msg[2] = {
		{
			.addr = i2c->addr,
			.flags = i2c->flags,
			.len = 3,
			.buf = &st->data[0].d8[1],
		},
		{
			.addr = i2c->addr,
			.flags = i2c->flags | I2C_M_RD,
			.len = 2,
			.buf = (char *)&st->data[0].d16,
		},
	};
	int ret;

	st->data[0].d32 = cpu_to_be32(AD5686_CMD(AD5686_CMD_NOOP) |
				      AD5686_ADDR(addr) |
				      0x00);

	ret = i2c_transfer(i2c->adapter, msg, 2);
	if (ret < 0)
		return ret;

	return be16_to_cpu(st->data[0].d16);
}

static int ad5686_i2c_write(struct ad5686_state *st,
			    u8 cmd, u8 addr, u16 val)
{
	struct i2c_client *i2c = to_i2c_client(st->dev);
	int ret;

	st->data[0].d32 = cpu_to_be32(AD5686_CMD(cmd) | AD5686_ADDR(addr)
				      | val);

	ret = i2c_master_send(i2c, &st->data[0].d8[1], 3);
	if (ret < 0)
		return ret;

	return (ret != 3) ? -EIO : 0;
}

static int ad5686_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	return ad5686_probe(&i2c->dev, id->driver_data, id->name,
			    ad5686_i2c_write, ad5686_i2c_read);
}

static int ad5686_i2c_remove(struct i2c_client *i2c)
{
	return ad5686_remove(&i2c->dev);
}

static const struct i2c_device_id ad5686_i2c_id[] = {
	{"ad5671r", ID_AD5671R},
	{"ad5675r", ID_AD5675R},
	{"ad5691r", ID_AD5691R},
	{"ad5692r", ID_AD5692R},
	{"ad5693", ID_AD5693},
	{"ad5693r", ID_AD5693R},
	{"ad5694", ID_AD5694},
	{"ad5694r", ID_AD5694R},
	{"ad5695r", ID_AD5695R},
	{"ad5696", ID_AD5696},
	{"ad5696r", ID_AD5696R},
	{}
};
MODULE_DEVICE_TABLE(i2c, ad5686_i2c_id);

static struct i2c_driver ad5686_i2c_driver = {
	.driver = {
		.name = "ad5696",
	},
	.probe = ad5686_i2c_probe,
	.remove = ad5686_i2c_remove,
	.id_table = ad5686_i2c_id,
};

module_i2c_driver(ad5686_i2c_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5686 and similar multi-channel DACs");
MODULE_LICENSE("GPL v2");
