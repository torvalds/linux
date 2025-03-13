// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C slave mode EEPROM simulator
 *
 * Copyright (C) 2014 by Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 * Copyright (C) 2014 by Renesas Electronics Corporation
 *
 * Because most slave IP cores can only detect one I2C slave address anyhow,
 * this driver does not support simulating EEPROM types which take more than
 * one address.
 */

/*
 * FIXME: What to do if only 8 bits of a 16 bit address are sent?
 * The ST-M24C64 sends only 0xff then. Needs verification with other
 * EEPROMs, though. We currently use the 8 bit as a valid address.
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

struct eeprom_data {
	struct bin_attribute bin;
	spinlock_t buffer_lock;
	u16 buffer_idx;
	u16 address_mask;
	u8 num_address_bytes;
	u8 idx_write_cnt;
	bool read_only;
	u8 buffer[];
};

#define I2C_SLAVE_BYTELEN GENMASK(15, 0)
#define I2C_SLAVE_FLAG_ADDR16 BIT(16)
#define I2C_SLAVE_FLAG_RO BIT(17)
#define I2C_SLAVE_DEVICE_MAGIC(_len, _flags) ((_flags) | ((_len) - 1))

static int i2c_slave_eeprom_slave_cb(struct i2c_client *client,
				     enum i2c_slave_event event, u8 *val)
{
	struct eeprom_data *eeprom = i2c_get_clientdata(client);

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		if (eeprom->idx_write_cnt < eeprom->num_address_bytes) {
			if (eeprom->idx_write_cnt == 0)
				eeprom->buffer_idx = 0;
			eeprom->buffer_idx = *val | (eeprom->buffer_idx << 8);
			eeprom->idx_write_cnt++;
		} else {
			if (!eeprom->read_only) {
				spin_lock(&eeprom->buffer_lock);
				eeprom->buffer[eeprom->buffer_idx++ & eeprom->address_mask] = *val;
				spin_unlock(&eeprom->buffer_lock);
			}
		}
		break;

	case I2C_SLAVE_READ_PROCESSED:
		/* The previous byte made it to the bus, get next one */
		eeprom->buffer_idx++;
		fallthrough;
	case I2C_SLAVE_READ_REQUESTED:
		spin_lock(&eeprom->buffer_lock);
		*val = eeprom->buffer[eeprom->buffer_idx & eeprom->address_mask];
		spin_unlock(&eeprom->buffer_lock);
		/*
		 * Do not increment buffer_idx here, because we don't know if
		 * this byte will be actually used. Read Linux I2C slave docs
		 * for details.
		 */
		break;

	case I2C_SLAVE_STOP:
	case I2C_SLAVE_WRITE_REQUESTED:
		eeprom->idx_write_cnt = 0;
		break;

	default:
		break;
	}

	return 0;
}

static ssize_t i2c_slave_eeprom_bin_read(struct file *filp, struct kobject *kobj,
		const struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	eeprom = dev_get_drvdata(kobj_to_dev(kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(buf, &eeprom->buffer[off], count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

static ssize_t i2c_slave_eeprom_bin_write(struct file *filp, struct kobject *kobj,
		const struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	eeprom = dev_get_drvdata(kobj_to_dev(kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(&eeprom->buffer[off], buf, count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

static int i2c_slave_init_eeprom_data(struct eeprom_data *eeprom, struct i2c_client *client,
				      unsigned int size)
{
	const struct firmware *fw;
	const char *eeprom_data;
	int ret = device_property_read_string(&client->dev, "firmware-name", &eeprom_data);

	if (!ret) {
		ret = request_firmware_into_buf(&fw, eeprom_data, &client->dev,
						eeprom->buffer, size);
		if (ret)
			return ret;
		release_firmware(fw);
	} else {
		/* An empty eeprom typically has all bits set to 1 */
		memset(eeprom->buffer, 0xff, size);
	}
	return 0;
}

static int i2c_slave_eeprom_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct eeprom_data *eeprom;
	int ret;
	unsigned int size = FIELD_GET(I2C_SLAVE_BYTELEN, id->driver_data) + 1;
	unsigned int flag_addr16 = FIELD_GET(I2C_SLAVE_FLAG_ADDR16, id->driver_data);

	eeprom = devm_kzalloc(&client->dev, sizeof(struct eeprom_data) + size, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	eeprom->num_address_bytes = flag_addr16 ? 2 : 1;
	eeprom->address_mask = size - 1;
	eeprom->read_only = FIELD_GET(I2C_SLAVE_FLAG_RO, id->driver_data);
	spin_lock_init(&eeprom->buffer_lock);
	i2c_set_clientdata(client, eeprom);

	ret = i2c_slave_init_eeprom_data(eeprom, client, size);
	if (ret)
		return ret;

	sysfs_bin_attr_init(&eeprom->bin);
	eeprom->bin.attr.name = "slave-eeprom";
	eeprom->bin.attr.mode = S_IRUSR | S_IWUSR;
	eeprom->bin.read_new = i2c_slave_eeprom_bin_read;
	eeprom->bin.write_new = i2c_slave_eeprom_bin_write;
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

static void i2c_slave_eeprom_remove(struct i2c_client *client)
{
	struct eeprom_data *eeprom = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	sysfs_remove_bin_file(&client->dev.kobj, &eeprom->bin);
}

static const struct i2c_device_id i2c_slave_eeprom_id[] = {
	{ "slave-24c02", I2C_SLAVE_DEVICE_MAGIC(2048 / 8,  0) },
	{ "slave-24c02ro", I2C_SLAVE_DEVICE_MAGIC(2048 / 8,  I2C_SLAVE_FLAG_RO) },
	{ "slave-24c32", I2C_SLAVE_DEVICE_MAGIC(32768 / 8, I2C_SLAVE_FLAG_ADDR16) },
	{ "slave-24c32ro", I2C_SLAVE_DEVICE_MAGIC(32768 / 8, I2C_SLAVE_FLAG_ADDR16 | I2C_SLAVE_FLAG_RO) },
	{ "slave-24c64", I2C_SLAVE_DEVICE_MAGIC(65536 / 8, I2C_SLAVE_FLAG_ADDR16) },
	{ "slave-24c64ro", I2C_SLAVE_DEVICE_MAGIC(65536 / 8, I2C_SLAVE_FLAG_ADDR16 | I2C_SLAVE_FLAG_RO) },
	{ "slave-24c512", I2C_SLAVE_DEVICE_MAGIC(524288 / 8, I2C_SLAVE_FLAG_ADDR16) },
	{ "slave-24c512ro", I2C_SLAVE_DEVICE_MAGIC(524288 / 8, I2C_SLAVE_FLAG_ADDR16 | I2C_SLAVE_FLAG_RO) },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_eeprom_id);

static struct i2c_driver i2c_slave_eeprom_driver = {
	.driver = {
		.name = "i2c-slave-eeprom",
	},
	.probe = i2c_slave_eeprom_probe,
	.remove = i2c_slave_eeprom_remove,
	.id_table = i2c_slave_eeprom_id,
};
module_i2c_driver(i2c_slave_eeprom_driver);

MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_DESCRIPTION("I2C slave mode EEPROM simulator");
MODULE_LICENSE("GPL v2");
