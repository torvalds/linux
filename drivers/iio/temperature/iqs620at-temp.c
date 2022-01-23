// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS620AT Temperature Sensor
 *
 * Copyright (C) 2019 Jeff LaBundy <jeff@labundy.com>
 */

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/mfd/iqs62x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define IQS620_TEMP_UI_OUT			0x1A

#define IQS620_TEMP_SCALE			1000
#define IQS620_TEMP_OFFSET			(-100)
#define IQS620_TEMP_OFFSET_V3			(-40)

static int iqs620_temp_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct iqs62x_core *iqs62x = iio_device_get_drvdata(indio_dev);
	int ret;
	__le16 val_buf;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_raw_read(iqs62x->regmap, IQS620_TEMP_UI_OUT,
				      &val_buf, sizeof(val_buf));
		if (ret)
			return ret;

		*val = le16_to_cpu(val_buf);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = IQS620_TEMP_SCALE;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OFFSET:
		*val = iqs62x->hw_num < IQS620_HW_NUM_V3 ? IQS620_TEMP_OFFSET
							 : IQS620_TEMP_OFFSET_V3;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static const struct iio_info iqs620_temp_info = {
	.read_raw = &iqs620_temp_read_raw,
};

static const struct iio_chan_spec iqs620_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
	},
};

static int iqs620_temp_probe(struct platform_device *pdev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(pdev->dev.parent);
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&pdev->dev, 0);
	if (!indio_dev)
		return -ENOMEM;

	iio_device_set_drvdata(indio_dev, iqs62x);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = iqs620_temp_channels;
	indio_dev->num_channels = ARRAY_SIZE(iqs620_temp_channels);
	indio_dev->name = iqs62x->dev_desc->dev_name;
	indio_dev->info = &iqs620_temp_info;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static struct platform_driver iqs620_temp_platform_driver = {
	.driver = {
		.name = "iqs620at-temp",
	},
	.probe = iqs620_temp_probe,
};
module_platform_driver(iqs620_temp_platform_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS620AT Temperature Sensor");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iqs620at-temp");
