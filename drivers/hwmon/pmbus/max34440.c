// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Maxim MAX34440/MAX34441
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2012 Guenter Roeck
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

enum chips { max34440, max34441, max34446, max34451, max34460, max34461 };

#define MAX34440_MFR_VOUT_PEAK		0xd4
#define MAX34440_MFR_IOUT_PEAK		0xd5
#define MAX34440_MFR_TEMPERATURE_PEAK	0xd6
#define MAX34440_MFR_VOUT_MIN		0xd7

#define MAX34446_MFR_POUT_PEAK		0xe0
#define MAX34446_MFR_POUT_AVG		0xe1
#define MAX34446_MFR_IOUT_AVG		0xe2
#define MAX34446_MFR_TEMPERATURE_AVG	0xe3

#define MAX34440_STATUS_OC_WARN		BIT(0)
#define MAX34440_STATUS_OC_FAULT	BIT(1)
#define MAX34440_STATUS_OT_FAULT	BIT(5)
#define MAX34440_STATUS_OT_WARN		BIT(6)

/*
 * The whole max344* family have IOUT_OC_WARN_LIMIT and IOUT_OC_FAULT_LIMIT
 * swapped from the standard pmbus spec addresses.
 */
#define MAX34440_IOUT_OC_WARN_LIMIT	0x46
#define MAX34440_IOUT_OC_FAULT_LIMIT	0x4A

#define MAX34451_MFR_CHANNEL_CONFIG	0xe4
#define MAX34451_MFR_CHANNEL_CONFIG_SEL_MASK	0x3f

struct max34440_data {
	int id;
	struct pmbus_driver_info info;
};

#define to_max34440_data(x)  container_of(x, struct max34440_data, info)

static const struct i2c_device_id max34440_id[];

static int max34440_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	int ret;
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct max34440_data *data = to_max34440_data(info);

	switch (reg) {
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_IOUT_OC_FAULT_LIMIT);
		break;
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_IOUT_OC_WARN_LIMIT);
		break;
	case PMBUS_VIRT_READ_VOUT_MIN:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_MFR_VOUT_MIN);
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_MFR_VOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_IOUT_AVG:
		if (data->id != max34446 && data->id != max34451)
			return -ENXIO;
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34446_MFR_IOUT_AVG);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_MFR_IOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_POUT_AVG:
		if (data->id != max34446)
			return -ENXIO;
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34446_MFR_POUT_AVG);
		break;
	case PMBUS_VIRT_READ_POUT_MAX:
		if (data->id != max34446)
			return -ENXIO;
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34446_MFR_POUT_PEAK);
		break;
	case PMBUS_VIRT_READ_TEMP_AVG:
		if (data->id != max34446 && data->id != max34460 &&
		    data->id != max34461)
			return -ENXIO;
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34446_MFR_TEMPERATURE_AVG);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   MAX34440_MFR_TEMPERATURE_PEAK);
		break;
	case PMBUS_VIRT_RESET_POUT_HISTORY:
		if (data->id != max34446)
			return -ENXIO;
		ret = 0;
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max34440_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct max34440_data *data = to_max34440_data(info);
	int ret;

	switch (reg) {
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = pmbus_write_word_data(client, page, MAX34440_IOUT_OC_FAULT_LIMIT,
					    word);
		break;
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_write_word_data(client, page, MAX34440_IOUT_OC_WARN_LIMIT,
					    word);
		break;
	case PMBUS_VIRT_RESET_POUT_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    MAX34446_MFR_POUT_PEAK, 0);
		if (ret)
			break;
		ret = pmbus_write_word_data(client, page,
					    MAX34446_MFR_POUT_AVG, 0);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    MAX34440_MFR_VOUT_MIN, 0x7fff);
		if (ret)
			break;
		ret = pmbus_write_word_data(client, page,
					    MAX34440_MFR_VOUT_PEAK, 0);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    MAX34440_MFR_IOUT_PEAK, 0);
		if (!ret && (data->id == max34446 || data->id == max34451))
			ret = pmbus_write_word_data(client, page,
					MAX34446_MFR_IOUT_AVG, 0);

		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    MAX34440_MFR_TEMPERATURE_PEAK,
					    0x8000);
		if (!ret && data->id == max34446)
			ret = pmbus_write_word_data(client, page,
					MAX34446_MFR_TEMPERATURE_AVG, 0);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max34440_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret = 0;
	int mfg_status;

	if (page >= 0) {
		ret = pmbus_set_page(client, page, 0xff);
		if (ret < 0)
			return ret;
	}

	switch (reg) {
	case PMBUS_STATUS_IOUT:
		mfg_status = pmbus_read_word_data(client, 0, 0xff,
						  PMBUS_STATUS_MFR_SPECIFIC);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX34440_STATUS_OC_WARN)
			ret |= PB_IOUT_OC_WARNING;
		if (mfg_status & MAX34440_STATUS_OC_FAULT)
			ret |= PB_IOUT_OC_FAULT;
		break;
	case PMBUS_STATUS_TEMPERATURE:
		mfg_status = pmbus_read_word_data(client, 0, 0xff,
						  PMBUS_STATUS_MFR_SPECIFIC);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX34440_STATUS_OT_WARN)
			ret |= PB_TEMP_OT_WARNING;
		if (mfg_status & MAX34440_STATUS_OT_FAULT)
			ret |= PB_TEMP_OT_FAULT;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max34451_set_supported_funcs(struct i2c_client *client,
					 struct max34440_data *data)
{
	/*
	 * Each of the channel 0-15 can be configured to monitor the following
	 * functions based on MFR_CHANNEL_CONFIG[5:0]
	 * 0x10: Sequencing + voltage monitoring (only valid for PAGES 0–11)
	 * 0x20: Voltage monitoring (no sequencing)
	 * 0x21: Voltage read only
	 * 0x22: Current monitoring
	 * 0x23: Current read only
	 * 0x30: General-purpose input active low
	 * 0x34: General-purpose input active high
	 * 0x00:  Disabled
	 */

