// SPDX-License-Identifier: GPL-2.0-or-later
/* tmp421.c
 *
 * Copyright (C) 2009 Andre Prendel <andre.prendel@gmx.de>
 * Preliminary support by:
 * Melvin Rook, Raymond Ng
 */

/*
 * Driver for the Texas Instruments TMP421 SMBus temperature sensor IC.
 * Supported models: TMP421, TMP422, TMP423, TMP441, TMP442
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2a, 0x4c, 0x4d, 0x4e, 0x4f,
					     I2C_CLIENT_END };

enum chips { tmp421, tmp422, tmp423, tmp441, tmp442 };

/* The TMP421 registers */
#define TMP421_STATUS_REG			0x08
#define TMP421_CONFIG_REG_1			0x09
#define TMP421_CONVERSION_RATE_REG		0x0B
#define TMP421_MANUFACTURER_ID_REG		0xFE
#define TMP421_DEVICE_ID_REG			0xFF

static const u8 TMP421_TEMP_MSB[4]		= { 0x00, 0x01, 0x02, 0x03 };
static const u8 TMP421_TEMP_LSB[4]		= { 0x10, 0x11, 0x12, 0x13 };

/* Flags */
#define TMP421_CONFIG_SHUTDOWN			0x40
#define TMP421_CONFIG_RANGE			0x04

/* Manufacturer / Device ID's */
#define TMP421_MANUFACTURER_ID			0x55
#define TMP421_DEVICE_ID			0x21
#define TMP422_DEVICE_ID			0x22
#define TMP423_DEVICE_ID			0x23
#define TMP441_DEVICE_ID			0x41
#define TMP442_DEVICE_ID			0x42

