// SPDX-License-Identifier: GPL-2.0-or-later
/*
    bt866 - BT866 Digital Video Encoder (Rockwell Part)

    Copyright (C) 1999 Mike Bernson <mike@mlb.org>
    Copyright (C) 1998 Dave Perks <dperks@ibm.net>

    Modifications for LML33/DC10plus unified driver
    Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>

    This code was modify/ported from the saa7111 driver written
    by Dave Perks.

    This code was adapted for the bt866 by Christer Weinigel and ported
    to 2.6 by Martin Samuelsson.

*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>

MODULE_DESCRIPTION("Brooktree-866 video encoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");


/* ----------------------------------------------------------------------- */

struct bt866 {
	struct v4l2_subdev sd;
	u8 reg[256];
};

static inline struct bt866 *to_bt866(struct v4l2_subdev *sd)
{
	return container_of(sd, struct bt866, sd);
}

static int bt866_write(struct bt866 *encoder, u8 subaddr, u8 data)
{
	struct i2c_client *client = v4l2_get_subdevdata(&encoder->sd);
	u8 buffer[2];
	int err;

	buffer[0] = subaddr;
	buffer[1] = data;

	encoder->reg[subaddr] = data;

	v4l_dbg(1, debug, client, "write 0x%02x = 0x%02x\n", subaddr, data);

	for (err = 0; err < 3;) {
		if (i2c_master_send(client, buffer, 2) == 2)
			break;
		err++;
		v4l_warn(client, "error #%d writing to 0x%02x\n",
				err, subaddr);
		schedule_timeout_interruptible(msecs_to_jiffies(100));
	}
	if (err == 3) {
		v4l_warn(client, "giving up\n");
		return -1;
	}

	return 0;
}

static int bt866_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	v4l2_dbg(1, debug, sd, "set norm %llx\n", (unsigned long long)std);

	/* Only PAL supported by this driver at the moment! */
	if (!(std & V4L2_STD_NTSC))
		return -EINVAL;
	return 0;
}

static int bt866_s_routing(struct v4l2_subdev *sd,
			   u32 input, u32 output, u32 config)
{
	static const __u8 init[] = {
		0xc8, 0xcc, /* CRSCALE */
		0xca, 0x91, /* CBSCALE */
		0xcc, 0x24, /* YC16 | OSDNUM */
		0xda, 0x00, /*  */
		0xdc, 0x24, /* SETMODE | PAL */
		0xde, 0x02, /* EACTIVE */

		/* overlay colors */
		0x70, 0xEB, 0x90, 0x80, 0xB0, 0x80, /* white */
		0x72, 0xA2, 0x92, 0x8E, 0xB2, 0x2C, /* yellow */
		0x74, 0x83, 0x94, 0x2C, 0xB4, 0x9C, /* cyan */
		0x76, 0x70, 0x96, 0x3A, 0xB6, 0x48, /* green */
		0x78, 0x54, 0x98, 0xC6, 0xB8, 0xB8, /* magenta */
		0x7A, 0x41, 0x9A, 0xD4, 0xBA, 0x64, /* red */
		0x7C, 0x23, 0x9C, 0x72, 0xBC, 0xD4, /* blue */
		0x7E, 0x10, 0x9E, 0x80, 0xBE, 0x80, /* black */

		0x60, 0xEB, 0x80, 0x80, 0xc0, 0x80, /* white */
		0x62, 0xA2, 0x82, 0x8E, 0xc2, 0x2C, /* yellow */
		0x64, 0x83, 0x84, 0x2C, 0xc4, 0x9C, /* cyan */
		0x66, 0x70, 0x86, 0x3A, 0xc6, 0x48, /* green */
		0x68, 0x54, 0x88, 0xC6, 0xc8, 0xB8, /* magenta */
		0x6A, 0x41, 0x8A, 0xD4, 0xcA, 0x64, /* red */
		0x6C, 0x23, 0x8C, 0x72, 0xcC, 0xD4, /* blue */
		0x6E, 0x10, 0x8E, 0x80, 0xcE, 0x80, /* black */
	};
	struct bt866 *encoder = to_bt866(sd);
	u8 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(init) / 2; i += 2)
		bt866_write(encoder, init[i], init[i+1]);

	val = encoder->reg[0xdc];

	if (input == 0)
		val |= 0x40; /* CBSWAP */
	else
		val &= ~0x40; /* !CBSWAP */

	bt866_write(encoder, 0xdc, val);

	val = encoder->reg[0xcc];
	if (input == 2)
		val |= 0x01; /* OSDBAR */
	else
		val &= ~0x01; /* !OSDBAR */
	bt866_write(encoder, 0xcc, val);

	v4l2_dbg(1, debug, sd, "set input %d\n", input);

	switch (input) {
	case 0:
	case 1:
	case 2:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#if 0
/* Code to setup square pixels, might be of some use in the future,
   but is currently unused. */
	val = encoder->reg[0xdc];
	if (*iarg)
		val |= 1; /* SQUARE */
	else
		val &= ~1; /* !SQUARE */
	bt866_write(client, 0xdc, val);
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_video_ops bt866_video_ops = {
	.s_std_output = bt866_s_std_output,
	.s_routing = bt866_s_routing,
};

static const struct v4l2_subdev_ops bt866_ops = {
	.video = &bt866_video_ops,
};

static int bt866_probe(struct i2c_client *client)
{
	struct bt866 *encoder;
	struct v4l2_subdev *sd;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = devm_kzalloc(&client->dev, sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	sd = &encoder->sd;
	v4l2_i2c_subdev_init(sd, client, &bt866_ops);
	return 0;
}

static void bt866_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

static const struct i2c_device_id bt866_id[] = {
	{ "bt866" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bt866_id);

static struct i2c_driver bt866_driver = {
	.driver = {
		.name	= "bt866",
	},
	.probe		= bt866_probe,
	.remove		= bt866_remove,
	.id_table	= bt866_id,
};

module_i2c_driver(bt866_driver);
