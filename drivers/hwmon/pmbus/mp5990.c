// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MP5990 Hot-Swap Controller
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define MP5990_EFUSE_CFG	(0xC4)
#define MP5990_VOUT_FORMAT	BIT(9)

struct mp5990_data {
	struct pmbus_driver_info info;
	u8 vout_mode;
	u8 vout_linear_exponent;
};

#define to_mp5990_data(x)  container_of(x, struct mp5990_data, info)

static int mp5990_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp5990_data *data = to_mp5990_data(info);

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

		/*
		 * The datasheet does not support the VOUT command,
		 * but the device responds with a default value of 0x17.
		 * In the standard, 0x17 represents linear mode.
		 * Therefore, we should report that VOUT is in direct
		 * format when the chip is configured for it.
		 */
		return PB_VOUT_MODE_DIRECT;

	default:
		return -ENODATA;
	}
}

static int mp5990_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp5990_data *data = to_mp5990_data(info);
	int ret;
	s32 mantissa;

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
		if (data->vout_mode == linear) {
			mantissa = ((s16)((ret & 0x7ff) << 5)) >> 5;
			ret = mantissa;
		}
		break;
	default:
		return -ENODATA;
	}

	return ret;
}

static struct pmbus_driver_info mp5990_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 32,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 0,
	.m[PSC_VOLTAGE_OUT] = 32,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,
	.m[PSC_CURRENT_OUT] = 16,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 0,
	.m[PSC_POWER] = 1,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = 0,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] =
		PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_PIN |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.read_byte_data = mp5990_read_byte_data,
	.read_word_data = mp5990_read_word_data,
};

static int mp5990_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	struct mp5990_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(struct mp5990_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp5990_info, sizeof(*info));
	info = &data->info;

	/* Read Vout Config */
	ret = i2c_smbus_read_word_data(client, MP5990_EFUSE_CFG);
	if (ret < 0) {
		dev_err(&client->dev, "Can't get vout mode.");
		return ret;
	}

	/*
	 * EFUSE_CFG (0xC4) bit9=1 is linear mode, bit=0 is direct mode.
	 */
	if (ret & MP5990_VOUT_FORMAT) {
		data->vout_mode = linear;
		data->info.format[PSC_VOLTAGE_IN] = linear;
		data->info.format[PSC_VOLTAGE_OUT] = linear;
		data->info.format[PSC_CURRENT_OUT] = linear;
		data->info.format[PSC_POWER] = linear;
		ret = i2c_smbus_read_word_data(client, PMBUS_READ_VOUT);
		if (ret < 0) {
			dev_err(&client->dev, "Can't get vout exponent.");
			return ret;
		}
		data->vout_linear_exponent = (u8)((ret >> 11) & 0x1f);
	} else {
		data->vout_mode = direct;
	}
	return pmbus_do_probe(client, info);
}

static const struct of_device_id mp5990_of_match[] = {
	{ .compatible = "mps,mp5990" },
	{}
};

static const struct i2c_device_id mp5990_id[] = {
	{"mp5990", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, mp5990_id);

static struct i2c_driver mp5990_driver = {
	.driver = {
		   .name = "mp5990",
		   .of_match_table = mp5990_of_match,
	},
	.probe = mp5990_probe,
	.id_table = mp5990_id,
};
module_i2c_driver(mp5990_driver);

MODULE_AUTHOR("Peter Yin <peter.yin@quantatw.com>");
MODULE_DESCRIPTION("PMBus driver for MP5990 HSC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
