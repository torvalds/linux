// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ee1004 - driver for DDR4 SPD EEPROMs
 *
 * Copyright (C) 2017-2019 Jean Delvare
 *
 * Based on the at24 driver:
 * Copyright (C) 2005-2007 David Brownell
 * Copyright (C) 2008 Wolfram Sang, Pengutronix
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>

/*
 * DDR4 memory modules use special EEPROMs following the Jedec EE1004
 * specification. These are 512-byte EEPROMs using a single I2C address
 * in the 0x50-0x57 range for data. One of two 256-byte page is selected
 * by writing a command to I2C address 0x36 or 0x37 on the same I2C bus.
 *
 * Therefore we need to request these 2 additional addresses, and serialize
 * access to all such EEPROMs with a single mutex.
 *
 * We assume it is safe to read up to 32 bytes at once from these EEPROMs.
 * We use SMBus access even if I2C is available, these EEPROMs are small
 * enough, and reading from them infrequent enough, that we favor simplicity
 * over performance.
 */

#define EE1004_ADDR_SET_PAGE0		0x36
#define EE1004_ADDR_SET_PAGE1		0x37
#define EE1004_NUM_PAGES		2
#define EE1004_PAGE_SIZE		256
#define EE1004_PAGE_SHIFT		8
#define EE1004_EEPROM_SIZE		(EE1004_PAGE_SIZE * EE1004_NUM_PAGES)

struct ee1004_bus {
	struct kref kref;
	struct list_head list;
	struct mutex lock;
	struct i2c_adapter *adapter;
	struct i2c_client *set_page_clients[EE1004_NUM_PAGES];
	int page;
};

static LIST_HEAD(ee1004_busses);
static DEFINE_MUTEX(ee1004_busses_lock);

static const struct i2c_device_id ee1004_ids[] = {
	{ "ee1004", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ee1004_ids);

static const struct of_device_id ee1004_of_match[] = {
	{ .compatible = "atmel,at30tse004a" },
	{}
};
MODULE_DEVICE_TABLE(of, ee1004_of_match);

/*-------------------------------------------------------------------------*/

static int ee1004_get_current_page(struct ee1004_bus *bus)
{
	int err;

	err = i2c_smbus_read_byte(bus->set_page_clients[0]);
	if (err == -ENXIO) {
		/* Nack means page 1 is selected */
		return 1;
	}
	if (err < 0) {
		/* Anything else is a real error, bail out */
		return err;
	}

	/* Ack means page 0 is selected, returned value meaningless */
	return 0;
}

static int ee1004_set_current_page(struct ee1004_bus *bus, int page)
{
	int ret;

	if (page == bus->page)
		return 0;

	/* Data is ignored */
	ret = i2c_smbus_write_byte(bus->set_page_clients[page], 0x00);

	/*
	 * Don't give up just yet. Some memory modules will select the page
	 * but not ack the command. Check which page is selected now.
	 */
	if (ret == -ENXIO && ee1004_get_current_page(bus) == page)
		ret = 0;
	if (ret < 0)
		return ret;

	bus->page = page;
	return 0;
}

static ssize_t ee1004_eeprom_read(struct i2c_client *client, struct ee1004_bus *bus, char *buf,
				  unsigned int offset, size_t count)
{
	int status, page;

	page = offset >> EE1004_PAGE_SHIFT;
	offset &= (1 << EE1004_PAGE_SHIFT) - 1;

	status = ee1004_set_current_page(bus, page);
	if (status) {
		dev_err(&client->dev, "Failed to select page %d (%d)\n", page, status);
		return status;
	}

	/* Can't cross page boundaries */
	if (offset + count > EE1004_PAGE_SIZE)
		count = EE1004_PAGE_SIZE - offset;

	if (count > I2C_SMBUS_BLOCK_MAX)
		count = I2C_SMBUS_BLOCK_MAX;

	return i2c_smbus_read_i2c_block_data_or_emulated(client, offset, count, buf);
}

static ssize_t eeprom_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);
	struct ee1004_bus *bus = i2c_get_clientdata(client);
	size_t requested = count;
	int ret = 0;

	/*
	 * Read data from chip, protecting against concurrent access to
	 * other EE1004 SPD EEPROMs on the same adapter.
	 */
	mutex_lock(&bus->lock);

	while (count) {
		ret = ee1004_eeprom_read(client, bus, buf, off, count);
		if (ret < 0)
			goto out;

		buf += ret;
		off += ret;
		count -= ret;
	}
