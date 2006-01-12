 /*
    tea6420 - i2c-driver for the tea6420 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

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

#include "tea6420.h"

static int debug = 0;		/* insmod parameter */
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off device debugging (default:off).");
#define dprintk(args...) \
            do { if (debug) { printk("%s: %s()[%d]: ", KBUILD_MODNAME, __FUNCTION__, __LINE__); printk(args); } } while (0)

/* addresses to scan, found only at 0x4c and/or 0x4d (7-Bit) */
static unsigned short normal_i2c[] = { I2C_TEA6420_1, I2C_TEA6420_2, I2C_CLIENT_END };

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;
static struct i2c_client client_template;

/* make a connection between the input 'i' and the output 'o'
   with gain 'g' for the tea6420-client 'client' (note: i = 6 means 'mute') */
static int tea6420_switch(struct i2c_client *client, int i, int o, int g)
{
	u8 byte = 0;
	int ret;

	dprintk("adr:0x%02x, i:%d, o:%d, g:%d\n", client->addr, i, o, g);

	/* check if the paramters are valid */
	if (i < 1 || i > 6 || o < 1 || o > 4 || g < 0 || g > 6 || g % 2 != 0)
		return -1;

	byte  = ((o - 1) << 5);
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
		dprintk("i2c_smbus_write_byte() failed, ret:%d\n", ret);
		return -EIO;
	}
	
	return 0;
}

/* this function is called by i2c_probe */
static int tea6420_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	int err = 0, i = 0;

	/* let's see whether this adapter can support what we need */
	if (0 == i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (0 == client) {
		return -ENOMEM;
	}

	/* fill client structure */
	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;

	/* tell the i2c layer a new client has arrived */
	if (0 != (err = i2c_attach_client(client))) {
		kfree(client);
		return err;
	}

	/* set initial values: set "mute"-input to all outputs at gain 0 */
	err = 0;
	for (i = 1; i < 5; i++) {
		err += tea6420_switch(client, 6, i, 0);
	}
	if (err) {
		dprintk("could not initialize tea6420\n");
		kfree(client);
		return -ENODEV;
	}

	printk("tea6420: detected @ 0x%02x on adapter %s\n", address, &client->adapter->name[0]);

	return 0;
}

static int attach(struct i2c_adapter *adapter)
{
	/* let's see whether this is a know adapter we can attach to */
	if (adapter->id != I2C_HW_SAA7146) {
		dprintk("refusing to probe on unknown adapter [name='%s',id=0x%x]\n", adapter->name, adapter->id);
		return -ENODEV;
	}

	return i2c_probe(adapter, &addr_data, &tea6420_detect);
}

static int detach(struct i2c_client *client)
{
	int ret = i2c_detach_client(client);
	kfree(client);
	return ret;
}

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tea6420_multiplex *a = (struct tea6420_multiplex *)arg;
	int result = 0;

	switch (cmd) {
	case TEA6420_SWITCH:
		result = tea6420_switch(client, a->in, a->out, a->gain);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return result;
}

static struct i2c_driver driver = {
	.driver = {
		.name	= "tea6420",
	},
	.id	= I2C_DRIVERID_TEA6420,
	.attach_adapter	= attach,
	.detach_client	= detach,
	.command	= command,
};

static struct i2c_client client_template = {
	.name = "tea6420",
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
MODULE_DESCRIPTION("tea6420 driver");
MODULE_LICENSE("GPL");
