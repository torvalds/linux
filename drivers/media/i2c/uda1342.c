// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/i2c/uda1342.h>
#include <linux/slab.h>

static int write_reg(struct i2c_client *client, int reg, int value)
{
	/* UDA1342 wants MSB first, but SMBus sends LSB first */
	i2c_smbus_write_word_data(client, reg, swab16(value));
	return 0;
}

static int uda1342_s_routing(struct v4l2_subdev *sd,
		u32 input, u32 output, u32 config)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (input) {
	case UDA1342_IN1:
		write_reg(client, 0x00, 0x1241); /* select input 1 */
		break;
	case UDA1342_IN2:
		write_reg(client, 0x00, 0x1441); /* select input 2 */
		break;
	default:
		v4l2_err(sd, "input %d not supported\n", input);
		break;
	}
	return 0;
}

static const struct v4l2_subdev_audio_ops uda1342_audio_ops = {
	.s_routing = uda1342_s_routing,
};

static const struct v4l2_subdev_ops uda1342_ops = {
	.audio = &uda1342_audio_ops,
};

static int uda1342_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct v4l2_subdev *sd;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	dev_dbg(&client->dev, "initializing UDA1342 at address %d on %s\n",
		client->addr, adapter->name);

	sd = devm_kzalloc(&client->dev, sizeof(*sd), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;

	v4l2_i2c_subdev_init(sd, client, &uda1342_ops);

	write_reg(client, 0x00, 0x8000); /* reset registers */
	write_reg(client, 0x00, 0x1241); /* select input 1 */

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	return 0;
}

static void uda1342_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

static const struct i2c_device_id uda1342_id[] = {
	{ "uda1342", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, uda1342_id);

static struct i2c_driver uda1342_driver = {
	.driver = {
		.name	= "uda1342",
	},
	.probe_new	= uda1342_probe,
	.remove		= uda1342_remove,
	.id_table	= uda1342_id,
};

module_i2c_driver(uda1342_driver);

MODULE_LICENSE("GPL v2");
