/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>

#include "wis-i2c.h"

struct wis_ov7640 {
	int brightness;
	int contrast;
	int saturation;
	int hue;
};

static u8 initial_registers[] = {
	0x12, 0x80,
	0x12, 0x54,
	0x14, 0x24,
	0x15, 0x01,
	0x28, 0x20,
	0x75, 0x82,
	0xFF, 0xFF, /* Terminator (reg 0xFF is unused) */
};

static int write_regs(struct i2c_client *client, u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0xFF; i += 2)
		if (i2c_smbus_write_byte_data(client, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

static int wis_ov7640_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	client->flags = I2C_CLIENT_SCCB;

	dev_dbg(&client->dev,
		"wis-ov7640: initializing OV7640 at address %d on %s\n",
		client->addr, adapter->name);

	if (write_regs(client, initial_registers) < 0) {
		dev_err(&client->dev, "wis-ov7640: error initializing OV7640\n");
		return -ENODEV;
	}

	return 0;
}

static int wis_ov7640_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id wis_ov7640_id[] = {
	{ "wis_ov7640", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wis_ov7640_id);

static struct i2c_driver wis_ov7640_driver = {
	.driver = {
		.name	= "WIS OV7640 I2C driver",
	},
	.probe		= wis_ov7640_probe,
	.remove		= wis_ov7640_remove,
	.id_table	= wis_ov7640_id,
};

module_i2c_driver(wis_ov7640_driver);

MODULE_LICENSE("GPL v2");
