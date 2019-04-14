/*
 * Hardware monitoring driver for LM25056 / LM25066 / LM5064 / LM5066
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2013 Guenter Roeck
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

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include "pmbus.h"

enum chips { lm25056, lm25066, lm5064, lm5066, lm5066i };

#define LM25066_READ_VAUX		0xd0
#define LM25066_MFR_READ_IIN		0xd1
#define LM25066_MFR_READ_PIN		0xd2
#define LM25066_MFR_IIN_OC_WARN_LIMIT	0xd3
#define LM25066_MFR_PIN_OP_WARN_LIMIT	0xd4
#define LM25066_READ_PIN_PEAK		0xd5
#define LM25066_CLEAR_PIN_PEAK		0xd6
#define LM25066_DEVICE_SETUP		0xd9
#define LM25066_READ_AVG_VIN		0xdc
#define LM25066_SAMPLES_FOR_AVG		0xdb
#define LM25066_READ_AVG_VOUT		0xdd
#define LM25066_READ_AVG_IIN		0xde
#define LM25066_READ_AVG_PIN		0xdf

#define LM25066_DEV_SETUP_CL		BIT(4)	/* Current limit */

#define LM25066_SAMPLES_FOR_AVG_MAX	4096

/* LM25056 only */

#define LM25056_VAUX_OV_WARN_LIMIT	0xe3
#define LM25056_VAUX_UV_WARN_LIMIT	0xe4

#define LM25056_MFR_STS_VAUX_OV_WARN	BIT(1)
#define LM25056_MFR_STS_VAUX_UV_WARN	BIT(0)

struct __coeff {
	short m, b, R;
};

#define PSC_CURRENT_IN_L	(PSC_NUM_CLASSES)
#define PSC_POWER_L		(PSC_NUM_CLASSES + 1)

