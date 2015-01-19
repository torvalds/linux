/*
 * I2C slave mode EEPROM simulator
 *
 * Copyright (C) 2014 by Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 * Copyright (C) 2014 by Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * Because most IP blocks can only detect one I2C slave address anyhow, this
 * driver does not support simulating EEPROM types which take more than one
 * address. It is prepared to simulate bigger EEPROMs with an internal 16 bit
 * pointer, yet implementation is deferred until the need actually arises.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

struct eeprom_data {
	struct bin_attribute bin;
	bool first_write;
	spinlock_t buffer_lock;
	u8 buffer_idx;
	u8 buffer[];
};

static int i2c_slave_eeprom_slave_cb(struct i2c_client *client,
				     enum i2c_slave_event event, u8 *val)
{
	struct eeprom_data *eeprom = i2c_get_clientdata(client);

	switch (event) {
	case I2C_SLAVE_REQ_WRITE_END:
		if (eeprom->first_write) {
			eeprom->buffer_idx = *val;
			eeprom->first_write = false;
		} else {
			spin_lock(&eeprom->buffer_lock);
			eeprom->buffer[eeprom->buffer_idx++] = *val;
			spin_unlock(&eeprom->buffer_lock);
		}
		break;

	case I2C_SLAVE_REQ_READ_START:
		spin_lock(&eeprom->buffer_lock);
		*val = eeprom->buffer[eeprom->buffer_idx];
		spin_unlock(&eeprom->buffer_lock);
		break;

	case I2C_SLAVE_REQ_READ_END:
		eeprom->buffer_idx++;
		break;

	case I2C_SLAVE_STOP:
		eeprom->first_write = true;
		break;

	default:
		break;
	}

	return 0;
}

static ssize_t i2c_slave_eeprom_bin_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	if (off + count > attr->size)
		return -EFBIG;

	eeprom = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(buf, &eeprom->buffer[off], count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

static ssize_t i2c_slave_eeprom_bin_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	if (off + count > attr->size)
		return -EFBIG;

	eeprom = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(&eeprom->buffer[off], buf, count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

static int i2c_slave_eeprom_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct eeprom_data *eeprom;
	int ret;
	unsigned size = id->driver_data;

	eeprom = devm_kzalloc(&client->dev, sizeof(struct eeprom_data) + size, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	eeprom->first_write = true;
	spin_lock_init(&eeprom->buffer_lock);
	i2c_set_clientdata(client, eeprom);

	sysfs_bin_attr_init(&eeprom->bin);
	eeprom->bin.attr.name = "slave-eeprom";
	eeprom->bin.attr.mode = S_IRUSR | S_IWUSR;
	eeprom->bin.read = i2c_slave_eeprom_bin_read;
	eeprom->bin.write = i2c_slave_eeprom_bin_write;
	eeprom->bin.size = size;

	ret = sysfs_create_bin_file(&client->dev.kobj, &eeprom->bin);
	if (ret)
		return ret;

	ret = i2c_slave_register(client, i2c_slave_eeprom_slave_cb);
	if (ret) {
		sysfs_remove_bin_file(&client->dev.kobj, &eeprom->bin);
		return ret;
	}

	return 0;
};

static int i2c_slave_eeprom_remove(struct i2c_client *client)
{
	struct eeprom_data *eeprom = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	sysfs_remove_bin_file(&client->dev.kobj, &eeprom->bin);

	return 0;
}

static const struct i2c_device_id i2c_slave_eeprom_id[] = {
	{ "slave-24c02", 2048 / 8 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_eeprom_id);

static struct i2c_driver i2c_slave_eeprom_driver = {
	.driver = {
		.name = "i2c-slave-eeprom",
		.owner = THIS_MODULE,
	},
	.probe = i2c_slave_eeprom_probe,
	.remove = i2c_slave_eeprom_remove,
	.id_table = i2c_slave_eeprom_id,
};
module_i2c_driver(i2c_slave_eeprom_driver);

MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_DESCRIPTION("I2C slave mode EEPROM simulator");
MODULE_LICENSE("GPL v2");
