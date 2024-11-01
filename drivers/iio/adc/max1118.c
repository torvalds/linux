// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX1117/MAX1118/MAX1119 8-bit, dual-channel ADCs driver
 *
 * Copyright (c) 2017 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX1117-MAX1119.pdf
 *
 * SPI interface connections
 *
 * SPI                MAXIM
 * Master  Direction  MAX1117/8/9
 * ------  ---------  -----------
 * nCS        -->     CNVST
 * SCK        -->     SCLK
 * MISO       <--     DOUT
 * ------  ---------  -----------
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/regulator/consumer.h>

enum max1118_id {
	max1117,
	max1118,
	max1119,
};

struct max1118 {
	struct spi_device *spi;
	struct mutex lock;
	struct regulator *reg;
	/* Ensure natural alignment of buffer elements */
	struct {
		u8 channels[2];
		s64 ts __aligned(8);
	} scan;

	u8 data __aligned(IIO_DMA_MINALIGN);
};

#define MAX1118_CHANNEL(ch)						\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = (ch),					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.scan_index = ch,					\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = 8,					\
			.storagebits = 8,				\
		},							\
	}

static const struct iio_chan_spec max1118_channels[] = {
	MAX1118_CHANNEL(0),
	MAX1118_CHANNEL(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static int max1118_read(struct iio_dev *indio_dev, int channel)
{
	struct max1118 *adc = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		/*
		 * To select CH1 for conversion, CNVST pin must be brought high
		 * and low for a second time.
		 */
		{
			.len = 0,
			.delay = {	/* > CNVST Low Time 100 ns */
				.value = 1,
				.unit = SPI_DELAY_UNIT_USECS
			},
			.cs_change = 1,
		},
		/*
		 * The acquisition interval begins with the falling edge of
		 * CNVST.  The total acquisition and conversion process takes
		 * <7.5us.
		 */
		{
			.len = 0,
			.delay = {
				.value = 8,
				.unit = SPI_DELAY_UNIT_USECS
			},
		},
		{
			.rx_buf = &adc->data,
			.len = 1,
		},
	};
	int ret;

	if (channel == 0)
		ret = spi_sync_transfer(adc->spi, xfers + 1, 2);
	else
		ret = spi_sync_transfer(adc->spi, xfers, 3);

	if (ret)
		return ret;

	return adc->data;
}

static int max1118_get_vref_mV(struct iio_dev *indio_dev)
{
	struct max1118 *adc = iio_priv(indio_dev);
	const struct spi_device_id *id = spi_get_device_id(adc->spi);
	int vref_uV;

	switch (id->driver_data) {
	case max1117:
		return 2048;
	case max1119:
		return 4096;
	case max1118:
		vref_uV = regulator_get_voltage(adc->reg);
		if (vref_uV < 0)
			return vref_uV;
		return vref_uV / 1000;
	}

	return -ENODEV;
}

static int max1118_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct max1118 *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		*val = max1118_read(indio_dev, chan->channel);
		mutex_unlock(&adc->lock);
		if (*val < 0)
			return *val;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = max1118_get_vref_mV(indio_dev);
		if (*val < 0)
			return *val;
		*val2 = 8;

		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static const struct iio_info max1118_info = {
	.read_raw = max1118_read_raw,
};

static irqreturn_t max1118_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max1118 *adc = iio_priv(indio_dev);
	int scan_index;
	int i = 0;

	mutex_lock(&adc->lock);

	for_each_set_bit(scan_index, indio_dev->active_scan_mask,
			indio_dev->masklength) {
		const struct iio_chan_spec *scan_chan =
				&indio_dev->channels[scan_index];
		int ret = max1118_read(indio_dev, scan_chan->channel);

		if (ret < 0) {
			dev_warn(&adc->spi->dev,
				"failed to get conversion data\n");
			goto out;
		}

		adc->scan.channels[i] = ret;
		i++;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, &adc->scan,
					   iio_get_time_ns(indio_dev));
out:
	mutex_unlock(&adc->lock);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void max1118_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int max1118_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct max1118 *adc;
	const struct spi_device_id *id = spi_get_device_id(spi);
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	mutex_init(&adc->lock);

	if (id->driver_data == max1118) {
		adc->reg = devm_regulator_get(&spi->dev, "vref");
		if (IS_ERR(adc->reg))
			return dev_err_probe(&spi->dev, PTR_ERR(adc->reg),
					     "failed to get vref regulator\n");
		ret = regulator_enable(adc->reg);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev, max1118_reg_disable,
					       adc->reg);
		if (ret)
			return ret;

	}

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &max1118_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max1118_channels;
	indio_dev->num_channels = ARRAY_SIZE(max1118_channels);

	/*
	 * To reinitiate a conversion on CH0, it is necessary to allow for a
	 * conversion to be complete and all of the data to be read out.  Once
	 * a conversion has been completed, the MAX1117/MAX1118/MAX1119 will go
	 * into AutoShutdown mode until the next conversion is initiated.
	 */
	max1118_read(indio_dev, 0);

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      max1118_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id max1118_id[] = {
	{ "max1117", max1117 },
	{ "max1118", max1118 },
	{ "max1119", max1119 },
	{}
};
MODULE_DEVICE_TABLE(spi, max1118_id);

static const struct of_device_id max1118_dt_ids[] = {
	{ .compatible = "maxim,max1117" },
	{ .compatible = "maxim,max1118" },
	{ .compatible = "maxim,max1119" },
	{},
};
MODULE_DEVICE_TABLE(of, max1118_dt_ids);

static struct spi_driver max1118_spi_driver = {
	.driver = {
		.name = "max1118",
		.of_match_table = max1118_dt_ids,
	},
	.probe = max1118_probe,
	.id_table = max1118_id,
};
module_spi_driver(max1118_spi_driver);

MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_DESCRIPTION("MAXIM MAX1117/MAX1118/MAX1119 ADCs driver");
MODULE_LICENSE("GPL v2");
