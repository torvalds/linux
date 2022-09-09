// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for LM25056 / LM25066 / LM5064 / LM5066
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2013 Guenter Roeck
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/of_device.h>
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

static const struct __coeff lm25066_coeff[][PSC_NUM_CLASSES + 2] = {
	[lm25056] = {
		[PSC_VOLTAGE_IN] = {
			.m = 16296,
			.b = 1343,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 13797,
			.b = -1833,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 6726,
			.b = -537,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 5501,
			.b = -2908,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 26882,
			.b = -5646,
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
			.b = -1800,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 22070,
			.b = -1800,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 13661,
			.b = -5200,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 6854,
			.b = -3100,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 736,
			.b = -3300,
			.R = -2,
		},
		[PSC_POWER_L] = {
			.m = 369,
			.b = -1900,
			.R = -2,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
	[lm5064] = {
		[PSC_VOLTAGE_IN] = {
			.m = 4611,
			.b = -642,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 4621,
			.b = 423,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 10742,
			.b = 1552,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 5456,
			.b = 2118,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 1204,
			.b = 8524,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 612,
			.b = 11202,
			.R = -3,
		},
		[PSC_TEMPERATURE] = {
			.m = 16,
		},
	},
	[lm5066] = {
		[PSC_VOLTAGE_IN] = {
			.m = 4587,
			.b = -1200,
			.R = -2,
		},
		[PSC_VOLTAGE_OUT] = {
			.m = 4587,
			.b = -2400,
			.R = -2,
		},
		[PSC_CURRENT_IN] = {
			.m = 10753,
			.b = -1200,
			.R = -2,
		},
		[PSC_CURRENT_IN_L] = {
			.m = 5405,
			.b = -600,
			.R = -2,
		},
		[PSC_POWER] = {
			.m = 1204,
			.b = -6000,
			.R = -3,
		},
		[PSC_POWER_L] = {
			.m = 605,
			.b = -8000,
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

static int lm25066_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct lm25066_data *data = to_lm25066_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, 0, 0xff, LM25066_READ_VAUX);
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
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_MFR_READ_IIN);
		break;
	case PMBUS_READ_PIN:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_MFR_READ_PIN);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_MFR_IIN_OC_WARN_LIMIT);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_MFR_PIN_OP_WARN_LIMIT);
		break;
	case PMBUS_VIRT_READ_VIN_AVG:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_READ_AVG_VIN);
		break;
	case PMBUS_VIRT_READ_VOUT_AVG:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_READ_AVG_VOUT);
		break;
	case PMBUS_VIRT_READ_IIN_AVG:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_READ_AVG_IIN);
		break;
	case PMBUS_VIRT_READ_PIN_AVG:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_READ_AVG_PIN);
		break;
	case PMBUS_VIRT_READ_PIN_MAX:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25066_READ_PIN_PEAK);
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

static int lm25056_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25056_VAUX_UV_WARN_LIMIT);
		if (ret < 0)
			break;
		/* Adjust returned value to match VIN coefficients */
		ret = DIV_ROUND_CLOSEST(ret * 293, 6140);
		break;
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   LM25056_VAUX_OV_WARN_LIMIT);
		if (ret < 0)
			break;
		/* Adjust returned value to match VIN coefficients */
		ret = DIV_ROUND_CLOSEST(ret * 293, 6140);
		break;
	default:
		ret = lm25066_read_word_data(client, page, phase, reg);
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
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_IIN_OC_WARN_LIMIT,
					    word);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25066_MFR_PIN_OP_WARN_LIMIT,
					    word);
		break;
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		/* Adjust from VIN coefficients (for LM25056) */
		word = DIV_ROUND_CLOSEST((int)word * 6140, 293);
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25056_VAUX_UV_WARN_LIMIT, word);
		break;
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		/* Adjust from VIN coefficients (for LM25056) */
		word = DIV_ROUND_CLOSEST((int)word * 6140, 293);
		word = ((s16)word < 0) ? 0 : clamp_val(word, 0, data->rlimit);
		ret = pmbus_write_word_data(client, 0,
					    LM25056_VAUX_OV_WARN_LIMIT, word);
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

#if IS_ENABLED(CONFIG_SENSORS_LM25066_REGULATOR)
static const struct regulator_desc lm25066_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
};
#endif

static const struct i2c_device_id lm25066_id[] = {
	{"lm25056", lm25056},
	{"lm25066", lm25066},
	{"lm5064", lm5064},
	{"lm5066", lm5066},
	{"lm5066i", lm5066i},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm25066_id);

static const struct of_device_id __maybe_unused lm25066_of_match[] = {
	{ .compatible = "ti,lm25056", .data = (void *)lm25056, },
	{ .compatible = "ti,lm25066", .data = (void *)lm25066, },
	{ .compatible = "ti,lm5064",  .data = (void *)lm5064,  },
	{ .compatible = "ti,lm5066",  .data = (void *)lm5066,  },
	{ .compatible = "ti,lm5066i", .data = (void *)lm5066i, },
	{ },
};
MODULE_DEVICE_TABLE(of, lm25066_of_match);

static int lm25066_probe(struct i2c_client *client)
{
	int config;
	u32 shunt;
	struct lm25066_data *data;
	struct pmbus_driver_info *info;
	const struct __coeff *coeff;
	const struct of_device_id *of_id;
	const struct i2c_device_id *i2c_id;

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

	i2c_id = i2c_match_id(lm25066_id, client);

	of_id = of_match_device(lm25066_of_match, &client->dev);
	if (of_id && (unsigned long)of_id->data != i2c_id->driver_data)
		dev_notice(&client->dev, "Device mismatch: %s in device tree, %s detected\n",
			   of_id->name, i2c_id->name);

	data->id = i2c_id->driver_data;
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

	/*
	 * Values in the TI datasheets are normalized for a 1mOhm sense
	 * resistor; assume that unless DT specifies a value explicitly.
	 */
	if (of_property_read_u32(client->dev.of_node, "shunt-resistor-micro-ohms", &shunt))
		shunt = 1000;

	info->m[PSC_CURRENT_IN] = info->m[PSC_CURRENT_IN] * shunt / 1000;
	info->m[PSC_POWER] = info->m[PSC_POWER] * shunt / 1000;

#if IS_ENABLED(CONFIG_SENSORS_LM25066_REGULATOR)
	/* LM25056 doesn't support OPERATION */
	if (data->id != lm25056) {
		info->num_regulators = ARRAY_SIZE(lm25066_reg_desc);
		info->reg_desc = lm25066_reg_desc;
	}
#endif

	return pmbus_do_probe(client, info);
}

/* This is the driver that will be inserted */
static struct i2c_driver lm25066_driver = {
	.driver = {
		   .name = "lm25066",
		   .of_match_table = of_match_ptr(lm25066_of_match),
	},
	.probe_new = lm25066_probe,
	.id_table = lm25066_id,
};

module_i2c_driver(lm25066_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for LM25066 and compatible chips");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
