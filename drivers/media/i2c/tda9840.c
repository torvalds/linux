// SPDX-License-Identifier: GPL-2.0-or-later
 /*
    tda9840 - i2c-driver for the tda9840 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>
    Copyright (C) 2008 Hans Verkuil <hverkuil@kernel.org>

    The tda9840 is a stereo/dual sound processor with digital
    identification. It can be found at address 0x84 on the i2c-bus.

    For detailed information download the specifications directly
    from SGS Thomson at http://www.st.com

  */


#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tda9840 driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define	SWITCH		0x00
#define	LEVEL_ADJUST	0x02
#define	STEREO_ADJUST	0x03
#define	TEST		0x04

#define TDA9840_SET_MUTE                0x00
#define TDA9840_SET_MONO                0x10
#define TDA9840_SET_STEREO              0x2a
#define TDA9840_SET_LANG1               0x12
#define TDA9840_SET_LANG2               0x1e
#define TDA9840_SET_BOTH                0x1a
#define TDA9840_SET_BOTH_R              0x16
#define TDA9840_SET_EXTERNAL            0x7a


static void tda9840_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (i2c_smbus_write_byte_data(client, reg, val))
		v4l2_dbg(1, debug, sd, "error writing %02x to %02x\n",
				val, reg);
}

static int tda9840_status(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int rc;
	u8 byte;

	rc = i2c_master_recv(client, &byte, 1);
	if (rc != 1) {
		v4l2_dbg(1, debug, sd,
			"i2c_master_recv() failed\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}

	if (byte & 0x80) {
		v4l2_dbg(1, debug, sd,
			"TDA9840_DETECT: register contents invalid\n");
		return -EINVAL;
	}

	v4l2_dbg(1, debug, sd, "TDA9840_DETECT: byte: 0x%02x\n", byte);
	return byte & 0x60;
}

static int tda9840_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *t)
{
	int stat = tda9840_status(sd);
	int byte;

	if (t->index)
		return -EINVAL;

	stat = stat < 0 ? 0 : stat;
	if (stat == 0 || stat == 0x60) /* mono input */
		byte = TDA9840_SET_MONO;
	else if (stat == 0x40) /* stereo input */
		byte = (t->audmode == V4L2_TUNER_MODE_MONO) ?
			TDA9840_SET_MONO : TDA9840_SET_STEREO;
	else { /* bilingual */
		switch (t->audmode) {
		case V4L2_TUNER_MODE_LANG1_LANG2:
			byte = TDA9840_SET_BOTH;
			break;
		case V4L2_TUNER_MODE_LANG2:
			byte = TDA9840_SET_LANG2;
			break;
		default:
			byte = TDA9840_SET_LANG1;
			break;
		}
	}
	v4l2_dbg(1, debug, sd, "TDA9840_SWITCH: 0x%02x\n", byte);
	tda9840_write(sd, SWITCH, byte);
	return 0;
}

static int tda9840_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *t)
{
	int stat = tda9840_status(sd);

	if (stat < 0)
		return stat;

	t->rxsubchans = V4L2_TUNER_SUB_MONO;

	switch (stat & 0x60) {
	case 0x00:
		t->rxsubchans = V4L2_TUNER_SUB_MONO;
		break;
	case 0x20:
		t->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		break;
	case 0x40:
		t->rxsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
		break;
	default: /* Incorrect detect */
		t->rxsubchans = V4L2_TUNER_MODE_MONO;
		break;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_tuner_ops tda9840_tuner_ops = {
	.s_tuner = tda9840_s_tuner,
	.g_tuner = tda9840_g_tuner,
};

static const struct v4l2_subdev_ops tda9840_ops = {
	.tuner = &tda9840_tuner_ops,
};

/* ----------------------------------------------------------------------- */

static int tda9840_probe(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	/* let's see whether this adapter can support what we need */
	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE_DATA |
			I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	sd = devm_kzalloc(&client->dev, sizeof(*sd), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &tda9840_ops);

	/* set initial values for level & stereo - adjustment, mode */
	tda9840_write(sd, LEVEL_ADJUST, 0);
	tda9840_write(sd, STEREO_ADJUST, 0);
	tda9840_write(sd, SWITCH, TDA9840_SET_STEREO);
	return 0;
}

static void tda9840_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

static const struct i2c_device_id tda9840_id[] = {
	{ "tda9840" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tda9840_id);

static struct i2c_driver tda9840_driver = {
	.driver = {
		.name	= "tda9840",
	},
	.probe		= tda9840_probe,
	.remove		= tda9840_remove,
	.id_table	= tda9840_id,
};

module_i2c_driver(tda9840_driver);
