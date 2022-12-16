// SPDX-License-Identifier: GPL-2.0-only
/*
 * hp206c.c - HOPERF HP206C precision barometer and altimeter sensor
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * (7-bit I2C slave address 0x76)
 *
 * Datasheet:
 *  http://www.hoperf.com/upload/sensor/HP206C_DataSheet_EN_V2.0.pdf
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>
#include <linux/util_macros.h>
#include <linux/acpi.h>

#include <asm/unaligned.h>

/* I2C commands: */
#define HP206C_CMD_SOFT_RST	0x06

#define HP206C_CMD_ADC_CVT	0x40

#define HP206C_CMD_ADC_CVT_OSR_4096	0x00
#define HP206C_CMD_ADC_CVT_OSR_2048	0x04
#define HP206C_CMD_ADC_CVT_OSR_1024	0x08
#define HP206C_CMD_ADC_CVT_OSR_512	0x0c
#define HP206C_CMD_ADC_CVT_OSR_256	0x10
#define HP206C_CMD_ADC_CVT_OSR_128	0x14

#define HP206C_CMD_ADC_CVT_CHNL_PT	0x00
#define HP206C_CMD_ADC_CVT_CHNL_T	0x02

#define HP206C_CMD_READ_P	0x30
#define HP206C_CMD_READ_T	0x32

#define HP206C_CMD_READ_REG	0x80
#define HP206C_CMD_WRITE_REG	0xc0

#define HP206C_REG_INT_EN	0x0b
#define HP206C_REG_INT_CFG	0x0c

#define HP206C_REG_INT_SRC	0x0d
#define HP206C_FLAG_DEV_RDY	0x40

#define HP206C_REG_PARA		0x0f
#define HP206C_FLAG_CMPS_EN	0x80

/* Maximum spin for DEV_RDY */
#define HP206C_MAX_DEV_RDY_WAIT_COUNT 20
#define HP206C_DEV_RDY_WAIT_US    20000

struct hp206c_data {
	struct mutex mutex;
	struct i2c_client *client;
	int temp_osr_index;
	int pres_osr_index;
};

struct hp206c_osr_setting {
	u8 osr_mask;
	unsigned int temp_conv_time_us;
	unsigned int pres_conv_time_us;
};

/* Data from Table 5 in datasheet. */
static const struct hp206c_osr_setting hp206c_osr_settings[] = {
	{ HP206C_CMD_ADC_CVT_OSR_4096,	65600,	131100	},
	{ HP206C_CMD_ADC_CVT_OSR_2048,	32800,	65600	},
	{ HP206C_CMD_ADC_CVT_OSR_1024,	16400,	32800	},
	{ HP206C_CMD_ADC_CVT_OSR_512,	8200,	16400	},
	{ HP206C_CMD_ADC_CVT_OSR_256,	4100,	8200	},
	{ HP206C_CMD_ADC_CVT_OSR_128,	2100,	4100	},
};
static const int hp206c_osr_rates[] = { 4096, 2048, 1024, 512, 256, 128 };
static const char hp206c_osr_rates_str[] = "4096 2048 1024 512 256 128";

static inline int hp206c_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, HP206C_CMD_READ_REG | reg);
}

static inline int hp206c_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client,
			HP206C_CMD_WRITE_REG | reg, val);
}

static int hp206c_read_20bit(struct i2c_client *client, u8 cmd)
{
	int ret;
	u8 values[3];

	ret = i2c_smbus_read_i2c_block_data(client, cmd, sizeof(values), values);
	if (ret < 0)
		return ret;
	if (ret != sizeof(values))
		return -EIO;
	return get_unaligned_be24(&values[0]) & GENMASK(19, 0);
}

/* Spin for max 160ms until DEV_RDY is 1, or return error. */
static int hp206c_wait_dev_rdy(struct iio_dev *indio_dev)
{
	int ret;
	int count = 0;
	struct hp206c_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	while (++count <= HP206C_MAX_DEV_RDY_WAIT_COUNT) {
		ret = hp206c_read_reg(client, HP206C_REG_INT_SRC);
		if (ret < 0) {
			dev_err(&indio_dev->dev, "Failed READ_REG INT_SRC: %d\n", ret);
			return ret;
		}
		if (ret & HP206C_FLAG_DEV_RDY)
			return 0;
		usleep_range(HP206C_DEV_RDY_WAIT_US, HP206C_DEV_RDY_WAIT_US * 3 / 2);
	}
	return -ETIMEDOUT;
}

static int hp206c_set_compensation(struct i2c_client *client, bool enabled)
{
	int val;

	val = hp206c_read_reg(client, HP206C_REG_PARA);
	if (val < 0)
		return val;
	if (enabled)
		val |= HP206C_FLAG_CMPS_EN;
	else
		val &= ~HP206C_FLAG_CMPS_EN;

	return hp206c_write_reg(client, HP206C_REG_PARA, val);
}

/* Do a soft reset */
static int hp206c_soft_reset(struct iio_dev *indio_dev)
{
	int ret;
	struct hp206c_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	ret = i2c_smbus_write_byte(client, HP206C_CMD_SOFT_RST);
	if (ret) {
		dev_err(&client->dev, "Failed to reset device: %d\n", ret);
		return ret;
	}

	usleep_range(400, 600);

	ret = hp206c_wait_dev_rdy(indio_dev);
	if (ret) {
		dev_err(&client->dev, "Device not ready after soft reset: %d\n", ret);
		return ret;
	}

	ret = hp206c_set_compensation(client, true);
	if (ret)
		dev_err(&client->dev, "Failed to enable compensation: %d\n", ret);
	return ret;
}

