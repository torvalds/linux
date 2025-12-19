// SPDX-License-Identifier: GPL-2.0+
/*
 * mp5926.c  - pmbus driver for mps mp5926
 *
 * Copyright 2025 Monolithic Power Systems, Inc
 *
 * Author: Yuxi Wang <Yuxi.Wang@monolithicpower.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define PAGE	0x01
#define EFUSE_CFG	0xCF
#define I_SCALE_SEL	0xC6
#define MP5926_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
			PMBUS_HAVE_IIN | PMBUS_HAVE_PIN | \
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_INPUT | \
			PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_VOUT)

struct mp5926_data {
	struct pmbus_driver_info info;
	u8 vout_mode;
	u8 vout_linear_exponent;
};

#define to_mp5926_data(x)  container_of(x, struct mp5926_data, info)

static int mp5926_read_byte_data(struct i2c_client *client, int page,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp5926_data *data = to_mp5926_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		if (data->vout_mode == linear) {
			/*
			 * The VOUT format used by the chip is linear11,
			 * not linear16. Report that VOUT is in linear mode
			 * and return exponent value extracted while probing
			 * the chip.
			 */
			return data->vout_linear_exponent;
		}
		return PB_VOUT_MODE_DIRECT;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int mp5926_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp5926_data *data = to_mp5926_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;
		/*
		 * Because the VOUT format used by the chip is linear11 and not
		 * linear16, we disregard bits[15:11]. The exponent is reported
		 * as part of the VOUT_MODE command.
		 */
		if (data->vout_mode == linear)
			ret = ((s16)((ret & 0x7ff) << 5)) >> 5;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static struct pmbus_driver_info mp5926_info = {
	.pages = PAGE,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_POWER] = direct,

	.m[PSC_VOLTAGE_IN] = 16,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 0,

	.m[PSC_CURRENT_IN] = 16,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 0,

	.m[PSC_VOLTAGE_OUT] = 16,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,

	.m[PSC_TEMPERATURE] = 4,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,

	.m[PSC_POWER] = 25,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -2,

	.read_word_data = mp5926_read_word_data,
	.read_byte_data = mp5926_read_byte_data,
	.func[0] = MP5926_FUNC,
};

static int mp5926_probe(struct i2c_client *client)
{
	struct mp5926_data *data;
	struct pmbus_driver_info *info;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(struct mp5926_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp5926_info, sizeof(*info));
	info = &data->info;
	ret = i2c_smbus_read_word_data(client, EFUSE_CFG);
	if (ret < 0)
		return ret;
	if (ret & BIT(12)) {
		data->vout_mode = linear;
		data->info.format[PSC_VOLTAGE_IN] = linear;
		data->info.format[PSC_CURRENT_IN] = linear;
		data->info.format[PSC_VOLTAGE_OUT] = linear;
		data->info.format[PSC_TEMPERATURE] = linear;
		data->info.format[PSC_POWER] = linear;
		ret = i2c_smbus_read_word_data(client, PMBUS_READ_VOUT);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret, "Can't get vout exponent.");
		data->vout_linear_exponent = (u8)((ret >> 11) & 0x1f);
	} else {
		data->vout_mode = direct;
		ret = i2c_smbus_read_word_data(client, I_SCALE_SEL);
		if (ret < 0)
			return ret;
		if (ret & BIT(6))
			data->info.m[PSC_CURRENT_IN] = 4;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id mp5926_id[] = {
	{ "mp5926", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, mp5926_id);

static const struct of_device_id mp5926_of_match[] = {
	{ .compatible = "mps,mp5926" },
	{}
};
MODULE_DEVICE_TABLE(of, mp5926_of_match);

static struct i2c_driver mp5926_driver = {
	.probe = mp5926_probe,
	.driver = {
			.name = "mp5926",
			.of_match_table = mp5926_of_match,
		   },
	.id_table = mp5926_id,
};

module_i2c_driver(mp5926_driver);
MODULE_AUTHOR("Yuxi Wang <Yuxi.Wang@monolithicpower.com>");
MODULE_DESCRIPTION("MPS MP5926 pmbus driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
