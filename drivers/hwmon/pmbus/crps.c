// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2024 IBM Corp.
 */

#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/pmbus.h>

#include "pmbus.h"

static const struct i2c_device_id crps_id[] = {
	{ "intel_crps185" },
	{}
};
MODULE_DEVICE_TABLE(i2c, crps_id);

static struct pmbus_driver_info crps_info = {
	.pages = 1,
	/* PSU uses default linear data format. */
	.func[0] = PMBUS_HAVE_PIN | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_IIN |
		PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
		PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12,
};

static int crps_probe(struct i2c_client *client)
{
	int rc;
	struct device *dev = &client->dev;
	char buf[I2C_SMBUS_BLOCK_MAX + 2] = { 0 };

	rc = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (rc < 0)
		return dev_err_probe(dev, rc, "Failed to read PMBUS_MFR_MODEL\n");

	if (rc != 7 || strncmp(buf, "03NK260", 7)) {
		buf[rc] = '\0';
		return dev_err_probe(dev, -ENODEV, "Model '%s' not supported\n", buf);
	}

	rc = pmbus_do_probe(client, &crps_info);
	if (rc)
		return dev_err_probe(dev, rc, "Failed to probe\n");

	return 0;
}

static const struct of_device_id crps_of_match[] = {
	{
		.compatible = "intel,crps185",
	},
	{}
};
MODULE_DEVICE_TABLE(of, crps_of_match);

static struct i2c_driver crps_driver = {
	.driver = {
		.name = "crps",
		.of_match_table = crps_of_match,
	},
	.probe = crps_probe,
	.id_table = crps_id,
};

module_i2c_driver(crps_driver);

MODULE_AUTHOR("Ninad Palsule");
MODULE_DESCRIPTION("PMBus driver for Intel Common Redundant power supplies");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
