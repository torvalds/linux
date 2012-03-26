/*
 * Hardware monitoring driver for LM25066 / LM5064 / LM5066
 *
 * Copyright (c) 2011 Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "pmbus.h"

enum chips { lm25066, lm5064, lm5066 };

#define LM25066_READ_VAUX		0xd0
#define LM25066_MFR_READ_IIN		0xd1
#define LM25066_MFR_READ_PIN		0xd2
#define LM25066_MFR_IIN_OC_WARN_LIMIT	0xd3
#define LM25066_MFR_PIN_OP_WARN_LIMIT	0xd4
#define LM25066_READ_PIN_PEAK		0xd5
#define LM25066_CLEAR_PIN_PEAK		0xd6
#define LM25066_DEVICE_SETUP		0xd9
#define LM25066_READ_AVG_VIN		0xdc
#define LM25066_READ_AVG_VOUT		0xdd
#define LM25066_READ_AVG_IIN		0xde
#define LM25066_READ_AVG_PIN		0xdf

#define LM25066_DEV_SETUP_CL		(1 << 4)	/* Current limit */

struct lm25066_data {
	int id;
	struct pmbus_driver_info info;
};

#define to_lm25066_data(x)  container_of(x, struct lm25066_data, info)

static int lm25066_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct lm25066_data *data = to_lm25066_data(info);
	int ret;

	if (page > 1)
		return -ENXIO;

	/* Map READ_VAUX into READ_VOUT register on page 1 */
	if (page == 1) {
		switch (reg) {
		case PMBUS_READ_VOUT:
			ret = pmbus_read_word_data(client, 0,
						   LM25066_READ_VAUX);
			if (ret < 0)
				break;
			/* Adjust returned value to match VOUT coefficients */
			switch (data->id) {
			case lm25066:
				/* VOUT: 4.54 mV VAUX: 283.2 uV LSB */
				ret = DIV_ROUND_CLOSEST(ret * 2832, 45400);
				break;
			case lm5064:
				/* VOUT: 4.53 mV VAUX: 700 uV LSB */
				ret = DIV_ROUND_CLOSEST(ret * 70, 453);
				break;
			case lm5066:
				/* VOUT: 2.18 mV VAUX: 725 uV LSB */
				ret = DIV_ROUND_CLOSEST(ret * 725, 2180);
				break;
			}
			break;
		default:
			/* No other valid registers on page 1 */
			ret = -ENXIO;
			break;
		}
		goto done;
	}

	switch (reg) {
	case PMBUS_READ_IIN:
		ret = pmbus_read_word_data(client, 0, LM25066_MFR_READ_IIN);
		break;
	case PMBUS_READ_PIN:
		ret = pmbus_read_word_data(client, 0, LM25066_MFR_READ_PIN);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0,
					   LM25066_MFR_IIN_OC_WARN_LIMIT);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0,
					   LM25066_MFR_PIN_OP_WARN_LIMIT);
		break;
	case PMBUS_VIRT_READ_VIN_AVG:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_AVG_VIN);
		break;
	case PMBUS_VIRT_READ_VOUT_AVG:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_AVG_VOUT);
		break;
	case PMBUS_VIRT_READ_IIN_AVG:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_AVG_IIN);
		break;
	case PMBUS_VIRT_READ_PIN_AVG:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_AVG_PIN);
		break;
	case PMBUS_VIRT_READ_PIN_MAX:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_PIN_PEAK);
		break;
	case PMBUS_VIRT_RESET_PIN_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
done:
	return ret;
}

static int lm25066_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	int ret;

	if (page > 1)
		return -ENXIO;

	switch (reg) {
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_IIN_OC_WARN_LIMIT,
					    word);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_PIN_OP_WARN_LIMIT,
					    word);
		break;
	case PMBUS_VIRT_RESET_PIN_HISTORY:
		ret = pmbus_write_byte(client, 0, LM25066_CLEAR_PIN_PEAK);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int lm25066_write_byte(struct i2c_client *client, int page, u8 value)
{
	if (page > 1)
		return -ENXIO;

	if (page <= 0)
		return pmbus_write_byte(client, page, value);

	return 0;
}

