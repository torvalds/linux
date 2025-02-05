// SPDX-License-Identifier: GPL-2.0+
/*
 * Vishay VEML6040 RGBW light sensor driver
 *
 * Copyright (C) 2024 Sentec AG
 * Author: Arthur Becker <arthur.becker@sentec.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/regmap.h>

/* VEML6040 Configuration Registers
 *
 * SD: Shutdown
 * AF: Auto / Force Mode (Auto Measurements On:0, Off:1)
 * TR: Trigger Measurement (when AF Bit is set)
 * IT: Integration Time
 */
#define VEML6040_CONF_REG 0x000
#define VEML6040_CONF_SD_MSK BIT(0)
#define VEML6040_CONF_AF_MSK BIT(1)
#define VEML6040_CONF_TR_MSK BIT(2)
#define VEML6040_CONF_IT_MSK GENMASK(6, 4)
#define VEML6040_CONF_IT_40_MS 0
#define VEML6040_CONF_IT_80_MS 1
#define VEML6040_CONF_IT_160_MS 2
#define VEML6040_CONF_IT_320_MS 3
#define VEML6040_CONF_IT_640_MS 4
#define VEML6040_CONF_IT_1280_MS 5

/* VEML6040 Read Only Registers */
#define VEML6040_REG_R 0x08
#define VEML6040_REG_G 0x09
#define VEML6040_REG_B 0x0A
#define VEML6040_REG_W 0x0B

static const int veml6040_it_ms[] = { 40, 80, 160, 320, 640, 1280 };

enum veml6040_chan {
	CH_RED,
	CH_GREEN,
	CH_BLUE,
	CH_WHITE,
};

struct veml6040_data {
	struct i2c_client *client;
	struct regmap *regmap;
};

static const struct regmap_config veml6040_regmap_config = {
	.name = "veml6040_regmap",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = VEML6040_REG_W,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int veml6040_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	int ret, reg, it_index;
	struct veml6040_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	struct device *dev = &data->client->dev;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(regmap, chan->address, &reg);
		if (ret) {
			dev_err(dev, "Data read failed: %d\n", ret);
			return ret;
		}
		*val = reg;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_INT_TIME:
		ret = regmap_read(regmap, VEML6040_CONF_REG, &reg);
		if (ret) {
			dev_err(dev, "Data read failed: %d\n", ret);
			return ret;
		}
		it_index = FIELD_GET(VEML6040_CONF_IT_MSK, reg);
		if (it_index >= ARRAY_SIZE(veml6040_it_ms)) {
			dev_err(dev, "Invalid Integration Time Set");
			return -EINVAL;
		}
		*val = veml6040_it_ms[it_index];
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int veml6040_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	struct veml6040_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		for (int i = 0; i < ARRAY_SIZE(veml6040_it_ms); i++) {
			if (veml6040_it_ms[i] != val)
				continue;

			return regmap_update_bits(data->regmap,
					VEML6040_CONF_REG,
					VEML6040_CONF_IT_MSK,
					FIELD_PREP(VEML6040_CONF_IT_MSK, i));
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int veml6040_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(veml6040_it_ms);
		*vals = veml6040_it_ms;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static const struct iio_info veml6040_info = {
	.read_raw = veml6040_read_raw,
	.write_raw = veml6040_write_raw,
	.read_avail = veml6040_read_avail,
};

static const struct iio_chan_spec veml6040_channels[] = {
	{
		.type = IIO_INTENSITY,
		.address = VEML6040_REG_R,
		.channel = CH_RED,
		.channel2 = IIO_MOD_LIGHT_RED,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_type_available =
			BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6040_REG_G,
		.channel = CH_GREEN,
		.channel2 = IIO_MOD_LIGHT_GREEN,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_type_available =
			BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6040_REG_B,
		.channel = CH_BLUE,
		.channel2 = IIO_MOD_LIGHT_BLUE,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_type_available =
			BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6040_REG_W,
		.channel = CH_WHITE,
		.channel2 = IIO_MOD_LIGHT_CLEAR,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_type_available =
			BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static void veml6040_shutdown_action(void *data)
{
	struct veml6040_data *veml6040_data = data;

	regmap_update_bits(veml6040_data->regmap, VEML6040_CONF_REG,
			   VEML6040_CONF_SD_MSK, VEML6040_CONF_SD_MSK);
}

static int veml6040_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct veml6040_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	const int init_config =
		FIELD_PREP(VEML6040_CONF_IT_MSK, VEML6040_CONF_IT_40_MS) |
		FIELD_PREP(VEML6040_CONF_AF_MSK, 0) |
		FIELD_PREP(VEML6040_CONF_SD_MSK, 0);
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "I2C adapter doesn't support plain I2C\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "IIO device allocation failed\n");

	regmap = devm_regmap_init_i2c(client, &veml6040_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Regmap setup failed\n");

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	indio_dev->name = "veml6040";
	indio_dev->info = &veml6040_info;
	indio_dev->channels = veml6040_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml6040_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return ret;

	ret = regmap_write(regmap, VEML6040_CONF_REG, init_config);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Could not set initial config\n");

	ret = devm_add_action_or_reset(dev, veml6040_shutdown_action, data);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id veml6040_id_table[] = {
	{"veml6040"},
	{}
};
MODULE_DEVICE_TABLE(i2c, veml6040_id_table);

static const struct of_device_id veml6040_of_match[] = {
	{.compatible = "vishay,veml6040"},
	{}
};
MODULE_DEVICE_TABLE(of, veml6040_of_match);

static struct i2c_driver veml6040_driver = {
	.probe = veml6040_probe,
	.id_table = veml6040_id_table,
	.driver = {
		.name = "veml6040",
		.of_match_table = veml6040_of_match,
	},
};
module_i2c_driver(veml6040_driver);

MODULE_DESCRIPTION("veml6040 RGBW light sensor driver");
MODULE_AUTHOR("Arthur Becker <arthur.becker@sentec.com>");
MODULE_LICENSE("GPL");
