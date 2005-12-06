/*
    max6875.c - driver for MAX6874/MAX6875

    Copyright (C) 2005 Ben Gardner <bgardner@wabtec.com>

    Based on i2c/chips/eeprom.c

    The MAX6875 has a bank of registers and two banks of EEPROM.
    Address ranges are defined as follows:
     * 0x0000 - 0x0046 = configuration registers
     * 0x8000 - 0x8046 = configuration EEPROM
     * 0x8100 - 0x82FF = user EEPROM

    This driver makes the user EEPROM available for read.

    The registers & config EEPROM should be accessed via i2c-dev.

    The MAX6875 ignores the lowest address bit, so each chip responds to
    two addresses - 0x50/0x51 and 0x52/0x53.

    Note that the MAX6875 uses i2c_smbus_write_byte_data() to set the read
    address, so this driver is destructive if loaded for the wrong EEPROM chip.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <asm/semaphore.h>

/* Do not scan - the MAX6875 access method will write to some EEPROM chips */
static unsigned short normal_i2c[] = {I2C_CLIENT_END};

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(max6875);

/* The MAX6875 can only read/write 16 bytes at a time */
#define SLICE_SIZE			16
#define SLICE_BITS			4

/* USER EEPROM is at addresses 0x8100 - 0x82FF */
#define USER_EEPROM_BASE		0x8100
#define USER_EEPROM_SIZE		0x0200
#define USER_EEPROM_SLICES		32

/* MAX6875 commands */
#define MAX6875_CMD_BLK_READ		0x84

/* Each client has this additional data */
struct max6875_data {
	struct i2c_client	client;
	struct semaphore	update_lock;

	u32			valid;
	u8			data[USER_EEPROM_SIZE];
	unsigned long		last_updated[USER_EEPROM_SLICES];
};

static int max6875_attach_adapter(struct i2c_adapter *adapter);
static int max6875_detect(struct i2c_adapter *adapter, int address, int kind);
static int max6875_detach_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver max6875_driver = {
	.driver = {
		.name	= "max6875",
	},
	.attach_adapter	= max6875_attach_adapter,
	.detach_client	= max6875_detach_client,
};

static void max6875_update_slice(struct i2c_client *client, int slice)
{
	struct max6875_data *data = i2c_get_clientdata(client);
	int i, j, addr;
	u8 *buf;

	if (slice >= USER_EEPROM_SLICES)
		return;

	down(&data->update_lock);

	buf = &data->data[slice << SLICE_BITS];

	if (!(data->valid & (1 << slice)) ||
	    time_after(jiffies, data->last_updated[slice])) {

		dev_dbg(&client->dev, "Starting update of slice %u\n", slice);

		data->valid &= ~(1 << slice);

		addr = USER_EEPROM_BASE + (slice << SLICE_BITS);

		/* select the eeprom address */
		if (i2c_smbus_write_byte_data(client, addr >> 8, addr & 0xFF)) {
			dev_err(&client->dev, "address set failed\n");
			goto exit_up;
		}

		if (i2c_check_functionality(client->adapter,
					    I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			if (i2c_smbus_read_i2c_block_data(client,
							  MAX6875_CMD_BLK_READ,
							  buf) != SLICE_SIZE) {
				goto exit_up;
			}
		} else {
			for (i = 0; i < SLICE_SIZE; i++) {
				j = i2c_smbus_read_byte(client);
				if (j < 0) {
					goto exit_up;
				}
				buf[i] = j;
			}
		}
		data->last_updated[slice] = jiffies;
		data->valid |= (1 << slice);
	}
exit_up:
	up(&data->update_lock);
}

static ssize_t max6875_read(struct kobject *kobj, char *buf, loff_t off,
			    size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);
	struct max6875_data *data = i2c_get_clientdata(client);
	int slice, max_slice;

	if (off > USER_EEPROM_SIZE)
		return 0;

	if (off + count > USER_EEPROM_SIZE)
		count = USER_EEPROM_SIZE - off;

	/* refresh slices which contain requested bytes */
	max_slice = (off + count - 1) >> SLICE_BITS;
	for (slice = (off >> SLICE_BITS); slice <= max_slice; slice++)
		max6875_update_slice(client, slice);

	memcpy(buf, &data->data[off], count);

	return count;
}

static struct bin_attribute user_eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = USER_EEPROM_SIZE,
	.read = max6875_read,
};

static int max6875_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, max6875_detect);
}

/* This function is called by i2c_probe */
static int max6875_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *real_client;
	struct i2c_client *fake_client;
	struct max6875_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_BYTE))
		return 0;

	/* Only check even addresses */
	if (address & 1)
		return 0;

	if (!(data = kzalloc(sizeof(struct max6875_data), GFP_KERNEL)))
		return -ENOMEM;

	/* A fake client is created on the odd address */
	if (!(fake_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_kfree1;
	}

	/* Init real i2c_client */
	real_client = &data->client;
	i2c_set_clientdata(real_client, data);
	real_client->addr = address;
	real_client->adapter = adapter;
	real_client->driver = &max6875_driver;
	real_client->flags = 0;
	strlcpy(real_client->name, "max6875", I2C_NAME_SIZE);
	init_MUTEX(&data->update_lock);

	/* Init fake client data */
	/* set the client data to the i2c_client so that it will get freed */
	i2c_set_clientdata(fake_client, fake_client);
	fake_client->addr = address | 1;
	fake_client->adapter = adapter;
	fake_client->driver = &max6875_driver;
	fake_client->flags = 0;
	strlcpy(fake_client->name, "max6875 subclient", I2C_NAME_SIZE);

	/* Prevent 24RF08 corruption (in case of user error) */
	i2c_smbus_write_quick(real_client, 0);

	if ((err = i2c_attach_client(real_client)) != 0)
		goto exit_kfree2;

	if ((err = i2c_attach_client(fake_client)) != 0)
		goto exit_detach;

	sysfs_create_bin_file(&real_client->dev.kobj, &user_eeprom_attr);

	return 0;

exit_detach:
	i2c_detach_client(real_client);
exit_kfree2:
	kfree(fake_client);
exit_kfree1:
	kfree(data);
	return err;
}

static int max6875_detach_client(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err)
		return err;
	kfree(i2c_get_clientdata(client));
	return 0;
}

static int __init max6875_init(void)
{
	return i2c_add_driver(&max6875_driver);
}

static void __exit max6875_exit(void)
{
	i2c_del_driver(&max6875_driver);
}


MODULE_AUTHOR("Ben Gardner <bgardner@wabtec.com>");
MODULE_DESCRIPTION("MAX6875 driver");
MODULE_LICENSE("GPL");

module_init(max6875_init);
module_exit(max6875_exit);
