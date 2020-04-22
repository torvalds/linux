// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MAX20730, MAX20734, and MAX20743 Integrated, Step-Down
 * Switching Regulators
 *
 * Copyright 2019 Google LLC.
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/pmbus.h>
#include <linux/util_macros.h>
#include "pmbus.h"

enum chips {
	max20730,
	max20734,
	max20743
};

struct max20730_data {
	enum chips id;
	struct pmbus_driver_info info;
	struct mutex lock;	/* Used to protect against parallel writes */
	u16 mfr_devset1;
};

#define to_max20730_data(x)  container_of(x, struct max20730_data, info)

#define MAX20730_MFR_DEVSET1	0xd2

/*
 * Convert discreet value to direct data format. Strictly speaking, all passed
 * values are constants, so we could do that calculation manually. On the
 * downside, that would make the driver more difficult to maintain, so lets
 * use this approach.
 */
static u16 val_to_direct(int v, enum pmbus_sensor_classes class,
			 const struct pmbus_driver_info *info)
{
	int R = info->R[class] - 3;	/* take milli-units into account */
	int b = info->b[class] * 1000;
	long d;

	d = v * info->m[class] + b;
	/*
	 * R < 0 is true for all callers, so we don't need to bother
	 * about the R > 0 case.
	 */
	while (R < 0) {
		d = DIV_ROUND_CLOSEST(d, 10);
		R++;
	}
	return (u16)d;
}

static long direct_to_val(u16 w, enum pmbus_sensor_classes class,
			  const struct pmbus_driver_info *info)
{
	int R = info->R[class] - 3;
	int b = info->b[class] * 1000;
	int m = info->m[class];
	long d = (s16)w;

	if (m == 0)
		return 0;

	while (R < 0) {
		d *= 10;
		R++;
	}
	d = (d - b) / m;
	return d;
}

static u32 max_current[][5] = {
	[max20730] = { 13000, 16600, 20100, 23600 },
	[max20734] = { 21000, 27000, 32000, 38000 },
	[max20743] = { 18900, 24100, 29200, 34100 },
};

