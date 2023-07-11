// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  MEN 14F021P00 Board Management Controller (BMC) MFD Core Driver.
 *
 *  Copyright (C) 2014 MEN Mikro Elektronik Nuernberg GmbH
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>

#define BMC_CMD_WDT_EXIT_PROD	0x18
#define BMC_CMD_WDT_PROD_STAT	0x19
#define BMC_CMD_REV_MAJOR	0x80
#define BMC_CMD_REV_MINOR	0x81
#define BMC_CMD_REV_MAIN	0x82

static struct mfd_cell menf21bmc_cell[] = {
	{ .name = "menf21bmc_wdt", },
	{ .name = "menf21bmc_led", },
	{ .name = "menf21bmc_hwmon", }
};

static int menf21bmc_wdt_exit_prod_mode(struct i2c_client *client)
{
	int val, ret;

	val = i2c_smbus_read_byte_data(client, BMC_CMD_WDT_PROD_STAT);
	if (val < 0)
		return val;

	/*
	 * Production mode should be not active after delivery of the Board.
	 * To be sure we check it, inform the user and exit the mode
	 * if active.
	 */
	if (val == 0x00) {
		dev_info(&client->dev,
			"BMC in production mode. Exit production mode\n");

		ret = i2c_smbus_write_byte(client, BMC_CMD_WDT_EXIT_PROD);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
menf21bmc_probe(struct i2c_client *client)
{
	int rev_major, rev_minor, rev_main;
	int ret;

	ret = i2c_check_functionality(client->adapter,
				      I2C_FUNC_SMBUS_BYTE_DATA |
				      I2C_FUNC_SMBUS_WORD_DATA |
				      I2C_FUNC_SMBUS_BYTE);
	if (!ret)
		return -ENODEV;

	rev_major = i2c_smbus_read_word_data(client, BMC_CMD_REV_MAJOR);
	if (rev_major < 0) {
		dev_err(&client->dev, "failed to get BMC major revision\n");
		return rev_major;
	}

	rev_minor = i2c_smbus_read_word_data(client, BMC_CMD_REV_MINOR);
	if (rev_minor < 0) {
		dev_err(&client->dev, "failed to get BMC minor revision\n");
		return rev_minor;
	}

	rev_main = i2c_smbus_read_word_data(client, BMC_CMD_REV_MAIN);
	if (rev_main < 0) {
		dev_err(&client->dev, "failed to get BMC main revision\n");
		return rev_main;
	}

	dev_info(&client->dev, "FW Revision: %02d.%02d.%02d\n",
		 rev_major, rev_minor, rev_main);

	/*
	 * We have to exit the Production Mode of the BMC to activate the
	 * Watchdog functionality and the BIOS life sign monitoring.
	 */
	ret = menf21bmc_wdt_exit_prod_mode(client);
	if (ret < 0) {
		dev_err(&client->dev, "failed to leave production mode\n");
		return ret;
	}

	ret = devm_mfd_add_devices(&client->dev, 0, menf21bmc_cell,
				   ARRAY_SIZE(menf21bmc_cell), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "failed to add BMC sub-devices\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id menf21bmc_id_table[] = {
	{ "menf21bmc" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, menf21bmc_id_table);

static struct i2c_driver menf21bmc_driver = {
	.driver.name	= "menf21bmc",
	.id_table	= menf21bmc_id_table,
	.probe		= menf21bmc_probe,
};

module_i2c_driver(menf21bmc_driver);

MODULE_DESCRIPTION("MEN 14F021P00 BMC mfd core driver");
MODULE_AUTHOR("Andreas Werner <andreas.werner@men.de>");
MODULE_LICENSE("GPL v2");
