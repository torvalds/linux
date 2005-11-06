/*
 *	ds_w1_bridge.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/types.h>

#include "../w1/w1.h"
#include "../w1/w1_int.h"
#include "dscore.h"

static struct ds_device *ds_dev;
static struct w1_bus_master *ds_bus_master;

static u8 ds9490r_touch_bit(unsigned long data, u8 bit)
{
	u8 ret;
	struct ds_device *dev = (struct ds_device *)data;

	if (ds_touch_bit(dev, bit, &ret))
		return 0;

	return ret;
}

static void ds9490r_write_bit(unsigned long data, u8 bit)
{
	struct ds_device *dev = (struct ds_device *)data;

	ds_write_bit(dev, bit);
}

static void ds9490r_write_byte(unsigned long data, u8 byte)
{
	struct ds_device *dev = (struct ds_device *)data;

	ds_write_byte(dev, byte);
}

static u8 ds9490r_read_bit(unsigned long data)
{
	struct ds_device *dev = (struct ds_device *)data;
	int err;
	u8 bit = 0;

	err = ds_touch_bit(dev, 1, &bit);
	if (err)
		return 0;
	//err = ds_read_bit(dev, &bit);
	//if (err)
	//	return 0;

	return bit & 1;
}

static u8 ds9490r_read_byte(unsigned long data)
{
	struct ds_device *dev = (struct ds_device *)data;
	int err;
	u8 byte = 0;

	err = ds_read_byte(dev, &byte);
	if (err)
		return 0;

	return byte;
}

static void ds9490r_write_block(unsigned long data, const u8 *buf, int len)
{
	struct ds_device *dev = (struct ds_device *)data;

	ds_write_block(dev, (u8 *)buf, len);
}

static u8 ds9490r_read_block(unsigned long data, u8 *buf, int len)
{
	struct ds_device *dev = (struct ds_device *)data;
	int err;

	err = ds_read_block(dev, buf, len);
	if (err < 0)
		return 0;

	return len;
}

static u8 ds9490r_reset(unsigned long data)
{
	struct ds_device *dev = (struct ds_device *)data;
	struct ds_status st;
	int err;

	memset(&st, 0, sizeof(st));

	err = ds_reset(dev, &st);
	if (err)
		return 1;

	return 0;
}

static int __devinit ds_w1_init(void)
{
	int err;

	ds_bus_master = kmalloc(sizeof(*ds_bus_master), GFP_KERNEL);
	if (!ds_bus_master) {
		printk(KERN_ERR "Failed to allocate DS9490R USB<->W1 bus_master structure.\n");
		return -ENOMEM;
	}

	ds_dev = ds_get_device();
	if (!ds_dev) {
		printk(KERN_ERR "DS9490R is not registered.\n");
		err =  -ENODEV;
		goto err_out_free_bus_master;
	}

	memset(ds_bus_master, 0, sizeof(*ds_bus_master));

	ds_bus_master->data		= (unsigned long)ds_dev;
	ds_bus_master->touch_bit	= &ds9490r_touch_bit;
	ds_bus_master->read_bit		= &ds9490r_read_bit;
	ds_bus_master->write_bit	= &ds9490r_write_bit;
	ds_bus_master->read_byte	= &ds9490r_read_byte;
	ds_bus_master->write_byte	= &ds9490r_write_byte;
	ds_bus_master->read_block	= &ds9490r_read_block;
	ds_bus_master->write_block	= &ds9490r_write_block;
	ds_bus_master->reset_bus	= &ds9490r_reset;

	err = w1_add_master_device(ds_bus_master);
	if (err)
		goto err_out_put_device;

	return 0;

err_out_put_device:
	ds_put_device(ds_dev);
err_out_free_bus_master:
	kfree(ds_bus_master);

	return err;
}

static void __devexit ds_w1_fini(void)
{
	w1_remove_master_device(ds_bus_master);
	ds_put_device(ds_dev);
	kfree(ds_bus_master);
}

module_init(ds_w1_init);
module_exit(ds_w1_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
