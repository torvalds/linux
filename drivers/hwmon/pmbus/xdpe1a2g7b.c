// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon Multi-phase Digital XDPE1A2G5B
 * and XDPE1A2G7B Controllers
 *
 * Copyright (c) 2026 Infineon Technologies. All rights reserved.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define XDPE1A2G7B_PAGE_NUM 2
#define XDPE1A2G7B_NVIDIA_195MV 0x1E /* NVIDIA mode 1.95mV, VID step is 5mV */

static int xdpe1a2g7b_identify(struct i2c_client *client,
			       struct pmbus_driver_info *info)
{
	u8 vout_params;
	int vout_mode;

	/*
	 * XDPE1A2G5B and XDPE1A2G7B support both Linear and NVIDIA PWM VID data
	 * formats via VOUT_MODE. Note that the device pages/loops are not fully
	 * independent: configuration is shared, so programming each page/loop
	 * separately is not supported.
	 */
	vout_mode = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
	if (vout_mode < 0)
		return vout_mode;

	switch (vout_mode >> 5) {
	case 0:
		info->format[PSC_VOLTAGE_OUT] = linear;
		return 0;
	case 1:
		info->format[PSC_VOLTAGE_OUT] = vid;
		vout_params = vout_mode & GENMASK(4, 0);
		/* Check for VID Code Type */
		switch (vout_params) {
		case XDPE1A2G7B_NVIDIA_195MV:
			/* VID vrm_version for PAGE0 and PAGE1 */
			info->vrm_version[0] = nvidia195mv;
			info->vrm_version[1] = nvidia195mv;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static struct pmbus_driver_info xdpe1a2g7b_info = {
	.pages = XDPE1A2G7B_PAGE_NUM,
	.identify = xdpe1a2g7b_identify,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP |
		   PMBUS_HAVE_POUT | PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT,
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		   PMBUS_HAVE_PIN | PMBUS_HAVE_POUT | PMBUS_HAVE_STATUS_INPUT,
};

static int xdpe1a2g7b_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;

	info = devm_kmemdup(&client->dev, &xdpe1a2g7b_info, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id xdpe1a2g7b_id[] = {
	{ "xdpe1a2g5b" },
	{ "xdpe1a2g7b" },
	{}
};

MODULE_DEVICE_TABLE(i2c, xdpe1a2g7b_id);

static const struct of_device_id __maybe_unused xdpe1a2g7b_of_match[] = {
	{ .compatible = "infineon,xdpe1a2g5b" },
	{ .compatible = "infineon,xdpe1a2g7b" },
	{}
};

MODULE_DEVICE_TABLE(of, xdpe1a2g7b_of_match);

static struct i2c_driver xdpe1a2g7b_driver = {
	.driver = {
		.name = "xdpe1a2g7b",
		.of_match_table = of_match_ptr(xdpe1a2g7b_of_match),
	},
	.probe = xdpe1a2g7b_probe,
	.id_table = xdpe1a2g7b_id,
};

module_i2c_driver(xdpe1a2g7b_driver);

MODULE_AUTHOR("Ashish Yadav <ashish.yadav@infineon.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon XDPE1A2G5B/7B");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
