// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADC12130/ADC12132/ADC12138 12-bit plus sign ADC driver
 *
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Datasheet: http://www.ti.com/lit/ds/symlink/adc12138.pdf
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/regulator/consumer.h>

#define ADC12138_MODE_AUTO_CAL			0x08
#define ADC12138_MODE_READ_STATUS		0x0c
#define ADC12138_MODE_ACQUISITION_TIME_6	0x0e
#define ADC12138_MODE_ACQUISITION_TIME_10	0x4e
#define ADC12138_MODE_ACQUISITION_TIME_18	0x8e
#define ADC12138_MODE_ACQUISITION_TIME_34	0xce

#define ADC12138_STATUS_CAL			BIT(6)

enum {
	adc12130,
	adc12132,
	adc12138,
};

struct adc12138 {
	struct spi_device *spi;
	unsigned int id;
	/* conversion clock */
	struct clk *cclk;
	/* positive analog voltage reference */
	struct regulator *vref_p;
	/* negative analog voltage reference */
	struct regulator *vref_n;
	struct mutex lock;
	struct completion complete;
	/* The number of cclk periods for the S/H's acquisition time */
	unsigned int acquisition_time;

	u8 tx_buf[2] ____cacheline_aligned;
	u8 rx_buf[2];
};

#define ADC12138_VOLTAGE_CHANNEL(chan)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = chan,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)	\
					| BIT(IIO_CHAN_INFO_OFFSET),	\
		.scan_index = chan,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 13,					\
			.storagebits = 16,				\
			.shift = 3,					\
			.endianness = IIO_BE,				\
		},							\
	}

