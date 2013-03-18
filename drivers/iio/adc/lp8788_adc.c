/*
 * TI LP8788 MFD - ADC driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* register address */
#define LP8788_ADC_CONF			0x60
#define LP8788_ADC_RAW			0x61
#define LP8788_ADC_DONE			0x63

#define ADC_CONV_START			1

struct lp8788_adc {
	struct lp8788 *lp;
	struct iio_map *map;
	struct mutex lock;
};

static const int lp8788_scale[LPADC_MAX] = {
	[LPADC_VBATT_5P5] = 1343101,
	[LPADC_VIN_CHG]   = 3052503,
	[LPADC_IBATT]     = 610500,
	[LPADC_IC_TEMP]   = 61050,
	[LPADC_VBATT_6P0] = 1465201,
	[LPADC_VBATT_5P0] = 1221001,
	[LPADC_ADC1]      = 610500,
	[LPADC_ADC2]      = 610500,
	[LPADC_VDD]       = 1025641,
	[LPADC_VCOIN]     = 757020,
	[LPADC_ADC3]      = 610500,
	[LPADC_ADC4]      = 610500,
};

static int lp8788_get_adc_result(struct lp8788_adc *adc, enum lp8788_adc_id id,
				int *val)
{
	unsigned int msb;
	unsigned int lsb;
	unsigned int result;
	u8 data;
	u8 rawdata[2];
	int size = ARRAY_SIZE(rawdata);
	int retry = 5;
	int ret;

	data = (id << 1) | ADC_CONV_START;
	ret = lp8788_write_byte(adc->lp, LP8788_ADC_CONF, data);
	if (ret)
		goto err_io;

	/* retry until adc conversion is done */
	data = 0;
	while (retry--) {
		usleep_range(100, 200);

		ret = lp8788_read_byte(adc->lp, LP8788_ADC_DONE, &data);
		if (ret)
			goto err_io;

		/* conversion done */
		if (data)
			break;
	}

	ret = lp8788_read_multi_bytes(adc->lp, LP8788_ADC_RAW, rawdata, size);
	if (ret)
		goto err_io;

	msb = (rawdata[0] << 4) & 0x00000ff0;
	lsb = (rawdata[1] >> 4) & 0x0000000f;
	result = msb | lsb;
	*val = result;

	return 0;

err_io:
	return ret;
}

static int lp8788_adc_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct lp8788_adc *adc = iio_priv(indio_dev);
	enum lp8788_adc_id id = chan->channel;
	int ret;

	mutex_lock(&adc->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = lp8788_get_adc_result(adc, id, val) ? -EIO : IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = lp8788_scale[id] / 1000000;
		*val2 = lp8788_scale[id] % 1000000;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&adc->lock);

	return ret;
}

static const struct iio_info lp8788_adc_info = {
	.read_raw = &lp8788_adc_read_raw,
	.driver_module = THIS_MODULE,
};

#define LP8788_CHAN(_id, _type) {				\
		.type = _type,					\
		.indexed = 1,					\
		.channel = LPADC_##_id,				\
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |	\
			IIO_CHAN_INFO_SCALE_SEPARATE_BIT,	\
		.datasheet_name = #_id,				\
}

static const struct iio_chan_spec lp8788_adc_channels[] = {
	[LPADC_VBATT_5P5] = LP8788_CHAN(VBATT_5P5, IIO_VOLTAGE),
	[LPADC_VIN_CHG]   = LP8788_CHAN(VIN_CHG, IIO_VOLTAGE),
	[LPADC_IBATT]     = LP8788_CHAN(IBATT, IIO_CURRENT),
	[LPADC_IC_TEMP]   = LP8788_CHAN(IC_TEMP, IIO_TEMP),
	[LPADC_VBATT_6P0] = LP8788_CHAN(VBATT_6P0, IIO_VOLTAGE),
	[LPADC_VBATT_5P0] = LP8788_CHAN(VBATT_5P0, IIO_VOLTAGE),
	[LPADC_ADC1]      = LP8788_CHAN(ADC1, IIO_VOLTAGE),
	[LPADC_ADC2]      = LP8788_CHAN(ADC2, IIO_VOLTAGE),
	[LPADC_VDD]       = LP8788_CHAN(VDD, IIO_VOLTAGE),
	[LPADC_VCOIN]     = LP8788_CHAN(VCOIN, IIO_VOLTAGE),
	[LPADC_ADC3]      = LP8788_CHAN(ADC3, IIO_VOLTAGE),
	[LPADC_ADC4]      = LP8788_CHAN(ADC4, IIO_VOLTAGE),
};

/* default maps used by iio consumer (lp8788-charger driver) */
static struct iio_map lp8788_default_iio_maps[] = {
	{
		.consumer_dev_name = "lp8788-charger",
		.consumer_channel = "lp8788_vbatt_5p0",
		.adc_channel_label = "VBATT_5P0",
	},
	{
		.consumer_dev_name = "lp8788-charger",
		.consumer_channel = "lp8788_adc1",
		.adc_channel_label = "ADC1",
	},
	{ }
};

static int lp8788_iio_map_register(struct iio_dev *indio_dev,
				struct lp8788_platform_data *pdata,
				struct lp8788_adc *adc)
{
	struct iio_map *map;
	int ret;

	map = (!pdata || !pdata->adc_pdata) ?
		lp8788_default_iio_maps : pdata->adc_pdata;

	ret = iio_map_array_register(indio_dev, map);
	if (ret) {
		dev_err(&indio_dev->dev, "iio map err: %d\n", ret);
		return ret;
	}

	adc->map = map;
	return 0;
}

static int lp8788_adc_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	struct iio_dev *indio_dev;
	struct lp8788_adc *adc;
	int ret;

	indio_dev = iio_device_alloc(sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->lp = lp;
	platform_set_drvdata(pdev, indio_dev);

	indio_dev->dev.of_node = pdev->dev.of_node;
	ret = lp8788_iio_map_register(indio_dev, lp->pdata, adc);
	if (ret)
		goto err_iio_map;

	mutex_init(&adc->lock);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &lp8788_adc_info;
	indio_dev->channels = lp8788_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(lp8788_adc_channels);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "iio dev register err: %d\n", ret);
		goto err_iio_device;
	}

	return 0;

err_iio_device:
	iio_map_array_unregister(indio_dev);
err_iio_map:
	iio_device_free(indio_dev);
	return ret;
}

static int lp8788_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	iio_map_array_unregister(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct platform_driver lp8788_adc_driver = {
	.probe = lp8788_adc_probe,
	.remove = lp8788_adc_remove,
	.driver = {
		.name = LP8788_DEV_ADC,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(lp8788_adc_driver);

MODULE_DESCRIPTION("Texas Instruments LP8788 ADC Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-adc");
