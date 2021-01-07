// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for ucd9200 series Digital PWM System Controllers
 *
 * Copyright (C) 2011 Ericsson AB.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define UCD9200_PHASE_INFO	0xd2
#define UCD9200_DEVICE_ID	0xfd

enum chips { ucd9200, ucd9220, ucd9222, ucd9224, ucd9240, ucd9244, ucd9246,
	     ucd9248 };

static const struct i2c_device_id ucd9200_id[] = {
	{"ucd9200", ucd9200},
	{"ucd9220", ucd9220},
	{"ucd9222", ucd9222},
	{"ucd9224", ucd9224},
	{"ucd9240", ucd9240},
	{"ucd9244", ucd9244},
	{"ucd9246", ucd9246},
	{"ucd9248", ucd9248},
	{}
};
MODULE_DEVICE_TABLE(i2c, ucd9200_id);

static const struct of_device_id __maybe_unused ucd9200_of_match[] = {
	{
		.compatible = "ti,cd9200",
		.data = (void *)ucd9200
	},
	{
		.compatible = "ti,cd9220",
		.data = (void *)ucd9220
	},
	{
		.compatible = "ti,cd9222",
		.data = (void *)ucd9222
	},
	{
		.compatible = "ti,cd9224",
		.data = (void *)ucd9224
	},
	{
		.compatible = "ti,cd9240",
		.data = (void *)ucd9240
	},
	{
		.compatible = "ti,cd9244",
		.data = (void *)ucd9244
	},
	{
		.compatible = "ti,cd9246",
		.data = (void *)ucd9246
	},
	{
		.compatible = "ti,cd9248",
		.data = (void *)ucd9248
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ucd9200_of_match);

static int ucd9200_probe(struct i2c_client *client)
{
	u8 block_buffer[I2C_SMBUS_BLOCK_MAX + 1];
	struct pmbus_driver_info *info;
	const struct i2c_device_id *mid;
	enum chips chip;
	int i, j, ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, UCD9200_DEVICE_ID,
					block_buffer);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read device ID\n");
		return ret;
	}
	block_buffer[ret] = '\0';
	dev_info(&client->dev, "Device ID %s\n", block_buffer);

	for (mid = ucd9200_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, block_buffer, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	if (client->dev.of_node)
		chip = (enum chips)of_device_get_match_data(&client->dev);
	else
		chip = mid->driver_data;

	if (chip != ucd9200 && strcmp(client->name, mid->name) != 0)
		dev_notice(&client->dev,
			   "Device mismatch: Configured %s, detected %s\n",
			   client->name, mid->name);

	info = devm_kzalloc(&client->dev, sizeof(struct pmbus_driver_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = i2c_smbus_read_block_data(client, UCD9200_PHASE_INFO,
					block_buffer);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read phase information\n");
		return ret;
	}

	/*
	 * Calculate number of configured pages (rails) from PHASE_INFO
	 * register.
	 * Rails have to be sequential, so we can abort after finding
	 * the first unconfigured rail.
	 */
	info->pages = 0;
	for (i = 0; i < ret; i++) {
		if (!block_buffer[i])
			break;
		info->pages++;
	}
	if (!info->pages) {
		dev_err(&client->dev, "No rails configured\n");
		return -ENODEV;
	}
	dev_info(&client->dev, "%d rails configured\n", info->pages);

	/*
	 * Set PHASE registers on all pages to 0xff to ensure that phase
	 * specific commands will apply to all phases of a given page (rail).
	 * This only affects the READ_IOUT and READ_TEMPERATURE2 registers.
	 * READ_IOUT will return the sum of currents of all phases of a rail,
	 * and READ_TEMPERATURE2 will return the maximum temperature detected
	 * for the the phases of the rail.
	 */
	for (i = 0; i < info->pages; i++) {
		/*
		 * Setting PAGE & PHASE fails once in a while for no obvious
		 * reason, so we need to retry a couple of times.
		 */
		for (j = 0; j < 3; j++) {
			ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
			if (ret < 0)
				continue;
			ret = i2c_smbus_write_byte_data(client, PMBUS_PHASE,
							0xff);
			if (ret < 0)
				continue;
			break;
		}
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed to initialize PHASE registers\n");
			return ret;
		}
	}
	if (info->pages > 1)
		i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);

	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT |
			PMBUS_HAVE_IIN | PMBUS_HAVE_PIN |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP |
			PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP;

	for (i = 1; i < info->pages; i++)
		info->func[i] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_POUT |
			PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP;

	/* ucd9240 supports a single fan */
	if (mid->driver_data == ucd9240)
		info->func[0] |= PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12;

	return pmbus_do_probe(client, info);
}

/* This is the driver that will be inserted */
static struct i2c_driver ucd9200_driver = {
	.driver = {
		.name = "ucd9200",
		.of_match_table = of_match_ptr(ucd9200_of_match),
	},
	.probe_new = ucd9200_probe,
	.id_table = ucd9200_id,
};

module_i2c_driver(ucd9200_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for TI UCD922x, UCD924x");
MODULE_LICENSE("GPL");