#define ADC12138_VOLTAGE_CHANNEL_DIFF(chan1, chan2, si)			\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = (chan1),					\
		.channel2 = (chan2),					\
		.differential = 1,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)	\
					| BIT(IIO_CHAN_INFO_OFFSET),	\
		.scan_index = si,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 13,					\
			.storagebits = 16,				\
			.shift = 3,					\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec adc12132_channels[] = {
	ADC12138_VOLTAGE_CHANNEL(0),
	ADC12138_VOLTAGE_CHANNEL(1),
	ADC12138_VOLTAGE_CHANNEL_DIFF(0, 1, 2),
	ADC12138_VOLTAGE_CHANNEL_DIFF(1, 0, 3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec adc12138_channels[] = {
	ADC12138_VOLTAGE_CHANNEL(0),
	ADC12138_VOLTAGE_CHANNEL(1),
	ADC12138_VOLTAGE_CHANNEL(2),
	ADC12138_VOLTAGE_CHANNEL(3),
	ADC12138_VOLTAGE_CHANNEL(4),
	ADC12138_VOLTAGE_CHANNEL(5),
	ADC12138_VOLTAGE_CHANNEL(6),
	ADC12138_VOLTAGE_CHANNEL(7),
	ADC12138_VOLTAGE_CHANNEL_DIFF(0, 1, 8),
	ADC12138_VOLTAGE_CHANNEL_DIFF(1, 0, 9),
	ADC12138_VOLTAGE_CHANNEL_DIFF(2, 3, 10),
	ADC12138_VOLTAGE_CHANNEL_DIFF(3, 2, 11),
	ADC12138_VOLTAGE_CHANNEL_DIFF(4, 5, 12),
	ADC12138_VOLTAGE_CHANNEL_DIFF(5, 4, 13),
	ADC12138_VOLTAGE_CHANNEL_DIFF(6, 7, 14),
	ADC12138_VOLTAGE_CHANNEL_DIFF(7, 6, 15),
	IIO_CHAN_SOFT_TIMESTAMP(16),
};

static int adc12138_mode_programming(struct adc12138 *adc, u8 mode,
				     void *rx_buf, int len)
{
	struct spi_transfer xfer = {
		.tx_buf = adc->tx_buf,
		.rx_buf = adc->rx_buf,
		.len = len,
	};
	int ret;

	/* Skip unused bits for ADC12130 and ADC12132 */
	if (adc->id != adc12138)
		mode = (mode & 0xc0) | ((mode & 0x0f) << 2);

	adc->tx_buf[0] = mode;

	ret = spi_sync_transfer(adc->spi, &xfer, 1);
	if (ret)
		return ret;

	memcpy(rx_buf, adc->rx_buf, len);

	return 0;
}

static int adc12138_read_status(struct adc12138 *adc)
{
	u8 rx_buf[2];
	int ret;

	ret = adc12138_mode_programming(adc, ADC12138_MODE_READ_STATUS,
					rx_buf, 2);
	if (ret)
		return ret;

	return (rx_buf[0] << 1) | (rx_buf[1] >> 7);
}

static int __adc12138_start_conv(struct adc12138 *adc,
				 struct iio_chan_spec const *channel,
				 void *data, int len)

{
	static const u8 ch_to_mux[] = { 0, 4, 1, 5, 2, 6, 3, 7 };
	u8 mode = (ch_to_mux[channel->channel] << 4) |
		  (channel->differential ? 0 : 0x80);

	return adc12138_mode_programming(adc, mode, data, len);
}

static int adc12138_start_conv(struct adc12138 *adc,
			       struct iio_chan_spec const *channel)
{
	u8 trash;

	return __adc12138_start_conv(adc, channel, &trash, 1);
}

static int adc12138_start_and_read_conv(struct adc12138 *adc,
					struct iio_chan_spec const *channel,
					__be16 *data)
{
	return __adc12138_start_conv(adc, channel, data, 2);
}

static int adc12138_read_conv_data(struct adc12138 *adc, __be16 *value)
{
	/* Issue a read status instruction and read previous conversion data */
	return adc12138_mode_programming(adc, ADC12138_MODE_READ_STATUS,
					 value, sizeof(*value));
}

static int adc12138_wait_eoc(struct adc12138 *adc, unsigned long timeout)
{
	if (!wait_for_completion_timeout(&adc->complete, timeout))
		return -ETIMEDOUT;

	return 0;
}

static int adc12138_adc_conversion(struct adc12138 *adc,
				   struct iio_chan_spec const *channel,
				   __be16 *value)
{
	int ret;

	reinit_completion(&adc->complete);

	ret = adc12138_start_conv(adc, channel);
	if (ret)
		return ret;

	ret = adc12138_wait_eoc(adc, msecs_to_jiffies(100));
	if (ret)
		return ret;

	return adc12138_read_conv_data(adc, value);
}

static int adc12138_read_raw(struct iio_dev *iio,
			     struct iio_chan_spec const *channel, int *value,
			     int *shift, long mask)
{
	struct adc12138 *adc = iio_priv(iio);
	int ret;
	__be16 data;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		ret = adc12138_adc_conversion(adc, channel, &data);
		mutex_unlock(&adc->lock);
		if (ret)
			return ret;

		*value = sign_extend32(be16_to_cpu(data) >> 3, 12);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(adc->vref_p);
		if (ret < 0)
			return ret;
		*value = ret;

		if (!IS_ERR(adc->vref_n)) {
			ret = regulator_get_voltage(adc->vref_n);
			if (ret < 0)
				return ret;
			*value -= ret;
		}

		/* convert regulator output voltage to mV */
		*value /= 1000;
		*shift = channel->scan_type.realbits - 1;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		if (!IS_ERR(adc->vref_n)) {
			*value = regulator_get_voltage(adc->vref_n);
			if (*value < 0)
				return *value;
		} else {
			*value = 0;
		}

		/* convert regulator output voltage to mV */
		*value /= 1000;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info adc12138_info = {
	.read_raw = adc12138_read_raw,
};

static int adc12138_init(struct adc12138 *adc)
{
	int ret;
	int status;
	u8 mode;
	u8 trash;

	reinit_completion(&adc->complete);

	ret = adc12138_mode_programming(adc, ADC12138_MODE_AUTO_CAL, &trash, 1);
	if (ret)
		return ret;

	/* data output at this time has no significance */
	status = adc12138_read_status(adc);
	if (status < 0)
		return status;

	adc12138_wait_eoc(adc, msecs_to_jiffies(100));

	status = adc12138_read_status(adc);
	if (status & ADC12138_STATUS_CAL) {
		dev_warn(&adc->spi->dev,
			"Auto Cal sequence is still in progress: %#x\n",
			status);
		return -EIO;
	}

	switch (adc->acquisition_time) {
	case 6:
		mode = ADC12138_MODE_ACQUISITION_TIME_6;
		break;
	case 10:
		mode = ADC12138_MODE_ACQUISITION_TIME_10;
		break;
	case 18:
		mode = ADC12138_MODE_ACQUISITION_TIME_18;
		break;
	case 34:
		mode = ADC12138_MODE_ACQUISITION_TIME_34;
		break;
	default:
		return -EINVAL;
	}

	return adc12138_mode_programming(adc, mode, &trash, 1);
}

static irqreturn_t adc12138_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adc12138 *adc = iio_priv(indio_dev);
	__be16 data[20] = { }; /* 16x 2 bytes ADC data + 8 bytes timestamp */
	__be16 trash;
	int ret;
	int scan_index;
	int i = 0;

	mutex_lock(&adc->lock);

	for_each_set_bit(scan_index, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		const struct iio_chan_spec *scan_chan =
				&indio_dev->channels[scan_index];

		reinit_completion(&adc->complete);

		ret = adc12138_start_and_read_conv(adc, scan_chan,
						   i ? &data[i - 1] : &trash);
		if (ret) {
			dev_warn(&adc->spi->dev,
				 "failed to start conversion\n");
			goto out;
		}

		ret = adc12138_wait_eoc(adc, msecs_to_jiffies(100));
		if (ret) {
			dev_warn(&adc->spi->dev, "wait eoc timeout\n");
			goto out;
		}

		i++;
	}

	if (i) {
		ret = adc12138_read_conv_data(adc, &data[i - 1]);
		if (ret) {
			dev_warn(&adc->spi->dev,
				 "failed to get conversion data\n");
			goto out;
		}
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data,
					   iio_get_time_ns(indio_dev));
out:
	mutex_unlock(&adc->lock);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t adc12138_eoc_handler(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct adc12138 *adc = iio_priv(indio_dev);

	complete(&adc->complete);

	return IRQ_HANDLED;
}

static int adc12138_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adc12138 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	adc->id = spi_get_device_id(spi)->driver_data;
	mutex_init(&adc->lock);
	init_completion(&adc->complete);

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adc12138_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	switch (adc->id) {
	case adc12130:
	case adc12132:
		indio_dev->channels = adc12132_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc12132_channels);
		break;
	case adc12138:
		indio_dev->channels = adc12138_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc12138_channels);
		break;
	default:
		return -EINVAL;
	}

	ret = of_property_read_u32(spi->dev.of_node, "ti,acquisition-time",
				   &adc->acquisition_time);
	if (ret)
		adc->acquisition_time = 10;

	adc->cclk = devm_clk_get(&spi->dev, NULL);
	if (IS_ERR(adc->cclk))
		return PTR_ERR(adc->cclk);

	adc->vref_p = devm_regulator_get(&spi->dev, "vref-p");
	if (IS_ERR(adc->vref_p))
		return PTR_ERR(adc->vref_p);

	adc->vref_n = devm_regulator_get_optional(&spi->dev, "vref-n");
	if (IS_ERR(adc->vref_n)) {
		/*
		 * Assume vref_n is 0V if an optional regulator is not
		 * specified, otherwise return the error code.
		 */
		ret = PTR_ERR(adc->vref_n);
		if (ret != -ENODEV)
			return ret;
	}

	ret = devm_request_irq(&spi->dev, spi->irq, adc12138_eoc_handler,
			       IRQF_TRIGGER_RISING, indio_dev->name, indio_dev);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adc->cclk);
	if (ret)
		return ret;

	ret = regulator_enable(adc->vref_p);
	if (ret)
		goto err_clk_disable;

	if (!IS_ERR(adc->vref_n)) {
		ret = regulator_enable(adc->vref_n);
		if (ret)
			goto err_vref_p_disable;
	}

	ret = adc12138_init(adc);
	if (ret)
		goto err_vref_n_disable;

	spi_set_drvdata(spi, indio_dev);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 adc12138_trigger_handler, NULL);
	if (ret)
		goto err_vref_n_disable;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_buffer_cleanup;

	return 0;