static const struct i2c_device_id tmp421_id[] = {
	{ "tmp421", 2 },
	{ "tmp422", 3 },
	{ "tmp423", 4 },
	{ "tmp441", 2 },
	{ "tmp442", 3 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp421_id);

static const struct of_device_id __maybe_unused tmp421_of_match[] = {
	{
		.compatible = "ti,tmp421",
		.data = (void *)2
	},
	{
		.compatible = "ti,tmp422",
		.data = (void *)3
	},
	{
		.compatible = "ti,tmp423",
		.data = (void *)4
	},
	{
		.compatible = "ti,tmp441",
		.data = (void *)2
	},
	{
		.compatible = "ti,tmp442",
		.data = (void *)3
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tmp421_of_match);

struct tmp421_data {
	struct i2c_client *client;
	struct mutex update_lock;
	u32 temp_config[5];
	struct hwmon_channel_info temp_info;
	const struct hwmon_channel_info *info[2];
	struct hwmon_chip_info chip;
	char valid;
	unsigned long last_updated;
	unsigned long channels;
	u8 config;
	s16 temp[4];
};

static int temp_from_raw(u16 reg, bool extended)
{
	/* Mask out status bits */
	int temp = reg & ~0xf;

	if (extended)
		temp = temp - 64 * 256;
	else
		temp = (s16)temp;

	return DIV_ROUND_CLOSEST(temp * 1000, 256);
}

static int tmp421_update_device(struct tmp421_data *data)
{
	struct i2c_client *client = data->client;
	int ret = 0;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + (HZ / 2)) ||
	    !data->valid) {
		ret = i2c_smbus_read_byte_data(client, TMP421_CONFIG_REG_1);
		if (ret < 0)
			goto exit;
		data->config = ret;

		for (i = 0; i < data->channels; i++) {
			ret = i2c_smbus_read_byte_data(client, TMP421_TEMP_MSB[i]);
			if (ret < 0)
				goto exit;
			data->temp[i] = ret << 8;

			ret = i2c_smbus_read_byte_data(client, TMP421_TEMP_LSB[i]);
			if (ret < 0)
				goto exit;
			data->temp[i] |= ret;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

exit:
	mutex_unlock(&data->update_lock);

	if (ret < 0) {
		data->valid = 0;
		return ret;
	}

	return 0;
}

static int tmp421_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct tmp421_data *tmp421 = dev_get_drvdata(dev);
	int ret = 0;

	ret = tmp421_update_device(tmp421);
	if (ret)
		return ret;

	switch (attr) {
	case hwmon_temp_input:
		*val = temp_from_raw(tmp421->temp[channel],
				     tmp421->config & TMP421_CONFIG_RANGE);
		return 0;
	case hwmon_temp_fault:
		/*
		 * Any of OPEN or /PVLD bits indicate a hardware mulfunction
		 * and the conversion result may be incorrect
		 */
		*val = !!(tmp421->temp[channel] & 0x03);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

}

static umode_t tmp421_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_fault:
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static int tmp421_init_client(struct i2c_client *client)
{
	int config, config_orig;

	/* Set the conversion rate to 2 Hz */
	i2c_smbus_write_byte_data(client, TMP421_CONVERSION_RATE_REG, 0x05);

	/* Start conversions (disable shutdown if necessary) */
	config = i2c_smbus_read_byte_data(client, TMP421_CONFIG_REG_1);
	if (config < 0) {
		dev_err(&client->dev,
			"Could not read configuration register (%d)\n", config);
		return config;
	}

	config_orig = config;
	config &= ~TMP421_CONFIG_SHUTDOWN;

	if (config != config_orig) {
		dev_info(&client->dev, "Enable monitoring chip\n");
		i2c_smbus_write_byte_data(client, TMP421_CONFIG_REG_1, config);
	}

	return 0;
}

static int tmp421_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	enum chips kind;
	struct i2c_adapter *adapter = client->adapter;
	static const char * const names[] = {
		"TMP421", "TMP422", "TMP423",
		"TMP441", "TMP442"
	};
	int addr = client->addr;
	u8 reg;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP421_MANUFACTURER_ID_REG);
	if (reg != TMP421_MANUFACTURER_ID)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP421_CONVERSION_RATE_REG);
	if (reg & 0xf8)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP421_STATUS_REG);
	if (reg & 0x7f)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, TMP421_DEVICE_ID_REG);
	switch (reg) {
	case TMP421_DEVICE_ID:
		kind = tmp421;
		break;
	case TMP422_DEVICE_ID:
		if (addr == 0x2a)
			return -ENODEV;
		kind = tmp422;
		break;
	case TMP423_DEVICE_ID:
		if (addr != 0x4c && addr != 0x4d)
			return -ENODEV;
		kind = tmp423;
		break;
	case TMP441_DEVICE_ID:
		kind = tmp441;
		break;
	case TMP442_DEVICE_ID:
		if (addr != 0x4c && addr != 0x4d)
			return -ENODEV;
		kind = tmp442;
		break;
	default:
		return -ENODEV;
	}

	strlcpy(info->type, tmp421_id[kind].name, I2C_NAME_SIZE);
	dev_info(&adapter->dev, "Detected TI %s chip at 0x%02x\n",
		 names[kind], client->addr);

	return 0;
}

static const struct hwmon_ops tmp421_ops = {
	.is_visible = tmp421_is_visible,
	.read = tmp421_read,
};

static int tmp421_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct tmp421_data *data;
	int i, err;

	data = devm_kzalloc(dev, sizeof(struct tmp421_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);
	if (client->dev.of_node)
		data->channels = (unsigned long)
			of_device_get_match_data(&client->dev);
	else
		data->channels = i2c_match_id(tmp421_id, client)->driver_data;
	data->client = client;

	err = tmp421_init_client(client);
	if (err)
		return err;

	for (i = 0; i < data->channels; i++)
		data->temp_config[i] = HWMON_T_INPUT | HWMON_T_FAULT;

	data->chip.ops = &tmp421_ops;
	data->chip.info = data->info;

	data->info[0] = &data->temp_info;

	data->temp_info.type = hwmon_temp;
	data->temp_info.config = data->temp_config;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &data->chip,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct i2c_driver tmp421_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name	= "tmp421",
		.of_match_table = of_match_ptr(tmp421_of_match),
	},
	.probe_new = tmp421_probe,
	.id_table = tmp421_id,
	.detect = tmp421_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(tmp421_driver);

MODULE_AUTHOR("Andre Prendel <andre.prendel@gmx.de>");
MODULE_DESCRIPTION("Texas Instruments TMP421/422/423/441/442 temperature sensor driver");
MODULE_LICENSE("GPL");
