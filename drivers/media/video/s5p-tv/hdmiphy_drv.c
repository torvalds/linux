/*
 * Samsung HDMI Physical interface driver
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co.Ltd
 * Author: Tomasz Stanislawski <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>

#include <media/v4l2-subdev.h>

MODULE_AUTHOR("Tomasz Stanislawski <t.stanislaws@samsung.com>");
MODULE_DESCRIPTION("Samsung HDMI Physical interface driver");
MODULE_LICENSE("GPL");

struct hdmiphy_conf {
	u32 preset;
	const u8 *data;
};

static const u8 hdmiphy_conf27[32] = {
	0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
	0x6B, 0x10, 0x02, 0x51, 0xDf, 0xF2, 0x54, 0x87,
	0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
	0x22, 0x40, 0xe3, 0x26, 0x00, 0x00, 0x00, 0x00,
};

static const u8 hdmiphy_conf74_175[32] = {
	0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xef, 0x5B,
	0x6D, 0x10, 0x01, 0x51, 0xef, 0xF3, 0x54, 0xb9,
	0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
	0x22, 0x40, 0xa5, 0x26, 0x01, 0x00, 0x00, 0x00,
};

static const u8 hdmiphy_conf74_25[32] = {
	0x01, 0x05, 0x00, 0xd8, 0x10, 0x9c, 0xf8, 0x40,
	0x6a, 0x10, 0x01, 0x51, 0xff, 0xf1, 0x54, 0xba,
	0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xe0,
	0x22, 0x40, 0xa4, 0x26, 0x01, 0x00, 0x00, 0x00,
};

static const u8 hdmiphy_conf148_5[32] = {
	0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf8, 0x40,
	0x6A, 0x18, 0x00, 0x51, 0xff, 0xF1, 0x54, 0xba,
	0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
	0x22, 0x40, 0xa4, 0x26, 0x02, 0x00, 0x00, 0x00,
};

static const struct hdmiphy_conf hdmiphy_conf[] = {
	{ V4L2_DV_480P59_94, hdmiphy_conf27 },
	{ V4L2_DV_1080P30, hdmiphy_conf74_175 },
	{ V4L2_DV_720P59_94, hdmiphy_conf74_175 },
	{ V4L2_DV_720P60, hdmiphy_conf74_25 },
	{ V4L2_DV_1080P50, hdmiphy_conf148_5 },
	{ V4L2_DV_1080P60, hdmiphy_conf148_5 },
};

const u8 *hdmiphy_preset2conf(u32 preset)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(hdmiphy_conf); ++i)
		if (hdmiphy_conf[i].preset == preset)
			return hdmiphy_conf[i].data;
	return NULL;
}

static int hdmiphy_s_power(struct v4l2_subdev *sd, int on)
{
	/* to be implemented */
	return 0;
}

static int hdmiphy_s_dv_preset(struct v4l2_subdev *sd,
	struct v4l2_dv_preset *preset)
{
	const u8 *data;
	u8 buffer[32];
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;

	dev_info(dev, "s_dv_preset(preset = %d)\n", preset->preset);
	data = hdmiphy_preset2conf(preset->preset);
	if (!data) {
		dev_err(dev, "format not supported\n");
		return -EINVAL;
	}

	/* storing configuration to the device */
	memcpy(buffer, data, 32);
	ret = i2c_master_send(client, buffer, 32);
	if (ret != 32) {
		dev_err(dev, "failed to configure HDMIPHY via I2C\n");
		return -EIO;
	}

	return 0;
}

static int hdmiphy_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	u8 buffer[2];
	int ret;

	dev_info(dev, "s_stream(%d)\n", enable);
	/* going to/from configuration from/to operation mode */
	buffer[0] = 0x1f;
	buffer[1] = enable ? 0x80 : 0x00;

	ret = i2c_master_send(client, buffer, 2);
	if (ret != 2) {
		dev_err(dev, "stream (%d) failed\n", enable);
		return -EIO;
	}
	return 0;
}

static const struct v4l2_subdev_core_ops hdmiphy_core_ops = {
	.s_power =  hdmiphy_s_power,
};

static const struct v4l2_subdev_video_ops hdmiphy_video_ops = {
	.s_dv_preset = hdmiphy_s_dv_preset,
	.s_stream =  hdmiphy_s_stream,
};

static const struct v4l2_subdev_ops hdmiphy_ops = {
	.core = &hdmiphy_core_ops,
	.video = &hdmiphy_video_ops,
};

static int __devinit hdmiphy_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	static struct v4l2_subdev sd;

	v4l2_i2c_subdev_init(&sd, client, &hdmiphy_ops);
	dev_info(&client->dev, "probe successful\n");
	return 0;
}

static int __devexit hdmiphy_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "remove successful\n");
	return 0;
}

static const struct i2c_device_id hdmiphy_id[] = {
	{ "hdmiphy", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, hdmiphy_id);

static struct i2c_driver hdmiphy_driver = {
	.driver = {
		.name	= "s5p-hdmiphy",
		.owner	= THIS_MODULE,
	},
	.probe		= hdmiphy_probe,
	.remove		= __devexit_p(hdmiphy_remove),
	.id_table = hdmiphy_id,
};

module_i2c_driver(hdmiphy_driver);
