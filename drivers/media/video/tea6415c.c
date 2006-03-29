 /*
    tea6415c - i2c-driver for the tea6415c by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

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
#include <linux/i2c.h>

#include "tea6415c.h"

static int debug = 0;		/* insmod parameter */
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off device debugging (default:off).");
#define dprintk(args...) \
	    do { if (debug) { printk("%s: %s()[%d]: ", KBUILD_MODNAME, __FUNCTION__, __LINE__); printk(args); } } while (0)

#define TEA6415C_NUM_INPUTS	8
#define TEA6415C_NUM_OUTPUTS	6

/* addresses to scan, found only at 0x03 and/or 0x43 (7-bit) */
static unsigned short normal_i2c[] = { I2C_TEA6415C_1, I2C_TEA6415C_2, I2C_CLIENT_END };

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;
static struct i2c_client client_template;

/* this function is called by i2c_probe */
static int detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client = NULL;
	int err = 0;

	/* let's see whether this adapter can support what we need */
	if (0 == i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
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

	printk("tea6415c: detected @ 0x%02x on adapter %s\n", address, &client->adapter->name[0]);

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

/* makes a connection between the input-pin 'i' and the output-pin 'o'
   for the tea6415c-client 'client' */
static int switch_matrix(struct i2c_client *client, int i, int o)
{
	u8 byte = 0;
	int ret;

	dprintk("adr:0x%02x, i:%d, o:%d\n", client->addr, i, o);

	/* check if the pins are valid */
	if (0 == ((1 == i ||  3 == i ||  5 == i ||  6 == i ||  8 == i || 10 == i || 20 == i || 11 == i)
	      && (18 == o || 17 == o || 16 == o || 15 == o || 14 == o || 13 == o)))
		return -1;

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
		dprintk("i2c_smbus_write_byte() failed, ret:%d\n", ret);
		return -EIO;
	}

	return ret;
}

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tea6415c_multiplex *v = (struct tea6415c_multiplex *)arg;
	int result = 0;

	switch (cmd) {
	case TEA6415C_SWITCH:
		result = switch_matrix(client, v->in, v->out);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return result;
}

static struct i2c_driver driver = {
	.driver = {
		.name = "tea6415c",
	},
	.id 	= I2C_DRIVERID_TEA6415C,
	.attach_adapter	= attach,
	.detach_client	= detach,
	.command	= command,
};

static struct i2c_client client_template = {
	.name = "tea6415c",
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
MODULE_DESCRIPTION("tea6415c driver");
MODULE_LICENSE("GPL");