static int max20730_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct max20730_data *data = to_max20730_data(info);
	int ret = 0;
	u32 max_c;

	switch (reg) {
	case PMBUS_OT_FAULT_LIMIT:
		switch ((data->mfr_devset1 >> 11) & 0x3) {
		case 0x0:
			ret = val_to_direct(150000, PSC_TEMPERATURE, info);
			break;
		case 0x1:
			ret = val_to_direct(130000, PSC_TEMPERATURE, info);
			break;
		default:
			ret = -ENODATA;
			break;
		}
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		max_c = max_current[data->id][(data->mfr_devset1 >> 5) & 0x3];
		ret = val_to_direct(max_c, PSC_CURRENT_OUT, info);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max20730_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	struct pmbus_driver_info *info;
	struct max20730_data *data;
	u16 devset1;
	int ret = 0;
	int idx;

	info = (struct pmbus_driver_info *)pmbus_get_driver_info(client);
	data = to_max20730_data(info);

	mutex_lock(&data->lock);
	devset1 = data->mfr_devset1;

	switch (reg) {
	case PMBUS_OT_FAULT_LIMIT:
		devset1 &= ~(BIT(11) | BIT(12));
		if (direct_to_val(word, PSC_TEMPERATURE, info) < 140000)
			devset1 |= BIT(11);
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		devset1 &= ~(BIT(5) | BIT(6));

		idx = find_closest(direct_to_val(word, PSC_CURRENT_OUT, info),
				   max_current[data->id], 4);
		devset1 |= (idx << 5);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	if (!ret && devset1 != data->mfr_devset1) {
		ret = i2c_smbus_write_word_data(client, MAX20730_MFR_DEVSET1,
						devset1);
		if (!ret) {
			data->mfr_devset1 = devset1;
			pmbus_clear_cache(client);
		}
	}
	mutex_unlock(&data->lock);
	return ret;
}

static const struct pmbus_driver_info max20730_info[] = {
	[max20730] = {
		.pages = 1,
		.read_word_data = max20730_read_word_data,
		.write_word_data = max20730_write_word_data,

		/* Source : Maxim AN6042 */
		.format[PSC_TEMPERATURE] = direct,
		.m[PSC_TEMPERATURE] = 21,
		.b[PSC_TEMPERATURE] = 5887,
		.R[PSC_TEMPERATURE] = -1,

		.format[PSC_VOLTAGE_IN] = direct,
		.m[PSC_VOLTAGE_IN] = 3609,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = -2,

		/*
		 * Values in the datasheet are adjusted for temperature and
		 * for the relationship between Vin and Vout.
		 * Unfortunately, the data sheet suggests that Vout measurement
		 * may be scaled with a resistor array. This is indeed the case
		 * at least on the evaulation boards. As a result, any in-driver
		 * adjustments would either be wrong or require elaborate means
		 * to configure the scaling. Instead of doing that, just report
		 * raw values and let userspace handle adjustments.
		 */
		.format[PSC_CURRENT_OUT] = direct,
		.m[PSC_CURRENT_OUT] = 153,
		.b[PSC_CURRENT_OUT] = 4976,
		.R[PSC_CURRENT_OUT] = -1,

		.format[PSC_VOLTAGE_OUT] = linear,

		.func[0] = PMBUS_HAVE_VIN |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	},
	[max20734] = {
		.pages = 1,
		.read_word_data = max20730_read_word_data,
		.write_word_data = max20730_write_word_data,

		/* Source : Maxim AN6209 */
		.format[PSC_TEMPERATURE] = direct,
		.m[PSC_TEMPERATURE] = 21,
		.b[PSC_TEMPERATURE] = 5887,
		.R[PSC_TEMPERATURE] = -1,

		.format[PSC_VOLTAGE_IN] = direct,
		.m[PSC_VOLTAGE_IN] = 3592,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = -2,

		.format[PSC_CURRENT_OUT] = direct,
		.m[PSC_CURRENT_OUT] = 111,
		.b[PSC_CURRENT_OUT] = 3461,
		.R[PSC_CURRENT_OUT] = -1,

		.format[PSC_VOLTAGE_OUT] = linear,

		.func[0] = PMBUS_HAVE_VIN |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	},
	[max20743] = {
		.pages = 1,
		.read_word_data = max20730_read_word_data,
		.write_word_data = max20730_write_word_data,

		/* Source : Maxim AN6042 */
		.format[PSC_TEMPERATURE] = direct,
		.m[PSC_TEMPERATURE] = 21,
		.b[PSC_TEMPERATURE] = 5887,
		.R[PSC_TEMPERATURE] = -1,

		.format[PSC_VOLTAGE_IN] = direct,
		.m[PSC_VOLTAGE_IN] = 3597,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = -2,

		.format[PSC_CURRENT_OUT] = direct,
		.m[PSC_CURRENT_OUT] = 95,
		.b[PSC_CURRENT_OUT] = 5014,
		.R[PSC_CURRENT_OUT] = -1,

		.format[PSC_VOLTAGE_OUT] = linear,

		.func[0] = PMBUS_HAVE_VIN |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	},
};

static int max20730_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	struct max20730_data *data;
	enum chips chip_id;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_WORD_DATA |
				     I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer ID\n");
		return ret;
	}
	if (ret != 5 || strncmp(buf, "MAXIM", 5)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer ID '%s'\n", buf);
		return -ENODEV;
	}

	/*
	 * The chips support reading PMBUS_MFR_MODEL. On both MAX20730
	 * and MAX20734, reading it returns M20743. Presumably that is
	 * the reason why the command is not documented. Unfortunately,
	 * that means that there is no reliable means to detect the chip.
	 * However, we can at least detect the chip series. Compare
	 * the returned value against 'M20743' and bail out if there is
	 * a mismatch. If that doesn't work for all chips, we may have
	 * to remove this check.
	 */
	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Model\n");
		return ret;
	}
	if (ret != 6 || strncmp(buf, "M20743", 6)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Model '%s'\n", buf);
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_REVISION, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Revision\n");
		return ret;
	}
	if (ret != 1 || buf[0] != 'F') {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Revision '%s'\n", buf);
		return -ENODEV;
	}

	if (client->dev.of_node)
		chip_id = (enum chips)of_device_get_match_data(dev);
	else
		chip_id = id->driver_data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->id = chip_id;
	mutex_init(&data->lock);
	memcpy(&data->info, &max20730_info[chip_id], sizeof(data->info));

	ret = i2c_smbus_read_word_data(client, MAX20730_MFR_DEVSET1);
	if (ret < 0)
		return ret;
	data->mfr_devset1 = ret;

	return pmbus_do_probe(client, id, &data->info);
}

static const struct i2c_device_id max20730_id[] = {
	{ "max20730", max20730 },
	{ "max20734", max20734 },
	{ "max20743", max20743 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, max20730_id);

static const struct of_device_id max20730_of_match[] = {
	{ .compatible = "maxim,max20730", .data = (void *)max20730 },
	{ .compatible = "maxim,max20734", .data = (void *)max20734 },
	{ .compatible = "maxim,max20743", .data = (void *)max20743 },
	{ },
};

MODULE_DEVICE_TABLE(of, max20730_of_match);

static struct i2c_driver max20730_driver = {
	.driver = {
		.name = "max20730",
		.of_match_table = max20730_of_match,
	},
	.probe = max20730_probe,
	.remove = pmbus_do_remove,
	.id_table = max20730_id,
};

module_i2c_driver(max20730_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX20730 / MAX20734 / MAX20743");
MODULE_LICENSE("GPL");
