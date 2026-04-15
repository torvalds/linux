// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon XDP720 Digital eFuse Controller
 *
 * Copyright (c) 2026 Infineon Technologies. All rights reserved.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include "pmbus.h"

/*
 * The IMON resistor required to generate the system overcurrent protection.
 * Arbitrary default Rimon value: 2k Ohm
 */
#define XDP720_DEFAULT_RIMON 2000000000 /* 2k ohm */
#define XDP720_TELEMETRY_AVG 0xE9

static struct pmbus_driver_info xdp720_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,

	.m[PSC_VOLTAGE_IN] = 4653,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = -2,
	.m[PSC_VOLTAGE_OUT] = 4653,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = -2,
	/*
	 * Current and Power measurement depends on the RIMON (kOhm) and
	 * GIMON(microA/A) values.
	 */
	.m[PSC_CURRENT_OUT] = 24668,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = -4,
	.m[PSC_POWER] = 4486,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -1,
	.m[PSC_TEMPERATURE] = 54,
	.b[PSC_TEMPERATURE] = 22521,
	.R[PSC_TEMPERATURE] = -1,

	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_PIN |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_INPUT |
		   PMBUS_HAVE_STATUS_TEMP,
};

static int xdp720_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	int ret;
	u32 rimon;
	int gimon;

	info = devm_kmemdup(&client->dev, &xdp720_info, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = devm_regulator_get_enable(&client->dev, "vdd-vin");
	if (ret)
		return dev_err_probe(&client->dev, ret,
			"failed to enable vdd-vin supply\n");

	ret = i2c_smbus_read_word_data(client, XDP720_TELEMETRY_AVG);
	if (ret < 0) {
		dev_err(&client->dev, "Can't get TELEMETRY_AVG\n");
		return ret;
	}

	ret >>= 10; /* 10th bit of TELEMETRY_AVG REG for GIMON Value */
	ret &= GENMASK(0, 0);
	if (ret == 1)
		gimon = 18200; /* output gain 18.2 microA/A */
	else
		gimon = 9100; /* output gain 9.1 microA/A */

	if (of_property_read_u32(client->dev.of_node,
				 "infineon,rimon-micro-ohms", &rimon))
		rimon = XDP720_DEFAULT_RIMON; /* Default if not set via DT */
	if (rimon == 0)
		return -EINVAL;

	/* Adapt the current and power scale for each instance */
	info->m[PSC_CURRENT_OUT] = DIV64_U64_ROUND_CLOSEST((u64)
		info->m[PSC_CURRENT_OUT] * rimon * gimon, 1000000000000ULL);
	info->m[PSC_POWER] = DIV64_U64_ROUND_CLOSEST((u64)
		info->m[PSC_POWER] * rimon * gimon, 1000000000000000ULL);

	return pmbus_do_probe(client, info);
}

static const struct of_device_id xdp720_of_match[] = {
	{ .compatible = "infineon,xdp720" },
	{}
};
MODULE_DEVICE_TABLE(of, xdp720_of_match);

static const struct i2c_device_id xdp720_id[] = {
	{ "xdp720" },
	{}
};
MODULE_DEVICE_TABLE(i2c, xdp720_id);

static struct i2c_driver xdp720_driver = {
	.driver = {
		   .name = "xdp720",
		   .of_match_table = xdp720_of_match,
	},
	.probe = xdp720_probe,
	.id_table = xdp720_id,
};

module_i2c_driver(xdp720_driver);

MODULE_AUTHOR("Ashish Yadav <ashish.yadav@infineon.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon XDP720 Digital eFuse Controller");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
