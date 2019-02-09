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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("OmniVision ov7640 sensor driver");
MODULE_LICENSE("GPL v2");

static const u8 initial_registers[] = {
	0x12, 0x80,
	0x12, 0x54,
	0x14, 0x24,
	0x15, 0x01,
	0x28, 0x20,
	0x75, 0x82,
	0xFF, 0xFF, /* Terminator (reg 0xFF is unused) */
};

static int write_regs(struct i2c_client *client, const u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0xFF; i += 2)
		if (i2c_smbus_write_byte_data(client, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_ops ov7640_ops;

static int ov7640_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct v4l2_subdev *sd;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	sd = devm_kzalloc(&client->dev, sizeof(*sd), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &ov7640_ops);

	client->flags = I2C_CLIENT_SCCB;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	if (write_regs(client, initial_registers) < 0) {
		v4l_err(client, "error initializing OV7640\n");
		return -ENODEV;
	}

	return 0;
}


static int ov7640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);

	return 0;
}

static const struct i2c_device_id ov7640_id[] = {
	{ "ov7640", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov7640_id);

static struct i2c_driver ov7640_driver = {
	.driver = {
		.name	= "ov7640",
	},
	.probe = ov7640_probe,
	.remove = ov7640_remove,
	.id_table = ov7640_id,
};
module_i2c_driver(ov7640_driver);
