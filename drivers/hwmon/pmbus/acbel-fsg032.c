// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2023 IBM Corp.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/hwmon-sysfs.h>
#include "pmbus.h"

#define ACBEL_MFR_FW_REVISION	0xd9

static ssize_t acbel_fsg032_debugfs_read(struct file *file, char __user *buf, size_t count,
					 loff_t *ppos)
{
	struct i2c_client *client = file->private_data;
	u8 data[I2C_SMBUS_BLOCK_MAX + 2] = { 0 };
	char out[8];
	int rc;

	rc = i2c_smbus_read_block_data(client, ACBEL_MFR_FW_REVISION, data);
	if (rc < 0)
		return rc;

	rc = snprintf(out, sizeof(out), "%*phN\n", min(rc, 3), data);
	return simple_read_from_buffer(buf, count, ppos, out, rc);
}

static const struct file_operations acbel_debugfs_ops = {
	.llseek = noop_llseek,
	.read = acbel_fsg032_debugfs_read,
	.write = NULL,
	.open = simple_open,
};

static void acbel_fsg032_init_debugfs(struct i2c_client *client)
{
	struct dentry *debugfs = pmbus_get_debugfs_dir(client);

	if (!debugfs)
		return;

	debugfs_create_file("fw_version", 0444, debugfs, client, &acbel_debugfs_ops);
}

static const struct i2c_device_id acbel_fsg032_id[] = {
	{ "acbel_fsg032" },
	{}
};

static struct pmbus_driver_info acbel_fsg032_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN |
		   PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3 |
		   PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_TEMP |
		   PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_FAN12,
};

static int acbel_fsg032_probe(struct i2c_client *client)
{
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	struct device *dev = &client->dev;
	int rc;

	rc = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (rc < 0) {
		dev_err(dev, "Failed to read PMBUS_MFR_ID\n");
		return rc;
	}
	if (strncmp(buf, "ACBEL", 5)) {
		buf[rc] = '\0';
		dev_err(dev, "Manufacturer '%s' not supported\n", buf);
		return -ENODEV;
	}

	rc = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (rc < 0) {
		dev_err(dev, "Failed to read PMBUS_MFR_MODEL\n");
		return rc;
	}

	if (strncmp(buf, "FSG032", 6)) {
		buf[rc] = '\0';
		dev_err(dev, "Model '%s' not supported\n", buf);
		return -ENODEV;
	}

	rc = pmbus_do_probe(client, &acbel_fsg032_info);
	if (rc)
		return rc;

	acbel_fsg032_init_debugfs(client);
	return 0;
}

static const struct of_device_id acbel_fsg032_of_match[] = {
	{ .compatible = "acbel,fsg032" },
	{}
};
MODULE_DEVICE_TABLE(of, acbel_fsg032_of_match);

static struct i2c_driver acbel_fsg032_driver = {
	.driver = {
		.name = "acbel-fsg032",
		.of_match_table = acbel_fsg032_of_match,
	},
	.probe = acbel_fsg032_probe,
	.id_table = acbel_fsg032_id,
};

module_i2c_driver(acbel_fsg032_driver);

MODULE_AUTHOR("Lakshmi Yadlapati");
MODULE_DESCRIPTION("PMBus driver for AcBel Power System power supplies");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