static struct __coeff lm25066_coeff[6][PSC_NUM_CLASSES + 2] = {
	[lm25056] = {
		[PSC_VOLTAGE_IN] = {
			.m = 16296,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 13797,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 6726,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 5501,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 26882,
			.R = -4,
		},
		[PSC_TEMPERATURE] = {
			.m = 1580,
			.b = -14500,
			.R = -2,
		},
	},
	[lm25066] = {
		[PSC_VOLTAGE_IN] = {
			.m = 22070,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 22070,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 13661,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 6852,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 736,
			.R = -2,
		},
		[PSC_POWER_L] = {
			.m = 369,
			.R = -2,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
	[lm5064] = {
		[PSC_VOLTAGE_IN] = {
			.m = 4611,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 4621,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 10742,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 5456,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 1204,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 612,
			.R = -3,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
	[lm5066] = {
		[PSC_VOLTAGE_IN] = {
			.m = 4587,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 4587,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 10753,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 5405,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 1204,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 605,
			.R = -3,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
	[lm5066i] = {
		[PSC_VOLTAGE_IN] = {
			.m = 4617,
			.b = -140,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 4602,
			.b = 500,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 15076,
			.b = -504,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 7645,
			.b = 100,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 1701,
			.b = -4000,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 861,
			.b = -965,
			.R = -3,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
};

struct lm25066_data {
	int id;
	u16 rlimit;			/* Maximum register value */
	struct pmbus_driver_info info;
};

#define to_lm25066_data(x)  container_of(x, struct lm25066_data, info)

static int lm25066_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct lm25066_data *data = to_lm25066_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, 0, LM25066_READ_VAUX);
		if (ret < 0)
			break;
		/* Adjust returned value to match VIN coefficients */
		switch (data->id) {
		case lm25056:
			/* VIN: 6.14 mV VAUX: 293 uV LSB */
			ret = DIV_ROUND_CLOSEST(ret * 293, 6140);
			break;
		case lm25066:
			/* VIN: 4.54 mV VAUX: 283.2 uV LSB */
			ret = DIV_ROUND_CLOSEST(ret * 2832, 45400);
			break;
		case lm5064:
			/* VIN: 4.53 mV VAUX: 700 uV LSB */
			ret = DIV_ROUND_CLOSEST(ret * 70, 453);
			break;
		case lm5066:
		case lm5066i:
			/* VIN: 2.18 mV VAUX: 725 uV LSB */
			ret = DIV_ROUND_CLOSEST(ret * 725, 2180);
			break;
		}
		break;
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
	case PMBUS_VIRT_SAMPLES:
		ret = pmbus_read_byte_data(client, 0, LM25066_SAMPLES_FOR_AVG);
		if (ret < 0)
			break;
		ret = 1 << ret;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int lm25056_read_word_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0,
					   LM25056_VAUX_UV_WARN_LIMIT);
		if (ret < 0)
			break;
		/* Adjust returned value to match VIN coefficients */
		ret = DIV_ROUND_CLOSEST(ret * 293, 6140);
		break;
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0,
					   LM25056_VAUX_OV_WARN_LIMIT);
		if (ret < 0)
			break;
		/* Adjust returned value to match VIN coefficients */
		ret = DIV_ROUND_CLOSEST(ret * 293, 6140);
		break;
	default:
		ret = lm25066_read_word_data(client, page, reg);
		break;
	}
	return ret;
}

static int lm25056_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret, s;

	switch (reg) {
	case PMBUS_VIRT_STATUS_VMON:
		ret = pmbus_read_byte_data(client, 0,
					   PMBUS_STATUS_MFR_SPECIFIC);
		if (ret < 0)
			break;
		s = 0;
		if (ret & LM25056_MFR_STS_VAUX_UV_WARN)
			s |= PB_VOLTAGE_UV_WARNING;
		if (ret & LM25056_MFR_STS_VAUX_OV_WARN)
			s |= PB_VOLTAGE_OV_WARNING;
		ret = s;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int lm25066_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct lm25066_data *data = to_lm25066_data(info);
	int ret;

	switch (reg) {
	case PMBUS_POUT_OP_FAULT_LIMIT:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_IIN_OC_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0, reg, word);
		pmbus_clear_cache(client);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_IIN_OC_WARN_LIMIT,
					    word);
		pmbus_clear_cache(client);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_PIN_OP_WARN_LIMIT,
					    word);
		pmbus_clear_cache(client);
		break;
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		/* Adjust from VIN coefficients (for LM25056) */
		word = DIV_ROUND_CLOSEST((int)word * 6140, 293);
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25056_VAUX_UV_WARN_LIMIT, word);
		pmbus_clear_cache(client);
		break;
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		/* Adjust from VIN coefficients (for LM25056) */
		word = DIV_ROUND_CLOSEST((int)word * 6140, 293);
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25056_VAUX_OV_WARN_LIMIT, word);
		pmbus_clear_cache(client);
		break;
	case PMBUS_VIRT_RESET_PIN_HISTORY:
		ret = pmbus_write_byte(client, 0, LM25066_CLEAR_PIN_PEAK);
		break;
	case PMBUS_VIRT_SAMPLES:
		word = clamp_val(word, 1, LM25066_SAMPLES_FOR_AVG_MAX);
		ret = pmbus_write_byte_data(client, 0, LM25066_SAMPLES_FOR_AVG,
					    ilog2(word));
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int lm25066_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int config;
	struct lm25066_data *data;
	struct pmbus_driver_info *info;
	struct __coeff *coeff;

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

	info->pages = 1;
	info->format[PSC_VOLTAGE_IN] = direct;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->format[PSC_CURRENT_IN] = direct;
	info->format[PSC_TEMPERATURE] = direct;
	info->format[PSC_POWER] = direct;

	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VMON
	  | PMBUS_HAVE_PIN | PMBUS_HAVE_IIN | PMBUS_HAVE_STATUS_INPUT
	  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_SAMPLES;

	if (data->id == lm25056) {
		info->func[0] |= PMBUS_HAVE_STATUS_VMON;
		info->read_word_data = lm25056_read_word_data;
		info->read_byte_data = lm25056_read_byte_data;
		data->rlimit = 0x0fff;
	} else {
		info->func[0] |= PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;
		info->read_word_data = lm25066_read_word_data;
		data->rlimit = 0x0fff;
	}
	info->write_word_data = lm25066_write_word_data;

	coeff = &lm25066_coeff[data->id][0];
	info->m[PSC_TEMPERATURE] = coeff[PSC_TEMPERATURE].m;
	info->b[PSC_TEMPERATURE] = coeff[PSC_TEMPERATURE].b;
	info->R[PSC_TEMPERATURE] = coeff[PSC_TEMPERATURE].R;
	info->m[PSC_VOLTAGE_IN] = coeff[PSC_VOLTAGE_IN].m;
	info->b[PSC_VOLTAGE_IN] = coeff[PSC_VOLTAGE_IN].b;
	info->R[PSC_VOLTAGE_IN] = coeff[PSC_VOLTAGE_IN].R;
	info->m[PSC_VOLTAGE_OUT] = coeff[PSC_VOLTAGE_OUT].m;
	info->b[PSC_VOLTAGE_OUT] = coeff[PSC_VOLTAGE_OUT].b;
	info->R[PSC_VOLTAGE_OUT] = coeff[PSC_VOLTAGE_OUT].R;
	info->R[PSC_CURRENT_IN] = coeff[PSC_CURRENT_IN].R;
	info->R[PSC_POWER] = coeff[PSC_POWER].R;
	if (config & LM25066_DEV_SETUP_CL) {
		info->m[PSC_CURRENT_IN] = coeff[PSC_CURRENT_IN_L].m;
		info->b[PSC_CURRENT_IN] = coeff[PSC_CURRENT_IN_L].b;
		info->m[PSC_POWER] = coeff[PSC_POWER_L].m;
		info->b[PSC_POWER] = coeff[PSC_POWER_L].b;
	} else {
		info->m[PSC_CURRENT_IN] = coeff[PSC_CURRENT_IN].m;
		info->b[PSC_CURRENT_IN] = coeff[PSC_CURRENT_IN].b;
		info->m[PSC_POWER] = coeff[PSC_POWER].m;
		info->b[PSC_POWER] = coeff[PSC_POWER].b;
	}

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id lm25066_id[] = {
	{"lm25056", lm25056},
	{"lm25066", lm25066},
	{"lm5064", lm5064},
	{"lm5066", lm5066},
	{"lm5066i", lm5066i},
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
MODULE_DESCRIPTION("PMBus driver for LM25066 and compatible chips");
MODULE_LICENSE("GPL");
