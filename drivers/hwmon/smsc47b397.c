/*
    smsc47b397.c - Part of lm_sensors, Linux kernel modules
			for hardware monitoring

    Supports the SMSC LPC47B397-NC Super-I/O chip.

    Author/Maintainer: Mark M. Hoffman <mhoffman@lightlink.com>
	Copyright (C) 2004 Utilitek Systems, Inc.

    derived in part from smsc47m1.c:
	Copyright (C) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
	Copyright (C) 2004 Jean Delvare <khali@linux-fr.org>

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
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/init.h>
#include <asm/io.h>

/* Address is autodetected, there is no default value */
static unsigned short address;

/* Super-I/0 registers and commands */

#define	REG	0x2e	/* The register to read/write */
#define	VAL	0x2f	/* The value to read/write */

static inline void superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

/* select superio logical device */
static inline void superio_select(int ld)
{
	superio_outb(0x07, ld);
}

static inline void superio_enter(void)
{
	outb(0x55, REG);
}

static inline void superio_exit(void)
{
	outb(0xAA, REG);
}

#define SUPERIO_REG_DEVID	0x20
#define SUPERIO_REG_DEVREV	0x21
#define SUPERIO_REG_BASE_MSB	0x60
#define SUPERIO_REG_BASE_LSB	0x61
#define SUPERIO_REG_LD8		0x08

#define SMSC_EXTENT		0x02

/* 0 <= nr <= 3 */
static u8 smsc47b397_reg_temp[] = {0x25, 0x26, 0x27, 0x80};
#define SMSC47B397_REG_TEMP(nr)	(smsc47b397_reg_temp[(nr)])

/* 0 <= nr <= 3 */
#define SMSC47B397_REG_FAN_LSB(nr) (0x28 + 2 * (nr))
#define SMSC47B397_REG_FAN_MSB(nr) (0x29 + 2 * (nr))

struct smsc47b397_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;

	struct semaphore update_lock;
	unsigned long last_updated; /* in jiffies */
	int valid;

	/* register values */
	u16 fan[4];
	u8 temp[4];
};

static int smsc47b397_read_value(struct i2c_client *client, u8 reg)
{
	struct smsc47b397_data *data = i2c_get_clientdata(client);
	int res;

	down(&data->lock);
	outb(reg, client->addr);
	res = inb_p(client->addr + 1);
	up(&data->lock);
	return res;
}

static struct smsc47b397_data *smsc47b397_update_device(struct device *dev)
{
 	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47b397_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		dev_dbg(&client->dev, "starting device update...\n");

		/* 4 temperature inputs, 4 fan inputs */
		for (i = 0; i < 4; i++) {
			data->temp[i] = smsc47b397_read_value(client,
					SMSC47B397_REG_TEMP(i));

			/* must read LSB first */
			data->fan[i]  = smsc47b397_read_value(client,
					SMSC47B397_REG_FAN_LSB(i));
			data->fan[i] |= smsc47b397_read_value(client,
					SMSC47B397_REG_FAN_MSB(i)) << 8;
		}

		data->last_updated = jiffies;
		data->valid = 1;

		dev_dbg(&client->dev, "... device update complete\n");
	}

	up(&data->update_lock);

	return data;
}

/* TEMP: 0.001C/bit (-128C to +127C)
   REG: 1C/bit, two's complement */
static int temp_from_reg(u8 reg)
{
	return (s8)reg * 1000;
}

/* 0 <= nr <= 3 */
static ssize_t show_temp(struct device *dev, char *buf, int nr)
{
	struct smsc47b397_data *data = smsc47b397_update_device(dev);
	return sprintf(buf, "%d\n", temp_from_reg(data->temp[nr]));
}

