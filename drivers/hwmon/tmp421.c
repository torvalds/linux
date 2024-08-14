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
#include <linux/of.h>
#include <linux/sysfs.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2a, 0x4c, 0x4d, 0x4e, 0x4f,
					     I2C_CLIENT_END };

enum chips { tmp421, tmp422, tmp423, tmp441, tmp442 };

#define MAX_CHANNELS				4
/* The TMP421 registers */
#define TMP421_STATUS_REG			0x08
#define TMP421_CONFIG_REG_1			0x09
#define TMP421_CONFIG_REG_2			0x0A
#define TMP421_CONFIG_REG_REN(x)		(BIT(3 + (x)))
#define TMP421_CONFIG_REG_REN_MASK		GENMASK(6, 3)
#define TMP421_CONVERSION_RATE_REG		0x0B
#define TMP421_N_FACTOR_REG_1			0x21
#define TMP421_MANUFACTURER_ID_REG		0xFE
#define TMP421_DEVICE_ID_REG			0xFF

static const u8 TMP421_TEMP_MSB[MAX_CHANNELS]	= { 0x00, 0x01, 0x02, 0x03 };
static const u8 TMP421_TEMP_LSB[MAX_CHANNELS]	= { 0x10, 0x11, 0x12, 0x13 };

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

struct tmp421_channel {
	const char *label;
	bool enabled;
	s16 temp;
};

struct tmp421_data {
	struct i2c_client *client;
	struct mutex update_lock;
	u32 temp_config[MAX_CHANNELS + 1];
	struct hwmon_channel_info temp_info;
	const struct hwmon_channel_info *info[2];
	struct hwmon_chip_info chip;
	bool valid;
	unsigned long last_updated;
	unsigned long channels;
	u8 config;
	struct tmp421_channel channel[MAX_CHANNELS];
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
			data->channel[i].temp = ret << 8;

			ret = i2c_smbus_read_byte_data(client, TMP421_TEMP_LSB[i]);
			if (ret < 0)
				goto exit;
			data->channel[i].temp |= ret;
		}
		data->last_updated = jiffies;
		data->valid = true;
	}

exit:
	mutex_unlock(&data->update_lock);

	if (ret < 0) {
		data->valid = false;
		return ret;
	}

	return 0;
}

static int tmp421_enable_channels(struct tmp421_data *data)
{
	int err;
	struct i2c_client *client = data->client;
	struct device *dev = &client->dev;
	int old = i2c_smbus_read_byte_data(client, TMP421_CONFIG_REG_2);
	int new, i;

	if (old < 0) {
		dev_err(dev, "error reading register, can't disable channels\n");
		return old;
	}

	new = old & ~TMP421_CONFIG_REG_REN_MASK;
	for (i = 0; i < data->channels; i++)
		if (data->channel[i].enabled)
			new |= TMP421_CONFIG_REG_REN(i);

	if (new == old)
		return 0;

	err = i2c_smbus_write_byte_data(client, TMP421_CONFIG_REG_2, new);
	if (err < 0)
		dev_err(dev, "error writing register, can't disable channels\n");

	return err;
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
		if (!tmp421->channel[channel].enabled)
			return -ENODATA;
		*val = temp_from_raw(tmp421->channel[channel].temp,
				     tmp421->config & TMP421_CONFIG_RANGE);
		return 0;
	case hwmon_temp_fault:
		if (!tmp421->channel[channel].enabled)
			return -ENODATA;
		/*
		 * Any of OPEN or /PVLD bits indicate a hardware mulfunction
		 * and the conversion result may be incorrect
		 */
		*val = !!(tmp421->channel[channel].temp & 0x03);
		return 0;
	case hwmon_temp_enable:
		*val = tmp421->channel[channel].enabled;
		return 0;
	default:
		return -EOPNOTSUPP;
	}

}

static int tmp421_read_string(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, const char **str)
{
	struct tmp421_data *data = dev_get_drvdata(dev);

	*str = data->channel[channel].label;

	return 0;
}

static int tmp421_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	struct tmp421_data *data = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_temp_enable:
		data->channel[channel].enabled = val;
		ret = tmp421_enable_channels(data);
		break;
	default:
	    ret = -EOPNOTSUPP;
	}

	return ret;
}

static umode_t tmp421_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_fault:
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_label:
		return 0444;
	case hwmon_temp_enable:
		return 0644;
	default:
		return 0;
	}
}

static int tmp421_init_client(struct tmp421_data *data)
{
	int config, config_orig;
	struct i2c_client *client = data->client;

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

	return tmp421_enable_channels(data);
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

	strscpy(info->type, tmp421_id[kind].name, I2C_NAME_SIZE);
	dev_info(&adapter->dev, "Detected TI %s chip at 0x%02x\n",
		 names[kind], client->addr);

	return 0;
}

static int tmp421_probe_child_from_dt(struct i2c_client *client,
				      struct device_node *child,
				      struct tmp421_data *data)

{
	struct device *dev = &client->dev;
	u32 i;
	s32 val;
	int err;

	err = of_property_read_u32(child, "reg", &i);
	if (err) {
		dev_err(dev, "missing reg property of %pOFn\n", child);
		return err;
	}

	if (i >= data->channels) {
		dev_err(dev, "invalid reg %d of %pOFn\n", i, child);
		return -EINVAL;
	}

	of_property_read_string(child, "label", &data->channel[i].label);
	if (data->channel[i].label)
		data->temp_config[i] |= HWMON_T_LABEL;

	data->channel[i].enabled = of_device_is_available(child);

	err = of_property_read_s32(child, "ti,n-factor", &val);
	if (!err) {
		if (i == 0) {
			dev_err(dev, "n-factor can't be set for internal channel\n");
			return -EINVAL;
		}

		if (val > 127 || val < -128) {
			dev_err(dev, "n-factor for channel %d invalid (%d)\n",
				i, val);
			return -EINVAL;
		}
		i2c_smbus_write_byte_data(client, TMP421_N_FACTOR_REG_1 + i - 1,
					  val);
	}

	return 0;
}

static int tmp421_probe_from_dt(struct i2c_client *client, struct tmp421_data *data)
{
	struct device *dev = &client->dev;
	const struct device_node *np = dev->of_node;
	struct device_node *child;
	int err;

	for_each_child_of_node(np, child) {
		if (strcmp(child->name, "channel"))
			continue;

		err = tmp421_probe_child_from_dt(client, child, data);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	return 0;
}

static const struct hwmon_ops tmp421_ops = {
	.is_visible = tmp421_is_visible,
	.read = tmp421_read,
	.read_string = tmp421_read_string,
	.write = tmp421_write,
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
	data->channels = (unsigned long)i2c_get_match_data(client);
	data->client = client;

	for (i = 0; i < data->channels; i++) {
		data->temp_config[i] = HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_ENABLE;
		data->channel[i].enabled = true;
	}

	err = tmp421_probe_from_dt(client, data);
	if (err)
		return err;

	err = tmp421_init_client(data);
	if (err)
		return err;

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
	.probe = tmp421_probe,
	.id_table = tmp421_id,
	.detect = tmp421_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(tmp421_driver);

MODULE_AUTHOR("Andre Prendel <andre.prendel@gmx.de>");
MODULE_DESCRIPTION("Texas Instruments TMP421/422/423/441/442 temperature sensor driver");
MODULE_LICENSE("GPL");
