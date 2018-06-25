/*
 * ADXL345 3-Axis Digital Accelerometer IIO core driver
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 */

#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>

#include "adxl345.h"

#define ADXL345_REG_DEVID		0x00
#define ADXL345_REG_POWER_CTL		0x2D
#define ADXL345_REG_DATA_FORMAT		0x31
#define ADXL345_REG_DATAX0		0x32
#define ADXL345_REG_DATAY0		0x34
#define ADXL345_REG_DATAZ0		0x36
#define ADXL345_REG_DATA_AXIS(index)	\
	(ADXL345_REG_DATAX0 + (index) * sizeof(__le16))

#define ADXL345_POWER_CTL_MEASURE	BIT(3)
#define ADXL345_POWER_CTL_STANDBY	0x00

#define ADXL345_DATA_FORMAT_FULL_RES	BIT(3) /* Up to 13-bits resolution */
#define ADXL345_DATA_FORMAT_2G		0
#define ADXL345_DATA_FORMAT_4G		1
#define ADXL345_DATA_FORMAT_8G		2
#define ADXL345_DATA_FORMAT_16G		3

#define ADXL345_DEVID			0xE5

/*
 * In full-resolution mode, scale factor is maintained at ~4 mg/LSB
 * in all g ranges.
 *
 * At +/- 16g with 13-bit resolution, scale is computed as:
 * (16 + 16) * 9.81 / (2^13 - 1) = 0.0383
 */
static const int adxl345_uscale = 38300;

struct adxl345_data {
	struct regmap *regmap;
	u8 data_range;
};

#define ADXL345_CHANNEL(index, axis) {					\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.address = index,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
}

static const struct iio_chan_spec adxl345_channels[] = {
	ADXL345_CHANNEL(0, X),
	ADXL345_CHANNEL(1, Y),
	ADXL345_CHANNEL(2, Z),
};

static int adxl345_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct adxl345_data *data = iio_priv(indio_dev);
	__le16 accel;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * Data is stored in adjacent registers:
		 * ADXL345_REG_DATA(X0/Y0/Z0) contain the least significant byte
		 * and ADXL345_REG_DATA(X0/Y0/Z0) + 1 the most significant byte
		 */
		ret = regmap_bulk_read(data->regmap,
				       ADXL345_REG_DATA_AXIS(chan->address),
				       &accel, sizeof(accel));
		if (ret < 0)
			return ret;

		*val = sign_extend32(le16_to_cpu(accel), 12);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = adxl345_uscale;

		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static const struct iio_info adxl345_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= adxl345_read_raw,
};

int adxl345_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name)
{
	struct adxl345_data *data;
	struct iio_dev *indio_dev;
	u32 regval;
	int ret;

	ret = regmap_read(regmap, ADXL345_REG_DEVID, &regval);
	if (ret < 0) {
		dev_err(dev, "Error reading device ID: %d\n", ret);
		return ret;
	}

	if (regval != ADXL345_DEVID) {
		dev_err(dev, "Invalid device ID: %x, expected %x\n",
			regval, ADXL345_DEVID);
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;
	/* Enable full-resolution mode */
	data->data_range = ADXL345_DATA_FORMAT_FULL_RES;

	ret = regmap_write(data->regmap, ADXL345_REG_DATA_FORMAT,
			   data->data_range);
	if (ret < 0) {
		dev_err(dev, "Failed to set data range: %d\n", ret);
		return ret;
	}

	indio_dev->dev.parent = dev;
	indio_dev->name = name;
	indio_dev->info = &adxl345_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxl345_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl345_channels);

	/* Enable measurement mode */
	ret = regmap_write(data->regmap, ADXL345_REG_POWER_CTL,
			   ADXL345_POWER_CTL_MEASURE);
	if (ret < 0) {
		dev_err(dev, "Failed to enable measurement mode: %d\n", ret);
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "iio_device_register failed: %d\n", ret);
		regmap_write(data->regmap, ADXL345_REG_POWER_CTL,
			     ADXL345_POWER_CTL_STANDBY);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adxl345_core_probe);

int adxl345_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adxl345_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	return regmap_write(data->regmap, ADXL345_REG_POWER_CTL,
			    ADXL345_POWER_CTL_STANDBY);
}
EXPORT_SYMBOL_GPL(adxl345_core_remove);

MODULE_AUTHOR("Eva Rachel Retuya <eraretuya@gmail.com>");
MODULE_DESCRIPTION("ADXL345 3-Axis Digital Accelerometer core driver");
MODULE_LICENSE("GPL v2");