static int lm25066_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int config;
	struct lm25066_data *data;
	struct pmbus_driver_info *info;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct lm25066_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	config = i2c_smbus_read_byte_data(client, LM25066_DEVICE_SETUP);
	if (config < 0)
		return config;

	data->id = id->driver_data;
	info = &data->info;

	info->pages = 2;
	info->format[PSC_VOLTAGE_IN] = direct;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->format[PSC_CURRENT_IN] = direct;
	info->format[PSC_TEMPERATURE] = direct;
	info->format[PSC_POWER] = direct;

	info->m[PSC_TEMPERATURE] = 16;
	info->b[PSC_TEMPERATURE] = 0;
	info->R[PSC_TEMPERATURE] = 0;

	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT
	  | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_PIN | PMBUS_HAVE_IIN
	  | PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;
	info->func[1] = PMBUS_HAVE_VOUT;

	info->read_word_data = lm25066_read_word_data;
	info->write_word_data = lm25066_write_word_data;
	info->write_byte = lm25066_write_byte;

	switch (id->driver_data) {
	case lm25066:
		info->m[PSC_VOLTAGE_IN] = 22070;
		info->b[PSC_VOLTAGE_IN] = 0;
		info->R[PSC_VOLTAGE_IN] = -2;
		info->m[PSC_VOLTAGE_OUT] = 22070;
		info->b[PSC_VOLTAGE_OUT] = 0;
		info->R[PSC_VOLTAGE_OUT] = -2;

		if (config & LM25066_DEV_SETUP_CL) {
			info->m[PSC_CURRENT_IN] = 6852;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 369;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -2;
		} else {
			info->m[PSC_CURRENT_IN] = 13661;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 736;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -2;
		}
		break;
	case lm5064:
		info->m[PSC_VOLTAGE_IN] = 22075;
		info->b[PSC_VOLTAGE_IN] = 0;
		info->R[PSC_VOLTAGE_IN] = -2;
		info->m[PSC_VOLTAGE_OUT] = 22075;
		info->b[PSC_VOLTAGE_OUT] = 0;
		info->R[PSC_VOLTAGE_OUT] = -2;

		if (config & LM25066_DEV_SETUP_CL) {
			info->m[PSC_CURRENT_IN] = 6713;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 3619;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -3;
		} else {
			info->m[PSC_CURRENT_IN] = 13426;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 7238;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -3;
		}
		break;
	case lm5066:
		info->m[PSC_VOLTAGE_IN] = 4587;
		info->b[PSC_VOLTAGE_IN] = 0;
		info->R[PSC_VOLTAGE_IN] = -2;
		info->m[PSC_VOLTAGE_OUT] = 4587;
		info->b[PSC_VOLTAGE_OUT] = 0;
		info->R[PSC_VOLTAGE_OUT] = -2;

		if (config & LM25066_DEV_SETUP_CL) {
			info->m[PSC_CURRENT_IN] = 10753;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 1204;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -3;
		} else {
			info->m[PSC_CURRENT_IN] = 5405;
			info->b[PSC_CURRENT_IN] = 0;
			info->R[PSC_CURRENT_IN] = -2;
			info->m[PSC_POWER] = 605;
			info->b[PSC_POWER] = 0;
			info->R[PSC_POWER] = -3;
		}
		break;
	default:
		return -ENODEV;
	}

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id lm25066_id[] = {
	{"lm25066", lm25066},
	{"lm5064", lm5064},
	{"lm5066", lm5066},
	{ }
};

MODULE_DEVICE_TABLE(i2c, lm25066_id);

/* This is the driver that will be inserted */
static struct i2c_driver lm25066_driver = {
	.driver = {
		   .name = "lm25066",
		   },
	.probe = lm25066_probe,
	.remove = pmbus_do_remove,
	.id_table = lm25066_id,
};

module_i2c_driver(lm25066_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for LM25066/LM5064/LM5066");
MODULE_LICENSE("GPL");
