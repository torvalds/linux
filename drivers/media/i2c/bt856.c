// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bt856 - BT856A Digital Video Encoder (Rockwell Part)
 *
 * Copyright (C) 1999 Mike Bernson <mike@mlb.org>
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * Modifications for LML33/DC10plus unified driver
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * This code was modify/ported from the saa7111 driver written
 * by Dave Perks.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *   - moved over to linux>=2.4.x i2c protocol (9/9/2002)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>

MODULE_DESCRIPTION("Brooktree-856A video encoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");


/* ----------------------------------------------------------------------- */

#define BT856_REG_OFFSET	0xDA
#define BT856_NR_REG		6

struct bt856 {
	struct v4l2_subdev sd;
	unsigned char reg[BT856_NR_REG];

	v4l2_std_id norm;
};

static inline struct bt856 *to_bt856(struct v4l2_subdev *sd)
{
	return container_of(sd, struct bt856, sd);
}

/* ----------------------------------------------------------------------- */

static inline int bt856_write(struct bt856 *encoder, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(&encoder->sd);

	encoder->reg[reg - BT856_REG_OFFSET] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int bt856_setbit(struct bt856 *encoder, u8 reg, u8 bit, u8 value)
{
	return bt856_write(encoder, reg,
		(encoder->reg[reg - BT856_REG_OFFSET] & ~(1 << bit)) |
				(value ? (1 << bit) : 0));
}

static void bt856_dump(struct bt856 *encoder)
{
	int i;

	v4l2_info(&encoder->sd, "register dump:\n");
	for (i = 0; i < BT856_NR_REG; i += 2)
		printk(KERN_CONT " %02x", encoder->reg[i]);
	printk(KERN_CONT "\n");
}

/* ----------------------------------------------------------------------- */

static int bt856_init(struct v4l2_subdev *sd, u32 arg)
{
	struct bt856 *encoder = to_bt856(sd);

	/* This is just for testing!!! */
	v4l2_dbg(1, debug, sd, "init\n");
	bt856_write(encoder, 0xdc, 0x18);
	bt856_write(encoder, 0xda, 0);
	bt856_write(encoder, 0xde, 0);

	bt856_setbit(encoder, 0xdc, 3, 1);
	/*bt856_setbit(encoder, 0xdc, 6, 0);*/
	bt856_setbit(encoder, 0xdc, 4, 1);

	if (encoder->norm & V4L2_STD_NTSC)
		bt856_setbit(encoder, 0xdc, 2, 0);
	else
		bt856_setbit(encoder, 0xdc, 2, 1);

	bt856_setbit(encoder, 0xdc, 1, 1);
	bt856_setbit(encoder, 0xde, 4, 0);
	bt856_setbit(encoder, 0xde, 3, 1);
	if (debug != 0)
		bt856_dump(encoder);
	return 0;
}

static int bt856_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct bt856 *encoder = to_bt856(sd);

	v4l2_dbg(1, debug, sd, "set norm %llx\n", (unsigned long long)std);

	if (std & V4L2_STD_NTSC) {
		bt856_setbit(encoder, 0xdc, 2, 0);
	} else if (std & V4L2_STD_PAL) {
		bt856_setbit(encoder, 0xdc, 2, 1);
		bt856_setbit(encoder, 0xda, 0, 0);
		/*bt856_setbit(encoder, 0xda, 0, 1);*/
	} else {
		return -EINVAL;
	}
	encoder->norm = std;
	if (debug != 0)
		bt856_dump(encoder);
	return 0;
}

static int bt856_s_routing(struct v4l2_subdev *sd,
			   u32 input, u32 output, u32 config)
{
	struct bt856 *encoder = to_bt856(sd);

	v4l2_dbg(1, debug, sd, "set input %d\n", input);

	/* We only have video bus.
	 * input= 0: input is from bt819
	 * input= 1: input is from ZR36060 */
	switch (input) {
	case 0:
		bt856_setbit(encoder, 0xde, 4, 0);
		bt856_setbit(encoder, 0xde, 3, 1);
		bt856_setbit(encoder, 0xdc, 3, 1);
		bt856_setbit(encoder, 0xdc, 6, 0);
		break;
	case 1:
		bt856_setbit(encoder, 0xde, 4, 0);
		bt856_setbit(encoder, 0xde, 3, 1);
		bt856_setbit(encoder, 0xdc, 3, 1);
		bt856_setbit(encoder, 0xdc, 6, 1);
		break;
	case 2:	/* Color bar */
		bt856_setbit(encoder, 0xdc, 3, 0);
		bt856_setbit(encoder, 0xde, 4, 1);
		break;
	default:
		return -EINVAL;
	}

	if (debug != 0)
		bt856_dump(encoder);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops bt856_core_ops = {
	.init = bt856_init,
};

static const struct v4l2_subdev_video_ops bt856_video_ops = {
	.s_std_output = bt856_s_std_output,
	.s_routing = bt856_s_routing,
};

static const struct v4l2_subdev_ops bt856_ops = {
	.core = &bt856_core_ops,
	.video = &bt856_video_ops,
};

/* ----------------------------------------------------------------------- */

static int bt856_probe(struct i2c_client *client)
{
	struct bt856 *encoder;
	struct v4l2_subdev *sd;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = devm_kzalloc(&client->dev, sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	sd = &encoder->sd;
	v4l2_i2c_subdev_init(sd, client, &bt856_ops);
	encoder->norm = V4L2_STD_NTSC;

	bt856_write(encoder, 0xdc, 0x18);
	bt856_write(encoder, 0xda, 0);
	bt856_write(encoder, 0xde, 0);

	bt856_setbit(encoder, 0xdc, 3, 1);
	/*bt856_setbit(encoder, 0xdc, 6, 0);*/
	bt856_setbit(encoder, 0xdc, 4, 1);

	if (encoder->norm & V4L2_STD_NTSC)
		bt856_setbit(encoder, 0xdc, 2, 0);
	else
		bt856_setbit(encoder, 0xdc, 2, 1);

	bt856_setbit(encoder, 0xdc, 1, 1);
	bt856_setbit(encoder, 0xde, 4, 0);
	bt856_setbit(encoder, 0xde, 3, 1);

	if (debug != 0)
		bt856_dump(encoder);
	return 0;
}

static void bt856_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

static const struct i2c_device_id bt856_id[] = {
	{ "bt856", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bt856_id);

static struct i2c_driver bt856_driver = {
	.driver = {
		.name	= "bt856",
	},
	.probe_new	= bt856_probe,
	.remove		= bt856_remove,
	.id_table	= bt856_id,
};

module_i2c_driver(bt856_driver);