#define sysfs_temp(num) \
static ssize_t show_temp##num(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp(dev, buf, num-1); \
} \
static DEVICE_ATTR(temp##num##_input, S_IRUGO, show_temp##num, NULL)

sysfs_temp(1);
sysfs_temp(2);
sysfs_temp(3);
sysfs_temp(4);

#define device_create_file_temp(client, num) \
	device_create_file(&client->dev, &dev_attr_temp##num##_input)

/* FAN: 1 RPM/bit
   REG: count of 90kHz pulses / revolution */
static int fan_from_reg(u16 reg)
{
	return 90000 * 60 / reg;
}

/* 0 <= nr <= 3 */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
        struct smsc47b397_data *data = smsc47b397_update_device(dev);
        return sprintf(buf, "%d\n", fan_from_reg(data->fan[nr]));
}

#define sysfs_fan(num) \
static ssize_t show_fan##num(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan(dev, buf, num-1); \
} \
static DEVICE_ATTR(fan##num##_input, S_IRUGO, show_fan##num, NULL)

sysfs_fan(1);
sysfs_fan(2);
sysfs_fan(3);
sysfs_fan(4);

#define device_create_file_fan(client, num) \
	device_create_file(&client->dev, &dev_attr_fan##num##_input)

static int smsc47b397_detach_client(struct i2c_client *client)
{
	struct smsc47b397_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	release_region(client->addr, SMSC_EXTENT);
	kfree(data);

	return 0;
}

static int smsc47b397_detect(struct i2c_adapter *adapter);

static struct i2c_driver smsc47b397_driver = {
	.owner		= THIS_MODULE,
	.name		= "smsc47b397",
	.attach_adapter	= smsc47b397_detect,
	.detach_client	= smsc47b397_detach_client,
};

static int smsc47b397_detect(struct i2c_adapter *adapter)
{
	struct i2c_client *new_client;
	struct smsc47b397_data *data;
	int err = 0;

	if (!request_region(address, SMSC_EXTENT, smsc47b397_driver.name)) {
		dev_err(&adapter->dev, "Region 0x%x already in use!\n",
			address);
		return -EBUSY;
	}

	if (!(data = kmalloc(sizeof(struct smsc47b397_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto error_release;
	}
	memset(data, 0x00, sizeof(struct smsc47b397_data));

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	init_MUTEX(&data->lock);
	new_client->adapter = adapter;
	new_client->driver = &smsc47b397_driver;
	new_client->flags = 0;

	strlcpy(new_client->name, "smsc47b397", I2C_NAME_SIZE);

	init_MUTEX(&data->update_lock);

	if ((err = i2c_attach_client(new_client)))
		goto error_free;

	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto error_detach;
	}

	device_create_file_temp(new_client, 1);
	device_create_file_temp(new_client, 2);
	device_create_file_temp(new_client, 3);
	device_create_file_temp(new_client, 4);

	device_create_file_fan(new_client, 1);
	device_create_file_fan(new_client, 2);
	device_create_file_fan(new_client, 3);
	device_create_file_fan(new_client, 4);

	return 0;

error_detach:
	i2c_detach_client(new_client);
error_free:
	kfree(data);
error_release:
	release_region(address, SMSC_EXTENT);
	return err;
}

static int __init smsc47b397_find(unsigned short *addr)
{
	u8 id, rev;

	superio_enter();
	id = superio_inb(SUPERIO_REG_DEVID);

	if (id != 0x6f) {
		superio_exit();
		return -ENODEV;
	}

	rev = superio_inb(SUPERIO_REG_DEVREV);

	superio_select(SUPERIO_REG_LD8);
	*addr = (superio_inb(SUPERIO_REG_BASE_MSB) << 8)
		 |  superio_inb(SUPERIO_REG_BASE_LSB);

	printk(KERN_INFO "smsc47b397: found SMSC LPC47B397-NC "
		"(base address 0x%04x, revision %u)\n", *addr, rev);

	superio_exit();
	return 0;
}

static int __init smsc47b397_init(void)
{
	int ret;

	if ((ret = smsc47b397_find(&address)))
		return ret;

	return i2c_isa_add_driver(&smsc47b397_driver);
}

static void __exit smsc47b397_exit(void)
{
	i2c_isa_del_driver(&smsc47b397_driver);
}

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("SMSC LPC47B397 driver");
MODULE_LICENSE("GPL");

module_init(smsc47b397_init);
module_exit(smsc47b397_exit);
