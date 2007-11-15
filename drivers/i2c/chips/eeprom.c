/*
    eeprom.c - Part of lm_sensors, Linux kernel modules for hardware
               monitoring
    Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
			       Philip Edelbrock <phil@netroedge.com>
    Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
    Copyright (C) 2003 IBM Corp.

    2004-01-16  Jean Delvare <khali@linux-fr.org>
    Divide the eeprom in 32-byte (arbitrary) slices. This significantly
    speeds sensors up, as well as various scripts using the eeprom
    module.

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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x50, 0x51, 0x52, 0x53, 0x54,
					0x55, 0x56, 0x57, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(eeprom);


/* Size of EEPROM in bytes */
#define EEPROM_SIZE		256

/* possible types of eeprom devices */
enum eeprom_nature {
	UNKNOWN,
	VAIO,
};

/* Each client has this additional data */
struct eeprom_data {
	struct i2c_client client;
	struct mutex update_lock;
	u8 valid;			/* bitfield, bit!=0 if slice is valid */
	unsigned long last_updated[8];	/* In jiffies, 8 slices */
	u8 data[EEPROM_SIZE];		/* Register values */
	enum eeprom_nature nature;
};


static int eeprom_attach_adapter(struct i2c_adapter *adapter);
static int eeprom_detect(struct i2c_adapter *adapter, int address, int kind);
static int eeprom_detach_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver eeprom_driver = {
	.driver = {
		.name	= "eeprom",
	},
	.id		= I2C_DRIVERID_EEPROM,
	.attach_adapter	= eeprom_attach_adapter,
	.detach_client	= eeprom_detach_client,
};

static void eeprom_update_client(struct i2c_client *client, u8 slice)
{
	struct eeprom_data *data = i2c_get_clientdata(client);
	int i, j;

	mutex_lock(&data->update_lock);

	if (!(data->valid & (1 << slice)) ||
	    time_after(jiffies, data->last_updated[slice] + 300 * HZ)) {
		dev_dbg(&client->dev, "Starting eeprom update, slice %u\n", slice);

		if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			for (i = slice << 5; i < (slice + 1) << 5; i += 32)
				if (i2c_smbus_read_i2c_block_data(client, i,
							32, data->data + i)
							!= 32)
					goto exit;
		} else {
			if (i2c_smbus_write_byte(client, slice << 5)) {
				dev_dbg(&client->dev, "eeprom read start has failed!\n");
				goto exit;
			}
			for (i = slice << 5; i < (slice + 1) << 5; i++) {
				j = i2c_smbus_read_byte(client);
				if (j < 0)
					goto exit;
				data->data[i] = (u8) j;
			}
		}
		data->last_updated[slice] = jiffies;
		data->valid |= (1 << slice);
	}
exit:
	mutex_unlock(&data->update_lock);
}

static ssize_t eeprom_read(struct kobject *kobj, struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = to_i2c_client(container_of(kobj, struct device, kobj));
	struct eeprom_data *data = i2c_get_clientdata(client);
	u8 slice;

	if (off > EEPROM_SIZE)
		return 0;
	if (off + count > EEPROM_SIZE)
		count = EEPROM_SIZE - off;

	/* Only refresh slices which contain requested bytes */
	for (slice = off >> 5; slice <= (off + count - 1) >> 5; slice++)
		eeprom_update_client(client, slice);

	/* Hide Vaio private settings to regular users:
	   - BIOS passwords: bytes 0x00 to 0x0f
	   - UUID: bytes 0x10 to 0x1f
	   - Serial number: 0xc0 to 0xdf */
	if (data->nature == VAIO && !capable(CAP_SYS_ADMIN)) {
		int i;

		for (i = 0; i < count; i++) {
			if ((off + i <= 0x1f) ||
			    (off + i >= 0xc0 && off + i <= 0xdf))
				buf[i] = 0;
			else
				buf[i] = data->data[off + i];
		}
	} else {
		memcpy(buf, &data->data[off], count);
	}

	return count;
}

static struct bin_attribute eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO,
	},
	.size = EEPROM_SIZE,
	.read = eeprom_read,
};

static int eeprom_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, eeprom_detect);
}

/* This function is called by i2c_probe */
static int eeprom_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct eeprom_data *data;
	int err = 0;

	/* There are three ways we can read the EEPROM data:
	   (1) I2C block reads (faster, but unsupported by most adapters)
	   (2) Consecutive byte reads (100% overhead)
	   (3) Regular byte data reads (200% overhead)
	   The third method is not implemented by this driver because all
	   known adapters support at least the second. */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA
					    | I2C_FUNC_SMBUS_BYTE))
		goto exit;

	if (!(data = kzalloc(sizeof(struct eeprom_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	new_client = &data->client;
	memset(data->data, 0xff, EEPROM_SIZE);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &eeprom_driver;
	new_client->flags = 0;

	/* Fill in the remaining client fields */
	strlcpy(new_client->name, "eeprom", I2C_NAME_SIZE);
	data->valid = 0;
	mutex_init(&data->update_lock);
	data->nature = UNKNOWN;

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_kfree;

	/* Detect the Vaio nature of EEPROMs.
	   We use the "PCG-" or "VGN-" prefix as the signature. */
	if (address == 0x57) {
		char name[4];

		name[0] = i2c_smbus_read_byte_data(new_client, 0x80);
		name[1] = i2c_smbus_read_byte(new_client);
		name[2] = i2c_smbus_read_byte(new_client);
		name[3] = i2c_smbus_read_byte(new_client);

		if (!memcmp(name, "PCG-", 4) || !memcmp(name, "VGN-", 4)) {
			dev_info(&new_client->dev, "Vaio EEPROM detected, "
				 "enabling privacy protection\n");
			data->nature = VAIO;
		}
	}

	/* create the sysfs eeprom file */
	err = sysfs_create_bin_file(&new_client->dev.kobj, &eeprom_attr);
	if (err)
		goto exit_detach;

	return 0;

exit_detach:
	i2c_detach_client(new_client);
exit_kfree:
	kfree(data);
exit:
	return err;
}

static int eeprom_detach_client(struct i2c_client *client)
{
	int err;

	sysfs_remove_bin_file(&client->dev.kobj, &eeprom_attr);

	err = i2c_detach_client(client);
	if (err)
		return err;

	kfree(i2c_get_clientdata(client));

	return 0;
}

static int __init eeprom_init(void)
{
	return i2c_add_driver(&eeprom_driver);
}

static void __exit eeprom_exit(void)
{
	i2c_del_driver(&eeprom_driver);
}


MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
		"Philip Edelbrock <phil@netroedge.com> and "
		"Greg Kroah-Hartman <greg@kroah.com>");
MODULE_DESCRIPTION("I2C EEPROM driver");
MODULE_LICENSE("GPL");

module_init(eeprom_init);
module_exit(eeprom_exit);