static int hp206c_conv_and_read(struct iio_dev *indio_dev,
				u8 conv_cmd, u8 read_cmd,
				unsigned int sleep_us)
{
	int ret;
	struct hp206c_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	ret = hp206c_wait_dev_rdy(indio_dev);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Device not ready: %d\n", ret);
		return ret;
	}

	ret = i2c_smbus_write_byte(client, conv_cmd);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed convert: %d\n", ret);
		return ret;
	}

	usleep_range(sleep_us, sleep_us * 3 / 2);

	ret = hp206c_wait_dev_rdy(indio_dev);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Device not ready: %d\n", ret);
		return ret;
	}

	ret = hp206c_read_20bit(client, read_cmd);
	if (ret < 0)
		dev_err(&indio_dev->dev, "Failed read: %d\n", ret);

	return ret;
}

static int hp206c_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct hp206c_data *data = iio_priv(indio_dev);
	const struct hp206c_osr_setting *osr_setting;
	u8 conv_cmd;

	mutex_lock(&data->mutex);

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_TEMP:
			*val = hp206c_osr_rates[data->temp_osr_index];
			ret = IIO_VAL_INT;
			break;

		case IIO_PRESSURE:
			*val = hp206c_osr_rates[data->pres_osr_index];
			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
		}
		break;

	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			osr_setting = &hp206c_osr_settings[data->temp_osr_index];
			conv_cmd = HP206C_CMD_ADC_CVT |
					osr_setting->osr_mask |
					HP206C_CMD_ADC_CVT_CHNL_T;
			ret = hp206c_conv_and_read(indio_dev,
					conv_cmd,
					HP206C_CMD_READ_T,
					osr_setting->temp_conv_time_us);
			if (ret >= 0) {
				/* 20 significant bits are provided.
				 * Extend sign over the rest.
				 */
				*val = sign_extend32(ret, 19);
				ret = IIO_VAL_INT;
			}
			break;

		case IIO_PRESSURE:
			osr_setting = &hp206c_osr_settings[data->pres_osr_index];
			conv_cmd = HP206C_CMD_ADC_CVT |
					osr_setting->osr_mask |
					HP206C_CMD_ADC_CVT_CHNL_PT;
			ret = hp206c_conv_and_read(indio_dev,
					conv_cmd,
					HP206C_CMD_READ_P,
					osr_setting->pres_conv_time_us);
			if (ret >= 0) {
				*val = ret;
				ret = IIO_VAL_INT;
			}
			break;
		default:
			ret = -EINVAL;
		}
		break;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			*val = 0;
			*val2 = 10000;
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;

		case IIO_PRESSURE:
			*val = 0;
			*val2 = 1000;
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			ret = -EINVAL;
		}
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&data->mutex);
	return ret;
}

static int hp206c_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret = 0;
	struct hp206c_data *data = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_OVERSAMPLING_RATIO)
		return -EINVAL;
	mutex_lock(&data->mutex);
	switch (chan->type) {
	case IIO_TEMP:
		data->temp_osr_index = find_closest_descending(val,
			hp206c_osr_rates, ARRAY_SIZE(hp206c_osr_rates));
		break;
	case IIO_PRESSURE:
		data->pres_osr_index = find_closest_descending(val,
			hp206c_osr_rates, ARRAY_SIZE(hp206c_osr_rates));
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&data->mutex);
	return ret;
}

static const struct iio_chan_spec hp206c_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	}
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(hp206c_osr_rates_str);

static struct attribute *hp206c_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group hp206c_attribute_group = {
	.attrs = hp206c_attributes,
};

static const struct iio_info hp206c_info = {
	.attrs = &hp206c_attribute_group,
	.read_raw = hp206c_read_raw,
	.write_raw = hp206c_write_raw,
};

static int hp206c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct iio_dev *indio_dev;
	struct hp206c_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE |
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		dev_err(&client->dev, "Adapter does not support "
				"all required i2c functionality\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->mutex);

	indio_dev->info = &hp206c_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = hp206c_channels;
	indio_dev->num_channels = ARRAY_SIZE(hp206c_channels);

	i2c_set_clientdata(client, indio_dev);

	/* Do a soft reset on probe */
	ret = hp206c_soft_reset(indio_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to reset on startup: %d\n", ret);
		return -ENODEV;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id hp206c_id[] = {
	{"hp206c"},
	{}
};
MODULE_DEVICE_TABLE(i2c, hp206c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id hp206c_acpi_match[] = {
	{"HOP206C", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, hp206c_acpi_match);
#endif

static struct i2c_driver hp206c_driver = {
	.probe_new = hp206c_probe,
	.id_table = hp206c_id,
	.driver = {
		.name = "hp206c",
		.acpi_match_table = ACPI_PTR(hp206c_acpi_match),
	},
};

module_i2c_driver(hp206c_driver);

MODULE_DESCRIPTION("HOPERF HP206C precision barometer and altimeter sensor");
MODULE_AUTHOR("Leonard Crestez <leonard.crestez@intel.com>");
MODULE_LICENSE("GPL v2");
