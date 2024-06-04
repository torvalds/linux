// SPDX-License-Identifier: GPL-2.0
/*
 * ScioSense ENS160 multi-gas sensor driver
 *
 * Copyright (c) 2024 Gustavo Silva <gustavograzs@gmail.com>
 *
 * Datasheet:
 *  https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf
 */

#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "ens160.h"

#define ENS160_PART_ID 0x160

#define ENS160_BOOTING_TIME_MS 10U

#define ENS160_REG_PART_ID		0x00

#define ENS160_REG_OPMODE		0x10

#define ENS160_REG_MODE_DEEP_SLEEP	0x00
#define ENS160_REG_MODE_IDLE		0x01
#define ENS160_REG_MODE_STANDARD	0x02
#define ENS160_REG_MODE_RESET		0xF0

#define ENS160_REG_COMMAND		0x12
#define ENS160_REG_COMMAND_GET_APPVER	0x0E
#define ENS160_REG_COMMAND_CLRGPR	0xCC

#define ENS160_REG_TEMP_IN		0x13
#define ENS160_REG_RH_IN		0x15
#define ENS160_REG_DEVICE_STATUS	0x20
#define ENS160_REG_DATA_AQI		0x21
#define ENS160_REG_DATA_TVOC		0x22
#define ENS160_REG_DATA_ECO2		0x24
#define ENS160_REG_DATA_T		0x30
#define ENS160_REG_DATA_RH		0x32
#define ENS160_REG_GPR_READ4		0x4C

#define ENS160_STATUS_VALIDITY_FLAG	GENMASK(3, 2)

#define ENS160_STATUS_NORMAL		0x00

struct ens160_data {
	struct regmap *regmap;
	u8 fw_version[3] __aligned(IIO_DMA_MINALIGN);
	__le16 buf;
};

static const struct iio_chan_spec ens160_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.address = ENS160_REG_DATA_TVOC,
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.address = ENS160_REG_DATA_ECO2,
	},
};

static int ens160_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ens160_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(data->regmap, chan->address,
				       &data->buf, sizeof(data->buf));
		if (ret)
			return ret;
		*val = le16_to_cpu(data->buf);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel2) {
		case IIO_MOD_CO2:
			/* The sensor reads CO2 data as ppm */
			*val = 0;
			*val2 = 100;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MOD_VOC:
			/* The sensor reads VOC data as ppb */
			*val = 0;
			*val2 = 100;
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ens160_set_mode(struct ens160_data *data, u8 mode)
{
	int ret;

	ret = regmap_write(data->regmap, ENS160_REG_OPMODE, mode);
	if (ret)
		return ret;

	msleep(ENS160_BOOTING_TIME_MS);

	return 0;
}

static void ens160_set_idle(void *data)
{
	ens160_set_mode(data, ENS160_REG_MODE_IDLE);
}

static int ens160_chip_init(struct ens160_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int status;
	int ret;

	ret = ens160_set_mode(data, ENS160_REG_MODE_RESET);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, ENS160_REG_PART_ID, &data->buf,
			       sizeof(data->buf));
	if (ret)
		return ret;

	if (le16_to_cpu(data->buf) != ENS160_PART_ID)
		return -ENODEV;

	ret = ens160_set_mode(data, ENS160_REG_MODE_IDLE);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, ENS160_REG_COMMAND,
			   ENS160_REG_COMMAND_CLRGPR);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, ENS160_REG_COMMAND,
			   ENS160_REG_COMMAND_GET_APPVER);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, ENS160_REG_GPR_READ4,
			       data->fw_version, sizeof(data->fw_version));
	if (ret)
		return ret;

	dev_info(dev, "firmware version: %u.%u.%u\n", data->fw_version[2],
		 data->fw_version[1], data->fw_version[0]);

	ret = ens160_set_mode(data, ENS160_REG_MODE_STANDARD);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, ens160_set_idle, data);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, ENS160_REG_DEVICE_STATUS, &status);
	if (ret)
		return ret;

	if (FIELD_GET(ENS160_STATUS_VALIDITY_FLAG, status)
	    != ENS160_STATUS_NORMAL)
		return -EINVAL;

	return 0;
}

static const struct iio_info ens160_info = {
	.read_raw = ens160_read_raw,
};

int devm_ens160_core_probe(struct device *dev, struct regmap *regmap,
			   const char *name)
{
	struct ens160_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;

	indio_dev->name = name;
	indio_dev->info = &ens160_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ens160_channels;
	indio_dev->num_channels = ARRAY_SIZE(ens160_channels);

	ret = ens160_chip_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "chip initialization failed\n");

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS(devm_ens160_core_probe, IIO_ENS160);

MODULE_AUTHOR("Gustavo Silva <gustavograzs@gmail.com>");
MODULE_DESCRIPTION("ScioSense ENS160 driver");
MODULE_LICENSE("GPL v2");
