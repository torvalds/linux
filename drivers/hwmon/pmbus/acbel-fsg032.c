// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2023 IBM Corp.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/hwmon-sysfs.h>
#include "pmbus.h"

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
	.probe_new = acbel_fsg032_probe,
	.id_table = acbel_fsg032_id,
};

module_i2c_driver(acbel_fsg032_driver);

MODULE_AUTHOR("Lakshmi Yadlapati");
MODULE_DESCRIPTION("PMBus driver for AcBel Power System power supplies");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
