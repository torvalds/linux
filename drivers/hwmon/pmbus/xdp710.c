// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Infineon XDP710 Hot-Swap Controller
 */

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define XDP710_REG_CFG		0xD3
#define XDP710_V_SNS_CFG	0xD4
#define XDP710_CS_RNG		0xD5

/*
 * The table to map configuration register values
 * to sense resistor values
 */
static const int micro_ohm_rsense[] = {
	200, 250, 300, 330, 400, 470, 500, 600,
	670, 700, 750, 800, 900, 1000, 1100, 1200,
	1250, 1300, 1400, 1500, 1600, 1700, 1800, 1900,
	2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700,
	2800, 3000, 3100, 3200, 3300, 3400, 3500, 3600,
	3700, 3800, 3900, 4000, 4100, 4200, 4300, 4400,
	4500, 4600, 4700, 4800, 4900, 5000, 5500, 6000,
	6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000
};

static struct pmbus_driver_info xdp710_info = {
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
	.m[PSC_CURRENT_OUT] = 23165,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = -2,
	.m[PSC_POWER] = 4211,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -2,
	.m[PSC_TEMPERATURE] = 52,
	.b[PSC_TEMPERATURE] = 14321,
	.R[PSC_TEMPERATURE] = -1,
	.func[0] =
		PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_PIN |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
};

static int xdp710_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	u8 cs_rng;
	u8 vtlm_rng;
	int rsense;
	int ret;
	int m = 0;

	info = devm_kmemdup(&client->dev, &xdp710_info, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = i2c_smbus_read_word_data(client, XDP710_CS_RNG);
	if (ret < 0) {
		dev_err(&client->dev, "Can't get CS_RNG");
		return ret;
	}
	cs_rng = (ret >> 6) & GENMASK(1, 0);

	ret = i2c_smbus_read_word_data(client, XDP710_V_SNS_CFG);
	if (ret < 0) {
		dev_err(&client->dev, "Can't get V_SNS_CFG");
		return ret;
	}
	vtlm_rng = ret & GENMASK(1, 0);

	ret = i2c_smbus_read_word_data(client, XDP710_REG_CFG);
	if (ret < 0) {
		dev_err(&client->dev, "Can't get REG_CFG");
		return ret;
	}
	ret &= GENMASK(5, 0);
	rsense = micro_ohm_rsense[ret];

	info->m[PSC_VOLTAGE_IN] <<= vtlm_rng;
	info->m[PSC_VOLTAGE_OUT] <<= vtlm_rng;

	m = info->m[PSC_CURRENT_OUT];
	info->m[PSC_CURRENT_OUT] = DIV_ROUND_CLOSEST(m * rsense >> cs_rng, 1000);

	m = info->m[PSC_POWER];
	info->m[PSC_POWER] = DIV_ROUND_CLOSEST(m * rsense >> cs_rng, 1000);

	return pmbus_do_probe(client, info);
}

static const struct of_device_id xdp710_of_match[] = {
	{ .compatible = "infineon,xdp710" },
	{}
};

static const struct i2c_device_id xdp710_id[] = {
	{"xdp710"},
	{ }
};
MODULE_DEVICE_TABLE(i2c, xdp710_id);

static struct i2c_driver xdp710_driver = {
	.driver = {
		   .name = "xdp710",
		   .of_match_table = xdp710_of_match,
	},
	.probe = xdp710_probe,
	.id_table = xdp710_id,
};
module_i2c_driver(xdp710_driver);

MODULE_AUTHOR("Peter Yin <peter.yin@quantatw.com>");
MODULE_DESCRIPTION("PMBus driver for XDP710 HSC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
