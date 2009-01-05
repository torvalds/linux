 /*
    tea6420 - i2c-driver for the tea6420 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>
    Copyright (C) 2008 Hans Verkuil <hverkuil@xs4all.nl>

    The tea6420 is a bus controlled audio-matrix with 5 stereo inputs,
    4 stereo outputs and gain control for each output.
    It is cascadable, i.e. it can be found at the adresses 0x98
    and 0x9a on the i2c-bus.

    For detailed informations download the specifications directly
    from SGS Thomson at http://www.st.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  */


#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-i2c-drv-legacy.h>
#include "tea6420.h"

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tea6420 driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* addresses to scan, found only at 0x4c and/or 0x4d (7-Bit) */
static unsigned short normal_i2c[] = { I2C_ADDR_TEA6420_1, I2C_ADDR_TEA6420_2, I2C_CLIENT_END };

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

/* make a connection between the input 'i' and the output 'o'
   with gain 'g' for the tea6420-client 'client' (note: i = 6 means 'mute') */
static int tea6420_switch(struct i2c_client *client, int i, int o, int g)
{
	u8 byte;
	int ret;

	v4l_dbg(1, debug, client, "i=%d, o=%d, g=%d\n", i, o, g);

	/* check if the parameters are valid */
	if (i < 1 || i > 6 || o < 1 || o > 4 || g < 0 || g > 6 || g % 2 != 0)
		return -1;

	byte = ((o - 1) << 5);
	byte |= (i - 1);

	/* to understand this, have a look at the tea6420-specs (p.5) */
	switch (g) {
	case 0:
		byte |= (3 << 3);
		break;
	case 2:
		byte |= (2 << 3);
		break;
	case 4:
		byte |= (1 << 3);
		break;
	case 6:
		break;
	}

	ret = i2c_smbus_write_byte(client, byte);
	if (ret) {
		v4l_dbg(1, debug, client,
			"i2c_smbus_write_byte() failed, ret:%d\n", ret);
		return -EIO;
	}
	return 0;
}

static long tea6420_ioctl(struct v4l2_subdev *sd, unsigned cmd, void *arg)
{
	if (cmd == TEA6420_SWITCH) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		struct tea6420_multiplex *a = (struct tea6420_multiplex *)arg;

		return tea6420_switch(client, a->in, a->out, a->gain);
	}
	return -ENOIOCTLCMD;
}

static int tea6420_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	return v4l2_subdev_command(i2c_get_clientdata(client), cmd, arg);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops tea6420_core_ops = {
	.ioctl = tea6420_ioctl,
};

static const struct v4l2_subdev_ops tea6420_ops = {
	.core = &tea6420_core_ops,
};

/* this function is called by i2c_probe */
static int tea6420_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	int err, i;

	/* let's see whether this adapter can support what we need */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	/* set initial values: set "mute"-input to all outputs at gain 0 */
	err = 0;
	for (i = 1; i < 5; i++) {
		err += tea6420_switch(client, 6, i, 0);
	}
	if (err) {
		v4l_dbg(1, debug, client, "could not initialize tea6420\n");
		return -ENODEV;
	}

	sd = kmalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;
	v4l2_i2c_subdev_init(sd, client, &tea6420_ops);
	return 0;
}

static int tea6420_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	return 0;
}

static int tea6420_legacy_probe(struct i2c_adapter *adapter)
{
	/* Let's see whether this is a known adapter we can attach to.
	   Prevents conflicts with tvaudio.c. */
	return adapter->id == I2C_HW_SAA7146;
}

static const struct i2c_device_id tea6420_id[] = {
	{ "tea6420", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tea6420_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "tea6420",
	.driverid = I2C_DRIVERID_TEA6420,
	.command = tea6420_command,
	.probe = tea6420_probe,
	.remove = tea6420_remove,
	.legacy_probe = tea6420_legacy_probe,
	.id_table = tea6420_id,
};