	int page, rv;

	for (page = 0; page < 16; page++) {
		rv = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
		if (rv < 0)
			return rv;

		rv = i2c_smbus_read_word_data(client,
					      MAX34451_MFR_CHANNEL_CONFIG);
		if (rv < 0)
			return rv;

		switch (rv & MAX34451_MFR_CHANNEL_CONFIG_SEL_MASK) {
		case 0x10:
		case 0x20:
			data->info.func[page] = PMBUS_HAVE_VOUT |
				PMBUS_HAVE_STATUS_VOUT;
			break;
		case 0x21:
			data->info.func[page] = PMBUS_HAVE_VOUT;
			break;
		case 0x22:
			data->info.func[page] = PMBUS_HAVE_IOUT |
				PMBUS_HAVE_STATUS_IOUT;
			break;
		case 0x23:
			data->info.func[page] = PMBUS_HAVE_IOUT;
			break;
		default:
			break;
		}
	}

	return 0;
}

static struct pmbus_driver_info max34440_info[] = {
	[max34440] = {
		.pages = 14,
		.format[PSC_VOLTAGE_IN] = direct,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.format[PSC_CURRENT_OUT] = direct,
		.m[PSC_VOLTAGE_IN] = 1,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = 3,	    /* R = 0 in datasheet reflects mV */
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,    /* R = 0 in datasheet reflects mV */
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 3,    /* R = 0 in datasheet reflects mA */
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[5] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[6] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[7] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[8] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[9] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[10] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[11] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[12] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[13] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_byte_data = max34440_read_byte_data,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
	[max34441] = {
		.pages = 12,
		.format[PSC_VOLTAGE_IN] = direct,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.format[PSC_CURRENT_OUT] = direct,
		.format[PSC_FAN] = direct,
		.m[PSC_VOLTAGE_IN] = 1,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = 3,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 3,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.m[PSC_FAN] = 1,
		.b[PSC_FAN] = 0,
		.R[PSC_FAN] = 0,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[5] = PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12,
		.func[6] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[7] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[8] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[9] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[10] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[11] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_byte_data = max34440_read_byte_data,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
	[max34446] = {
		.pages = 7,
		.format[PSC_VOLTAGE_IN] = direct,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.format[PSC_CURRENT_OUT] = direct,
		.format[PSC_POWER] = direct,
		.m[PSC_VOLTAGE_IN] = 1,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = 3,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 3,
		.m[PSC_POWER] = 1,
		.b[PSC_POWER] = 0,
		.R[PSC_POWER] = 3,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[4] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[5] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[6] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_byte_data = max34440_read_byte_data,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
	[max34451] = {
		.pages = 21,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.format[PSC_CURRENT_OUT] = direct,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 2,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		/* func 0-15 is set dynamically before probing */
		.func[16] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[17] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[18] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[19] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[20] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
	[max34460] = {
		.pages = 18,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[5] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[6] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[7] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[8] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[9] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[10] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[11] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[13] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[14] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[15] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[16] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[17] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
	[max34461] = {
		.pages = 23,
		.format[PSC_VOLTAGE_OUT] = direct,
		.format[PSC_TEMPERATURE] = direct,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[5] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[6] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[7] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[8] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[9] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[10] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[11] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[12] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[13] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[14] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		.func[15] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
		/* page 16 is reserved */
		.func[17] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[18] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[19] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[20] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[21] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_word_data = max34440_read_word_data,
		.write_word_data = max34440_write_word_data,
	},
};

static int max34440_probe(struct i2c_client *client)
{
	struct max34440_data *data;
	int rv;

	data = devm_kzalloc(&client->dev, sizeof(struct max34440_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->id = i2c_match_id(max34440_id, client)->driver_data;
	data->info = max34440_info[data->id];

	if (data->id == max34451) {
		rv = max34451_set_supported_funcs(client, data);
		if (rv)
			return rv;
	}

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id max34440_id[] = {
	{"max34440", max34440},
	{"max34441", max34441},
	{"max34446", max34446},
	{"max34451", max34451},
	{"max34460", max34460},
	{"max34461", max34461},
	{}
};
MODULE_DEVICE_TABLE(i2c, max34440_id);

/* This is the driver that will be inserted */
static struct i2c_driver max34440_driver = {
	.driver = {
		   .name = "max34440",
		   },
	.probe_new = max34440_probe,
	.id_table = max34440_id,
};

module_i2c_driver(max34440_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX34440/MAX34441");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
