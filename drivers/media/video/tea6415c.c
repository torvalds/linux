 /*
    tea6415c - i2c-driver for the tea6415c by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>
    Copyright (C) 2008 Hans Verkuil <hverkuil@xs4all.nl>

    The tea6415c is a bus controlled video-matrix-switch
    with 8 inputs and 6 outputs.
    It is cascadable, i.e. it can be found at the addresses
    0x86 and 0x06 on the i2c-bus.

    For detailed informations download the specifications directly
    from SGS Thomson at http://www.st.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License vs published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mvss Ave, Cambridge, MA 02139, USA.
  */


#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include "tea6415c.h"

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tea6415c driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


/* makes a connection between the input-pin 'i' and the output-pin 'o' */
static int tea6415c_s_routing(struct v4l2_subdev *sd,
			      u32 i, u32 o, u32 config)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 byte = 0;
	int ret;

	v4l2_dbg(1, debug, sd, "i=%d, o=%d\n", i, o);

	/* check if the pins are valid */
	if (0 == ((1 == i ||  3 == i ||  5 == i ||  6 == i ||  8 == i || 10 == i || 20 == i || 11 == i)
	      && (18 == o || 17 == o || 16 == o || 15 == o || 14 == o || 13 == o)))
		return -EINVAL;

	/* to understand this, have a look at the tea6415c-specs (p.5) */
	switch (o) {
	case 18:
		byte = 0x00;
		break;
	case 14:
		byte = 0x20;
		break;
	case 16:
		byte = 0x10;
		break;
	case 17:
		byte = 0x08;
		break;
	case 15:
		byte = 0x18;
		break;
	case 13:
		byte = 0x28;
		break;
	};

	switch (i) {
	case 5:
		byte |= 0x00;
		break;
	case 8:
		byte |= 0x04;
		break;
	case 3:
		byte |= 0x02;
		break;
	case 20:
		byte |= 0x06;
		break;
	case 6:
		byte |= 0x01;
		break;
	case 10:
		byte |= 0x05;
		break;
	case 1:
		byte |= 0x03;
		break;
	case 11:
		byte |= 0x07;
		break;
	};

	ret = i2c_smbus_write_byte(client, byte);
	if (ret) {
		v4l2_dbg(1, debug, sd,
			"i2c_smbus_write_byte() failed, ret:%d\n", ret);
		return -EIO;
	}
	return ret;
}

static int tea6415c_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_TEA6415C, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops tea6415c_core_ops = {
	.g_chip_ident = tea6415c_g_chip_ident,
};

static const struct v4l2_subdev_video_ops tea6415c_video_ops = {
	.s_routing = tea6415c_s_routing,
};

static const struct v4l2_subdev_ops tea6415c_ops = {
	.core = &tea6415c_core_ops,
	.video = &tea6415c_video_ops,
};

static int tea6415c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;

	/* let's see whether this adapter can support what we need */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	sd = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &tea6415c_ops);
	return 0;
}

static int tea6415c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	return 0;
}

static const struct i2c_device_id tea6415c_id[] = {
	{ "tea6415c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tea6415c_id);

static struct i2c_driver tea6415c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "tea6415c",
	},
	.probe		= tea6415c_probe,
	.remove		= tea6415c_remove,
	.id_table	= tea6415c_id,
};

static __init int init_tea6415c(void)
{
	return i2c_add_driver(&tea6415c_driver);
}

static __exit void exit_tea6415c(void)
{
	i2c_del_driver(&tea6415c_driver);
}

module_init(init_tea6415c);
module_exit(exit_tea6415c);
