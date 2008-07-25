 /*
    tda9840 - i2c-driver for the tda9840 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>
    Copyright (C) 2008 Hans Verkuil <hverkuil@xs4all.nl>

    The tda9840 is a stereo/dual sound processor with digital
    identification. It can be found at address 0x84 on the i2c-bus.

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
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>
#include "tda9840.h"

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

/* addresses to scan, found only at 0x42 (7-Bit) */
static unsigned short normal_i2c[] = { I2C_ADDR_TDA9840, I2C_CLIENT_END };

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static void tda9840_write(struct i2c_client *client, u8 reg, u8 val)
{
	if (i2c_smbus_write_byte_data(client, reg, val))
		v4l_dbg(1, debug, client, "error writing %02x to %02x\n",
				val, reg);
}

static int tda9840_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	int result;
	int byte = *(int *)arg;

	switch (cmd) {
	case TDA9840_SWITCH:
		v4l_dbg(1, debug, client, "TDA9840_SWITCH: 0x%02x\n", byte);

		if (byte != TDA9840_SET_MONO
		    && byte != TDA9840_SET_MUTE
		    && byte != TDA9840_SET_STEREO
		    && byte != TDA9840_SET_LANG1
		    && byte != TDA9840_SET_LANG2
		    && byte != TDA9840_SET_BOTH
		    && byte != TDA9840_SET_BOTH_R
		    && byte != TDA9840_SET_EXTERNAL) {
			return -EINVAL;
		}

		tda9840_write(client, SWITCH, byte);
		break;

	case TDA9840_LEVEL_ADJUST:
		v4l_dbg(1, debug, client, "TDA9840_LEVEL_ADJUST: %d\n", byte);

		/* check for correct range */
		if (byte > 25 || byte < -20)
			return -EINVAL;

		/* calculate actual value to set, see specs, page 18 */
		byte /= 5;
		if (0 < byte)
			byte += 0x8;
		else
			byte = -byte;
		tda9840_write(client, LEVEL_ADJUST, byte);
		break;

	case TDA9840_STEREO_ADJUST:
		v4l_dbg(1, debug, client, "TDA9840_STEREO_ADJUST: %d\n", byte);

		/* check for correct range */
		if (byte > 25 || byte < -24)
			return -EINVAL;

		/* calculate actual value to set */
		byte /= 5;
		if (0 < byte)
			byte += 0x20;
		else
			byte = -byte;

		tda9840_write(client, STEREO_ADJUST, byte);
		break;

	case TDA9840_DETECT: {
		int *ret = (int *)arg;

		byte = i2c_smbus_read_byte_data(client, STEREO_ADJUST);
		if (byte == -1) {
			v4l_dbg(1, debug, client,
				"i2c_smbus_read_byte_data() failed\n");
			return -EIO;
		}

		if (byte & 0x80) {
			v4l_dbg(1, debug, client,
				"TDA9840_DETECT: register contents invalid\n");
			return -EINVAL;
		}

		v4l_dbg(1, debug, client, "TDA9840_DETECT: byte: 0x%02x\n", byte);
		*ret = (byte & 0x60) >> 5;
		result = 0;
		break;
	}
	case TDA9840_TEST:
		v4l_dbg(1, debug, client, "TDA9840_TEST: 0x%02x\n", byte);

		/* mask out irrelevant bits */
		byte &= 0x3;

		tda9840_write(client, TEST, byte);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (result)
		return -EIO;

	return 0;
}

static int tda9840_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int result;
	int byte;

	/* let's see whether this adapter can support what we need */
	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE_DATA |
			I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return 0;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	/* set initial values for level & stereo - adjustment, mode */
	byte = 0;
	result = tda9840_command(client, TDA9840_LEVEL_ADJUST, &byte);
	result += tda9840_command(client, TDA9840_STEREO_ADJUST, &byte);
	byte = TDA9840_SET_MONO;
	result = tda9840_command(client, TDA9840_SWITCH, &byte);
	if (result) {
		v4l_dbg(1, debug, client, "could not initialize tda9840\n");
		return -ENODEV;
	}
	return 0;
}

static int tda9840_legacy_probe(struct i2c_adapter *adapter)
{
	/* Let's see whether this is a known adapter we can attach to.
	   Prevents conflicts with tvaudio.c. */
	return adapter->id == I2C_HW_SAA7146;
}
static const struct i2c_device_id tda9840_id[] = {
	{ "tda9840", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tda9840_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "tda9840",
	.driverid = I2C_DRIVERID_TDA9840,
	.command = tda9840_command,
	.probe = tda9840_probe,
	.legacy_probe = tda9840_legacy_probe,
	.id_table = tda9840_id,
};
