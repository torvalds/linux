/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/video_decoder.h>

#include "wis-i2c.h"

struct wis_ov7640 {
	int brightness;
	int contrast;
	int saturation;
	int hue;
};

static u8 initial_registers[] =
{
	0x12, 0x80,
	0x12, 0x54,
	0x14, 0x24,
	0x15, 0x01,
	0x28, 0x20,
	0x75, 0x82,
	0xFF, 0xFF, /* Terminator (reg 0xFF is unused) */
};

static int write_regs(struct i2c_client *client, u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0xFF; i += 2)
		if (i2c_smbus_write_byte_data(client, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

static struct i2c_driver wis_ov7640_driver;

static struct i2c_client wis_ov7640_client_templ = {
	.name		= "OV7640 (WIS)",
	.driver		= &wis_ov7640_driver,
};

static int wis_ov7640_detect(struct i2c_adapter *adapter, int addr, int kind)
{
	struct i2c_client *client;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memcpy(client, &wis_ov7640_client_templ,
			sizeof(wis_ov7640_client_templ));
	client->adapter = adapter;
	client->addr = addr;
	client->flags = I2C_CLIENT_SCCB;

	printk(KERN_DEBUG
		"wis-ov7640: initializing OV7640 at address %d on %s\n",
		addr, adapter->name);

	if (write_regs(client, initial_registers) < 0) {
		printk(KERN_ERR "wis-ov7640: error initializing OV7640\n");
		kfree(client);
		return 0;
	}

	i2c_attach_client(client);
	return 0;
}

static int wis_ov7640_detach(struct i2c_client *client)
{
	int r;

	r = i2c_detach_client(client);
	if (r < 0)
		return r;

	kfree(client);
	return 0;
}

static struct i2c_driver wis_ov7640_driver = {
	.driver = {
		.name	= "WIS OV7640 I2C driver",
	},
	.id		= I2C_DRIVERID_WIS_OV7640,
	.detach_client	= wis_ov7640_detach,
};

static int __init wis_ov7640_init(void)
{
	int r;

	r = i2c_add_driver(&wis_ov7640_driver);
	if (r < 0)
		return r;
	return wis_i2c_add_driver(wis_ov7640_driver.id, wis_ov7640_detect);
}

static void __exit wis_ov7640_cleanup(void)
{
	wis_i2c_del_driver(wis_ov7640_detect);
	i2c_del_driver(&wis_ov7640_driver);
}

module_init(wis_ov7640_init);
module_exit(wis_ov7640_cleanup);

MODULE_LICENSE("GPL v2");
