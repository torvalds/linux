// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Texas Instruments TPS53679
 *
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Vadim Pasternak <vadimp@mellanox.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define TPS53679_PROT_VR12_5MV		0x01 /* VR12.0 mode, 5-mV DAC */
#define TPS53679_PROT_VR12_5_10MV	0x02 /* VR12.5 mode, 10-mV DAC */
#define TPS53679_PROT_VR13_10MV		0x04 /* VR13.0 mode, 10-mV DAC */
#define TPS53679_PROT_IMVP8_5MV		0x05 /* IMVP8 mode, 5-mV DAC */
#define TPS53679_PROT_VR13_5MV		0x07 /* VR13.0 mode, 5-mV DAC */
#define TPS53679_PAGE_NUM		2

static int tps53679_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	u8 vout_params;
	int ret;

	/* Read the register with VOUT scaling value.*/
	ret = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	vout_params = ret & GENMASK(4, 0);

	switch (vout_params) {
	case TPS53679_PROT_VR13_10MV:
	case TPS53679_PROT_VR12_5_10MV:
		info->vrm_version = vr13;
		break;
	case TPS53679_PROT_VR13_5MV:
	case TPS53679_PROT_VR12_5MV:
	case TPS53679_PROT_IMVP8_5MV:
		info->vrm_version = vr12;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct pmbus_driver_info tps53679_info = {
	.pages = TPS53679_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT,
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT,
	.identify = tps53679_identify,
};

static int tps53679_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct pmbus_driver_info *info;

	info = devm_kmemdup(&client->dev, &tps53679_info, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id tps53679_id[] = {
	{"tps53679", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, tps53679_id);

static const struct of_device_id __maybe_unused tps53679_of_match[] = {
	{.compatible = "ti,tps53679"},
	{}
};
MODULE_DEVICE_TABLE(of, tps53679_of_match);

static struct i2c_driver tps53679_driver = {
	.driver = {
		.name = "tps53679",
		.of_match_table = of_match_ptr(tps53679_of_match),
	},
	.probe = tps53679_probe,
	.remove = pmbus_do_remove,
	.id_table = tps53679_id,
};

module_i2c_driver(tps53679_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("PMBus driver for Texas Instruments TPS53679");
MODULE_LICENSE("GPL");
