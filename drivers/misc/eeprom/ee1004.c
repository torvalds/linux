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

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
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

#define EE1004_ADDR_SET_PAGE		0x36
#define EE1004_NUM_PAGES		2
#define EE1004_PAGE_SIZE		256
#define EE1004_PAGE_SHIFT		8
#define EE1004_EEPROM_SIZE		(EE1004_PAGE_SIZE * EE1004_NUM_PAGES)

/*
 * Mutex protects ee1004_set_page and ee1004_dev_count, and must be held
 * from page selection to end of read.
 */
static DEFINE_MUTEX(ee1004_bus_lock);
static struct i2c_client *ee1004_set_page[EE1004_NUM_PAGES];
static unsigned int ee1004_dev_count;
static int ee1004_current_page;

static const struct i2c_device_id ee1004_ids[] = {
	{ "ee1004", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ee1004_ids);

/*-------------------------------------------------------------------------*/

static int ee1004_get_current_page(void)
{
	int err;

	err = i2c_smbus_read_byte(ee1004_set_page[0]);
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

static int ee1004_set_current_page(struct device *dev, int page)
{
	int ret;

	if (page == ee1004_current_page)
		return 0;

	/* Data is ignored */
	ret = i2c_smbus_write_byte(ee1004_set_page[page], 0x00);
	/*
	 * Don't give up just yet. Some memory modules will select the page
	 * but not ack the command. Check which page is selected now.
	 */
	if (ret == -ENXIO && ee1004_get_current_page() == page)
		ret = 0;
	if (ret < 0) {
		dev_err(dev, "Failed to select page %d (%d)\n", page, ret);
		return ret;
	}

	dev_dbg(dev, "Selected page %d\n", page);
	ee1004_current_page = page;

	return 0;
}

static ssize_t ee1004_eeprom_read(struct i2c_client *client, char *buf,
				  unsigned int offset, size_t count)
{
	int status, page;

	page = offset >> EE1004_PAGE_SHIFT;
	offset &= (1 << EE1004_PAGE_SHIFT) - 1;

	status = ee1004_set_current_page(&client->dev, page);
	if (status)
		return status;

	/* Can't cross page boundaries */
	if (offset + count > EE1004_PAGE_SIZE)
		count = EE1004_PAGE_SIZE - offset;

	return i2c_smbus_read_i2c_block_data_or_emulated(client, offset, count, buf);
}

static ssize_t eeprom_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);
	size_t requested = count;
	int ret = 0;

	/*
	 * Read data from chip, protecting against concurrent access to
	 * other EE1004 SPD EEPROMs on the same adapter.
	 */
	mutex_lock(&ee1004_bus_lock);

	while (count) {
		ret = ee1004_eeprom_read(client, buf, off, count);
		if (ret < 0)
			goto out;

		buf += ret;
		off += ret;
		count -= ret;
	}
out:
	mutex_unlock(&ee1004_bus_lock);

	return ret < 0 ? ret : requested;
}

static BIN_ATTR_RO(eeprom, EE1004_EEPROM_SIZE);

static struct bin_attribute *ee1004_attrs[] = {
	&bin_attr_eeprom,
	NULL
};

BIN_ATTRIBUTE_GROUPS(ee1004);

static void ee1004_cleanup(int idx)
{
	if (--ee1004_dev_count == 0)
		while (--idx >= 0) {
			i2c_unregister_device(ee1004_set_page[idx]);
			ee1004_set_page[idx] = NULL;
		}
}

static int ee1004_probe(struct i2c_client *client)
{
	int err, cnr = 0;

	/* Make sure we can operate on this adapter */
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_READ_I2C_BLOCK) &&
	    !i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -EPFNOSUPPORT;

	/* Use 2 dummy devices for page select command */
	mutex_lock(&ee1004_bus_lock);
	if (++ee1004_dev_count == 1) {
		for (cnr = 0; cnr < EE1004_NUM_PAGES; cnr++) {
			struct i2c_client *cl;

			cl = i2c_new_dummy_device(client->adapter, EE1004_ADDR_SET_PAGE + cnr);
			if (IS_ERR(cl)) {
				err = PTR_ERR(cl);
				goto err_clients;
			}
			ee1004_set_page[cnr] = cl;
		}

		/* Remember current page to avoid unneeded page select */
		err = ee1004_get_current_page();
		if (err < 0)
			goto err_clients;
		dev_dbg(&client->dev, "Currently selected page: %d\n", err);
		ee1004_current_page = err;
	} else if (client->adapter != ee1004_set_page[0]->adapter) {
		dev_err(&client->dev,
			"Driver only supports devices on a single I2C bus\n");
		err = -EOPNOTSUPP;
		goto err_clients;
	}
	mutex_unlock(&ee1004_bus_lock);

	dev_info(&client->dev,
		 "%u byte EE1004-compliant SPD EEPROM, read-only\n",
		 EE1004_EEPROM_SIZE);

	return 0;

 err_clients:
	ee1004_cleanup(cnr);
	mutex_unlock(&ee1004_bus_lock);

	return err;
}

static int ee1004_remove(struct i2c_client *client)
{
	/* Remove page select clients if this is the last device */
	mutex_lock(&ee1004_bus_lock);
	ee1004_cleanup(EE1004_NUM_PAGES);
	mutex_unlock(&ee1004_bus_lock);

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct i2c_driver ee1004_driver = {
	.driver = {
		.name = "ee1004",
		.dev_groups = ee1004_groups,
	},
	.probe_new = ee1004_probe,
	.remove = ee1004_remove,
	.id_table = ee1004_ids,
};
module_i2c_driver(ee1004_driver);

MODULE_DESCRIPTION("Driver for EE1004-compliant DDR4 SPD EEPROMs");
MODULE_AUTHOR("Jean Delvare");
MODULE_LICENSE("GPL");
