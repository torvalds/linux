// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon PXE1610
 *
 * Copyright (c) 2019 Facebook Inc
 *
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define PXE1610_NUM_PAGES 3

/* Identify chip parameters. */
static int pxe1610_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	int i;

	for (i = 0; i < PXE1610_NUM_PAGES; i++) {
		if (pmbus_check_byte_register(client, i, PMBUS_VOUT_MODE)) {
			u8 vout_mode;
			int ret;

			/* Read the register with VOUT scaling value.*/
			ret = pmbus_read_byte_data(client, i, PMBUS_VOUT_MODE);
			if (ret < 0)
				return ret;

			vout_mode = ret & GENMASK(4, 0);

			switch (vout_mode) {
			case 1:
				info->vrm_version[i] = vr12;
				break;
			case 2:
				info->vrm_version[i] = vr13;
				break;
			default:
				return -ENODEV;
			}
		}
	}

	return 0;
}

static struct pmbus_driver_info pxe1610_info = {
	.pages = PXE1610_NUM_PAGES,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN
		| PMBUS_HAVE_VOUT | PMBUS_HAVE_IIN
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_PIN
		| PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.func[1] = PMBUS_HAVE_VIN
		| PMBUS_HAVE_VOUT | PMBUS_HAVE_IIN
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_PIN
		| PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.func[2] = PMBUS_HAVE_VIN
		| PMBUS_HAVE_VOUT | PMBUS_HAVE_IIN
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_PIN
		| PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.identify = pxe1610_identify,
};

static int pxe1610_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	if (!i2c_check_functionality(
			client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE_DATA
			| I2C_FUNC_SMBUS_READ_WORD_DATA
			| I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	/*
	 * By default this device doesn't boot to page 0, so set page 0
	 * to access all pmbus registers.
	 */
	i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);

	/* Read Manufacturer id */
	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read PMBUS_MFR_ID\n");
		return ret;
	}
	if (ret != 2 || strncmp(buf, "XP", 2)) {
		dev_err(&client->dev, "MFR_ID unrecognized\n");
		return -ENODEV;
	}

	info = devm_kmemdup(&client->dev, &pxe1610_info,
			    sizeof(struct pmbus_driver_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id pxe1610_id[] = {
	{"pxe1610", 0},
	{"pxe1110", 0},
	{"pxm1310", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pxe1610_id);

static struct i2c_driver pxe1610_driver = {
	.driver = {
			.name = "pxe1610",
			},
	.probe_new = pxe1610_probe,
	.id_table = pxe1610_id,
};

module_i2c_driver(pxe1610_driver);

MODULE_AUTHOR("Vijay Khemka <vijaykhemka@fb.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon PXE1610, PXE1110 and PXM1310");
MODULE_LICENSE("GPL");
