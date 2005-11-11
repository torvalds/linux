/*
 * cs53l32a (Adaptec AVC-2010 and AVC-2410) i2c ivtv driver.
 * Copyright (C) 2005  Martin Vaughan
 *
 * Audio source switching for Adaptec AVC-2410 added by Trev Jackson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev.h>
#include <media/audiochip.h>

MODULE_DESCRIPTION("i2c device driver for cs53l32a Audio ADC");
MODULE_AUTHOR("Martin Vaughan");
MODULE_LICENSE("GPL");

static int debug = 0;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages\n\t\t\t0=Off (default), 1=On");

#define cs53l32a_dbg(fmt, arg...) \
	do { \
		if (debug) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
			       i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

#define cs53l32a_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define cs53l32a_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

static unsigned short normal_i2c[] = { 0x22 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

static int cs53l32a_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int cs53l32a_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int cs53l32a_command(struct i2c_client *client, unsigned int cmd,
			    void *arg)
{
	int *input = arg;

	switch (cmd) {
	case AUDC_SET_INPUT:
		switch (*input) {
		case AUDIO_TUNER:
			cs53l32a_write(client, 0x01, 0x01);
			break;
		case AUDIO_EXTERN:
			cs53l32a_write(client, 0x01, 0x21);
			break;
		case AUDIO_MUTE:
			cs53l32a_write(client, 0x03, 0xF0);
			break;
		case AUDIO_UNMUTE:
			cs53l32a_write(client, 0x03, 0x30);
			break;
		default:
			cs53l32a_err("Invalid input %d.\n", *input);
			return -EINVAL;
		}
		break;

	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			if (ctrl->id != V4L2_CID_AUDIO_VOLUME)
				return -EINVAL;
			if (ctrl->value > 12 || ctrl->value < -90)
				return -EINVAL;
			cs53l32a_write(client, 0x04, (u8) ctrl->value);
			cs53l32a_write(client, 0x05, (u8) ctrl->value);
			break;
		}

	case VIDIOC_LOG_STATUS:
		{
			u8 v = cs53l32a_read(client, 0x01);
			u8 m = cs53l32a_read(client, 0x03);

			cs53l32a_info("Input: %s%s\n",
				      v == 0x21 ? "external line in" : "tuner",
				      (m & 0xC0) ? " (muted)" : "");
			break;
		}

	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static struct i2c_driver i2c_driver;

static int cs53l32a_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	int i;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;

	memset(client, 0, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	client->flags = I2C_CLIENT_ALLOW_USE;
	snprintf(client->name, sizeof(client->name) - 1, "cs53l32a");

	cs53l32a_info("chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(client, i);

		cs53l32a_dbg("Read Reg %d %02x\n", i, v);
	}

	/* Set cs53l32a internal register for Adaptec 2010/2410 setup */

	cs53l32a_write(client, 0x01, (u8) 0x21);
	cs53l32a_write(client, 0x02, (u8) 0x29);
	cs53l32a_write(client, 0x03, (u8) 0x30);
	cs53l32a_write(client, 0x04, (u8) 0x00);
	cs53l32a_write(client, 0x05, (u8) 0x00);
	cs53l32a_write(client, 0x06, (u8) 0x00);
	cs53l32a_write(client, 0x07, (u8) 0x00);

	/* Display results, should be 0x21,0x29,0x30,0x00,0x00,0x00,0x00 */

	for (i = 1; i <= 7; i++) {
		u8 v = cs53l32a_read(client, i);

		cs53l32a_dbg("Read Reg %d %02x\n", i, v);
	}

	i2c_attach_client(client);

	return 0;
}

static int cs53l32a_probe(struct i2c_adapter *adapter)
{
#ifdef I2C_CLASS_TV_ANALOG
	if (adapter->class & I2C_CLASS_TV_ANALOG)
#else
	if (adapter->id == I2C_HW_B_BT848)
#endif
		return i2c_probe(adapter, &addr_data, cs53l32a_attach);
	return 0;
}

static int cs53l32a_detach(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.name = "cs53l32a",
	.id = I2C_DRIVERID_CS53L32A,
	.flags = I2C_DF_NOTIFY,
	.attach_adapter = cs53l32a_probe,
	.detach_client = cs53l32a_detach,
	.command = cs53l32a_command,
	.owner = THIS_MODULE,
};


static int __init cs53l32a_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit cs53l32a_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(cs53l32a_init_module);
module_exit(cs53l32a_cleanup_module);
