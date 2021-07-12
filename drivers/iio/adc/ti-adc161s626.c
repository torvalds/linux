// SPDX-License-Identifier: GPL-2.0+
/*
 * ti-adc161s626.c - Texas Instruments ADC161S626 1-channel differential ADC
 *
 * ADC Devices Supported:
 *  adc141s626 - 14-bit ADC
 *  adc161s626 - 16-bit ADC
 *
 * Copyright (C) 2016-2018
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regulator/consumer.h>

#define TI_ADC_DRV_NAME	"ti-adc161s626"

enum {
	TI_ADC141S626,
	TI_ADC161S626,
};

static const struct iio_chan_spec ti_adc141s626_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 14,
			.storagebits = 16,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec ti_adc161s626_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

struct ti_adc_data {
	struct iio_dev *indio_dev;
	struct spi_device *spi;
	struct regulator *ref;

	u8 read_size;
	u8 shift;

	u8 buffer[16] ____cacheline_aligned;
};

static int ti_adc_read_measurement(struct ti_adc_data *data,
				   struct iio_chan_spec const *chan, int *val)
{
	int ret;

	switch (data->read_size) {
	case 2: {
		__be16 buf;

		ret = spi_read(data->spi, (void *) &buf, 2);
		if (ret)
			return ret;

		*val = be16_to_cpu(buf);
		break;
	}
	case 3: {
		__be32 buf;

		ret = spi_read(data->spi, (void *) &buf, 3);
		if (ret)
			return ret;

		*val = be32_to_cpu(buf) >> 8;
		break;
	}
	default:
		return -EINVAL;
	}

	*val = sign_extend32(*val >> data->shift, chan->scan_type.realbits - 1);

	return 0;
}

static irqreturn_t ti_adc_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ti_adc_data *data = iio_priv(indio_dev);
	int ret;

	ret = ti_adc_read_measurement(data, &indio_dev->channels[0],
				     (int *) &data->buffer);
	if (!ret)
		iio_push_to_buffers_with_timestamp(indio_dev,
					data->buffer,
					iio_get_time_ns(indio_dev));

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ti_adc_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ti_adc_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = ti_adc_read_measurement(data, chan, val);
		iio_device_release_direct_mode(indio_dev);

		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(data->ref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		*val = 1 << (chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	}

	return 0;
}

static const struct iio_info ti_adc_info = {
	.read_raw = ti_adc_read_raw,
};

static void ti_adc_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int ti_adc_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ti_adc_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &ti_adc_info;
	indio_dev->name = TI_ADC_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data = iio_priv(indio_dev);
	data->spi = spi;

	switch (spi_get_device_id(spi)->driver_data) {
	case TI_ADC141S626:
		indio_dev->channels = ti_adc141s626_channels;
		indio_dev->num_channels = ARRAY_SIZE(ti_adc141s626_channels);
		data->shift = 0;
		data->read_size = 2;
		break;
	case TI_ADC161S626:
		indio_dev->channels = ti_adc161s626_channels;
		indio_dev->num_channels = ARRAY_SIZE(ti_adc161s626_channels);
		data->shift = 6;
		data->read_size = 3;
		break;
	}

	data->ref = devm_regulator_get(&spi->dev, "vdda");
	if (IS_ERR(data->ref))
		return PTR_ERR(data->ref);

	ret = regulator_enable(data->ref);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ti_adc_reg_disable,
				       data->ref);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      ti_adc_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ti_adc_dt_ids[] = {
	{ .compatible = "ti,adc141s626", },
	{ .compatible = "ti,adc161s626", },
	{}
};
MODULE_DEVICE_TABLE(of, ti_adc_dt_ids);

static const struct spi_device_id ti_adc_id[] = {
	{"adc141s626", TI_ADC141S626},
	{"adc161s626", TI_ADC161S626},
	{},
};
MODULE_DEVICE_TABLE(spi, ti_adc_id);

static struct spi_driver ti_adc_driver = {
	.driver = {
		.name	= TI_ADC_DRV_NAME,
		.of_match_table = ti_adc_dt_ids,
	},
	.probe		= ti_adc_probe,
	.id_table	= ti_adc_id,
};
module_spi_driver(ti_adc_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("Texas Instruments ADC1x1S 1-channel differential ADC");
MODULE_LICENSE("GPL");
