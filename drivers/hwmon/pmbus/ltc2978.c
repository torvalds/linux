/*
 * Hardware monitoring driver for LTC2978
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

enum chips { ltc2978 };

#define LTC2978_MFR_VOUT_PEAK		0xdd
#define LTC2978_MFR_VIN_PEAK		0xde
#define LTC2978_MFR_TEMPERATURE_PEAK	0xdf
#define LTC2978_MFR_SPECIAL_ID		0xe7

#define LTC2978_MFR_VOUT_MIN		0xfb
#define LTC2978_MFR_VIN_MIN		0xfc
#define LTC2978_MFR_TEMPERATURE_MIN	0xfd

#define LTC2978_ID_REV1			0x0121
#define LTC2978_ID_REV2			0x0122

/*
 * LTC2978 clears peak data whenever the CLEAR_FAULTS command is executed, which
 * happens pretty much each time chip data is updated. Raw peak data therefore
 * does not provide much value. To be able to provide useful peak data, keep an
 * internal cache of measured peak data, which is only cleared if an explicit
 * "clear peak" command is executed for the sensor in question.
 */
struct ltc2978_data {
	enum chips id;
	int vin_min, vin_max;
	int temp_min, temp_max;
	int vout_min[8], vout_max[8];
	struct pmbus_driver_info info;
};

#define to_ltc2978_data(x)  container_of(x, struct ltc2978_data, info)

static inline int lin11_to_val(int data)
{
	s16 e = ((s16)data) >> 11;
	s32 m = (((s16)(data << 5)) >> 5);

	/*
	 * mantissa is 10 bit + sign, exponent adds up to 15 bit.
	 * Add 6 bit to exponent for maximum accuracy (10 + 15 + 6 = 31).
	 */
	e += 6;
	return (e < 0 ? m >> -e : m << e);
}

static int ltc2978_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VIN_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret) > lin11_to_val(data->vin_max))
				data->vin_max = ret;
			ret = data->vin_max;
		}
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VOUT_PEAK);
		if (ret >= 0) {
			/*
			 * VOUT is 16 bit unsigned with fixed exponent,
			 * so we can compare it directly
			 */
			if (ret > data->vout_max[page])
				data->vout_max[page] = ret;
			ret = data->vout_max[page];
		}
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page,
					   LTC2978_MFR_TEMPERATURE_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret) > lin11_to_val(data->temp_max))
				data->temp_max = ret;
			ret = data->temp_max;
		}
		break;
	case PMBUS_VIRT_READ_VIN_MIN:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VIN_MIN);
		if (ret >= 0) {
			if (lin11_to_val(ret) < lin11_to_val(data->vin_min))
				data->vin_min = ret;
			ret = data->vin_min;
		}
		break;
	case PMBUS_VIRT_READ_VOUT_MIN:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VOUT_MIN);
		if (ret >= 0) {
			/*
			 * VOUT_MIN is known to not be supported on some lots
			 * of LTC2978 revision 1, and will return the maximum
			 * possible voltage if read. If VOUT_MAX is valid and
			 * lower than the reading of VOUT_MIN, use it instead.
			 */
			if (data->vout_max[page] && ret > data->vout_max[page])
				ret = data->vout_max[page];
			if (ret < data->vout_min[page])
				data->vout_min[page] = ret;
			ret = data->vout_min[page];
		}
		break;
	case PMBUS_VIRT_READ_TEMP_MIN:
		ret = pmbus_read_word_data(client, page,
					   LTC2978_MFR_TEMPERATURE_MIN);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    < lin11_to_val(data->temp_min))
				data->temp_min = ret;
			ret = data->temp_min;
		}
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ltc2978_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		data->vout_min[page] = 0xffff;
		data->vout_max[page] = 0;
		ret = pmbus_write_byte(client, page, PMBUS_CLEAR_FAULTS);
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		data->vin_min = 0x7bff;
		data->vin_max = 0;
		ret = pmbus_write_byte(client, page, PMBUS_CLEAR_FAULTS);
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		data->temp_min = 0x7bff;
		data->temp_max = 0x7fff;
		ret = pmbus_write_byte(client, page, PMBUS_CLEAR_FAULTS);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static const struct i2c_device_id ltc2978_id[] = {
	{"ltc2978", ltc2978},
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2978_id);

static int ltc2978_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int chip_id, ret, i;
	struct ltc2978_data *data;
	struct pmbus_driver_info *info;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	data = kzalloc(sizeof(struct ltc2978_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip_id = i2c_smbus_read_word_data(client, LTC2978_MFR_SPECIAL_ID);
	if (chip_id < 0) {
		ret = chip_id;
		goto err_mem;
	}

	if (chip_id == LTC2978_ID_REV1 || chip_id == LTC2978_ID_REV2) {
		data->id = ltc2978;
	} else {
		dev_err(&client->dev, "Unsupported chip ID 0x%x\n", chip_id);
		ret = -ENODEV;
		goto err_mem;
	}
	if (data->id != id->driver_data)
		dev_warn(&client->dev,
			 "Device mismatch: Configured %s, detected %s\n",
			 id->name,
			 ltc2978_id[data->id].name);

	info = &data->info;
	info->read_word_data = ltc2978_read_word_data;
	info->write_word_data = ltc2978_write_word_data;

	data->vout_min[0] = 0xffff;
	data->vin_min = 0x7bff;
	data->temp_min = 0x7bff;
	data->temp_max = 0x7fff;

	switch (id->driver_data) {
	case ltc2978:
		info->pages = 8;
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
		  | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;
		for (i = 1; i < 8; i++) {
			info->func[i] = PMBUS_HAVE_VOUT
			  | PMBUS_HAVE_STATUS_VOUT;
			data->vout_min[i] = 0xffff;
		}
		break;
	default:
		ret = -ENODEV;
		goto err_mem;
	}

	ret = pmbus_do_probe(client, id, info);
	if (ret)
		goto err_mem;
	return 0;

err_mem:
	kfree(data);
	return ret;
}

static int ltc2978_remove(struct i2c_client *client)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct ltc2978_data *data = to_ltc2978_data(info);

	pmbus_do_remove(client);
	kfree(data);
	return 0;
}

/* This is the driver that will be inserted */
static struct i2c_driver ltc2978_driver = {
	.driver = {
		   .name = "ltc2978",
		   },
	.probe = ltc2978_probe,
	.remove = ltc2978_remove,
	.id_table = ltc2978_id,
};

static int __init ltc2978_init(void)
{
	return i2c_add_driver(&ltc2978_driver);
}

static void __exit ltc2978_exit(void)
{
	i2c_del_driver(&ltc2978_driver);
}

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for LTC2978");
MODULE_LICENSE("GPL");
module_init(ltc2978_init);
module_exit(ltc2978_exit);
