 /*
    tda9840 - i2c-driver for the tda9840 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

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

#include "tda9840.h"

static int debug = 0;		/* insmod parameter */
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off device debugging (default:off).");
#define dprintk(args...) \
            do { if (debug) { printk("%s: %s()[%d]: ", KBUILD_MODNAME, __FUNCTION__, __LINE__); printk(args); } } while (0)

#define	SWITCH		0x00
#define	LEVEL_ADJUST	0x02
#define	STEREO_ADJUST	0x03
#define	TEST		0x04

/* addresses to scan, found only at 0x42 (7-Bit) */
static unsigned short normal_i2c[] = { I2C_TDA9840, I2C_CLIENT_END };

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;
static struct i2c_client client_template;

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	int result;
	int byte = *(int *)arg;

	switch (cmd) {
	case TDA9840_SWITCH:

		dprintk("TDA9840_SWITCH: 0x%02x\n", byte);

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

		result = i2c_smbus_write_byte_data(client, SWITCH, byte);
		if (result)
			dprintk("i2c_smbus_write_byte() failed, ret:%d\n", result);
		break;

	case TDA9840_LEVEL_ADJUST:

		dprintk("TDA9840_LEVEL_ADJUST: %d\n", byte);

		/* check for correct range */
		if (byte > 25 || byte < -20)
			return -EINVAL;

		/* calculate actual value to set, see specs, page 18 */
		byte /= 5;
		if (0 < byte)
			byte += 0x8;
		else
			byte = -byte;

		result = i2c_smbus_write_byte_data(client, LEVEL_ADJUST, byte);
		if (result)
			dprintk("i2c_smbus_write_byte() failed, ret:%d\n", result);
		break;

	case TDA9840_STEREO_ADJUST:

		dprintk("TDA9840_STEREO_ADJUST: %d\n", byte);

		/* check for correct range */
		if (byte > 25 || byte < -24)
			return -EINVAL;

		/* calculate actual value to set */
		byte /= 5;
		if (0 < byte)
			byte += 0x20;
		else
			byte = -byte;

		result = i2c_smbus_write_byte_data(client, STEREO_ADJUST, byte);
		if (result)
			dprintk("i2c_smbus_write_byte() failed, ret:%d\n", result);
		break;

	case TDA9840_DETECT: {
		int *ret = (int *)arg;

		byte = i2c_smbus_read_byte_data(client, STEREO_ADJUST);
		if (byte == -1) {
			dprintk("i2c_smbus_read_byte_data() failed\n");
			return -EIO;
		}

		if (0 != (byte & 0x80)) {
			dprintk("TDA9840_DETECT: register contents invalid\n");
			return -EINVAL;
		}

		dprintk("TDA9840_DETECT: byte: 0x%02x\n", byte);
		*ret = ((byte & 0x60) >> 5);
		result = 0;
		break;
	}
	case TDA9840_TEST:
		dprintk("TDA9840_TEST: 0x%02x\n", byte);

		/* mask out irrelevant bits */
		byte &= 0x3;

		result = i2c_smbus_write_byte_data(client, TEST, byte);
		if (result)
			dprintk("i2c_smbus_write_byte() failed, ret:%d\n", result);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (result)
		return -EIO;

	return 0;
}

static int detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	int result = 0;

	int byte = 0x0;

	/* let's see whether this adapter can support what we need */
	if (0 == i2c_check_functionality(adapter,
				    I2C_FUNC_SMBUS_READ_BYTE_DATA |
				    I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (0 == client) {
		printk("not enough kernel memory\n");
		return -ENOMEM;
	}

	/* fill client structure */
	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;

	/* tell the i2c layer a new client has arrived */
	if (0 != (result = i2c_attach_client(client))) {
		kfree(client);
		return result;
	}

	/* set initial values for level & stereo - adjustment, mode */
	byte = 0;
	result  = command(client, TDA9840_LEVEL_ADJUST, &byte);
	result += command(client, TDA9840_STEREO_ADJUST, &byte);
	byte = TDA9840_SET_MONO;
	result = command(client, TDA9840_SWITCH, &byte);
	if (result) {
		dprintk("could not initialize tda9840\n");
		return -ENODEV;
	}

	printk("tda9840: detected @ 0x%02x on adapter %s\n", address, &client->adapter->name[0]);
	return 0;
}

static int attach(struct i2c_adapter *adapter)
{
	/* let's see whether this is a know adapter we can attach to */
	if (adapter->id != I2C_HW_SAA7146) {
		dprintk("refusing to probe on unknown adapter [name='%s',id=0x%x]\n", adapter->name, adapter->id);
		return -ENODEV;
	}

	return i2c_probe(adapter, &addr_data, &detect);
}

static int detach(struct i2c_client *client)
{
	int ret = i2c_detach_client(client);
	kfree(client);
	return ret;
}

static struct i2c_driver driver = {
	.driver = {
		.name	= "tda9840",
	},
	.id	= I2C_DRIVERID_TDA9840,
	.attach_adapter	= attach,
	.detach_client	= detach,
	.command	= command,
};

static struct i2c_client client_template = {
	.name = "tda9840",
	.driver = &driver,
};

static int __init this_module_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit this_module_exit(void)
{
	i2c_del_driver(&driver);
}

module_init(this_module_init);
module_exit(this_module_exit);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tda9840 driver");
MODULE_LICENSE("GPL");