err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_vref_n_disable:
	if (!IS_ERR(adc->vref_n))
		regulator_disable(adc->vref_n);
err_vref_p_disable:
	regulator_disable(adc->vref_p);
err_clk_disable:
	clk_disable_unprepare(adc->cclk);

	return ret;
}

static int adc12138_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adc12138 *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (!IS_ERR(adc->vref_n))
		regulator_disable(adc->vref_n);
	regulator_disable(adc->vref_p);
	clk_disable_unprepare(adc->cclk);

	return 0;
}

#ifdef CONFIG_OF

static const struct of_device_id adc12138_dt_ids[] = {
	{ .compatible = "ti,adc12130", },
	{ .compatible = "ti,adc12132", },
	{ .compatible = "ti,adc12138", },
	{}
};
MODULE_DEVICE_TABLE(of, adc12138_dt_ids);

#endif

static const struct spi_device_id adc12138_id[] = {
	{ "adc12130", adc12130 },
	{ "adc12132", adc12132 },
	{ "adc12138", adc12138 },
	{}
};
MODULE_DEVICE_TABLE(spi, adc12138_id);

static struct spi_driver adc12138_driver = {
	.driver = {
		.name = "adc12138",
		.of_match_table = of_match_ptr(adc12138_dt_ids),
	},
	.probe = adc12138_probe,
	.remove = adc12138_remove,
	.id_table = adc12138_id,
};
module_spi_driver(adc12138_driver);

MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_DESCRIPTION("ADC12130/ADC12132/ADC12138 driver");
MODULE_LICENSE("GPL v2");