out:
	mutex_unlock(&bus->lock);

	return ret < 0 ? ret : requested;
}

static BIN_ATTR_RO(eeprom, EE1004_EEPROM_SIZE);

static struct bin_attribute *ee1004_attrs[] = {
	&bin_attr_eeprom,
	NULL
};

BIN_ATTRIBUTE_GROUPS(ee1004);

static void ee1004_bus_unregister(struct ee1004_bus *bus)
{
	i2c_unregister_device(bus->set_page_clients[1]);
	i2c_unregister_device(bus->set_page_clients[0]);
}

static void ee1004_bus_release(struct kref *kref)
{
	struct ee1004_bus *bus = container_of(kref, struct ee1004_bus, kref);

	ee1004_bus_unregister(bus);

	mutex_lock(&ee1004_busses_lock);
	list_del(&bus->list);
	mutex_unlock(&ee1004_busses_lock);

	kfree(bus);
}

static int ee1004_bus_initialize(struct ee1004_bus *bus, struct i2c_adapter *adapter)
{
	bus->set_page_clients[0] = i2c_new_dummy_device(adapter, EE1004_ADDR_SET_PAGE0);
	if (IS_ERR(bus->set_page_clients[0]))
		return PTR_ERR(bus->set_page_clients[0]);

	bus->set_page_clients[1] = i2c_new_dummy_device(adapter, EE1004_ADDR_SET_PAGE1);
	if (IS_ERR(bus->set_page_clients[1])) {
		i2c_unregister_device(bus->set_page_clients[0]);
		return PTR_ERR(bus->set_page_clients[1]);
	}

	bus->page = ee1004_get_current_page(bus);
	if (bus->page < 0) {
		ee1004_bus_unregister(bus);
		return bus->page;
	}

	kref_init(&bus->kref);
	list_add(&bus->list, &ee1004_busses);
	mutex_init(&bus->lock);
	bus->adapter = adapter;

	return 0;
}

static int ee1004_probe(struct i2c_client *client)
{
	struct ee1004_bus *bus;
	bool found = false;
	int rc = 0;

	/* Make sure we can operate on this adapter */
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_READ_I2C_BLOCK) &&
	    !i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -EPFNOSUPPORT;

	mutex_lock(&ee1004_busses_lock);
	list_for_each_entry(bus, &ee1004_busses, list) {
		if (bus->adapter == client->adapter) {
			kref_get(&bus->kref);
			found = true;
			break;
		}
	}

	if (!found) {
		bus = kzalloc(sizeof(*bus), GFP_KERNEL);
		if (!bus) {
			rc = -ENOMEM;
			goto unlock;
		}

		rc = ee1004_bus_initialize(bus, client->adapter);
		if (rc) {
			kfree(bus);
			goto unlock;
		}
	}

	i2c_set_clientdata(client, bus);

	dev_info(&client->dev,
		 "%u byte EE1004-compliant SPD EEPROM, read-only\n",
		 EE1004_EEPROM_SIZE);

unlock:
	mutex_unlock(&ee1004_busses_lock);
	return rc;
}

static void ee1004_remove(struct i2c_client *client)
{
	struct ee1004_bus *bus = i2c_get_clientdata(client);

	kref_put(&bus->kref, ee1004_bus_release);
}

/*-------------------------------------------------------------------------*/

static struct i2c_driver ee1004_driver = {
	.driver = {
		.name = "ee1004",
		.dev_groups = ee1004_groups,
		.of_match_table = ee1004_of_match,
	},
	.probe = ee1004_probe,
	.remove = ee1004_remove,
	.id_table = ee1004_ids,
};
module_i2c_driver(ee1004_driver);

MODULE_DESCRIPTION("Driver for EE1004-compliant DDR4 SPD EEPROMs");
MODULE_AUTHOR("Jean Delvare");
MODULE_LICENSE("GPL");
