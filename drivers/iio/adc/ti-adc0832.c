/*
 * ADC0831/ADC0832/ADC0834/ADC0838 8-bit ADC driver
 *
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Datasheet: http://www.ti.com/lit/ds/symlink/adc0832-n.pdf
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

enum {
	adc0831,
	adc0832,
	adc0834,
	adc0838,
};

struct adc0832 {
	struct spi_device *spi;
	struct regulator *reg;
	struct mutex lock;
	u8 mux_bits;

	u8 tx_buf[2] ____cacheline_aligned;
	u8 rx_buf[2];
};

#define ADC0832_VOLTAGE_CHANNEL(chan)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = chan,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)	\
	}

#define ADC0832_VOLTAGE_CHANNEL_DIFF(chan1, chan2)			\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = (chan1),					\
		.channel2 = (chan2),					\
		.differential = 1,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)	\
	}

static const struct iio_chan_spec adc0831_channels[] = {
	ADC0832_VOLTAGE_CHANNEL_DIFF(0, 1),
};

static const struct iio_chan_spec adc0832_channels[] = {
	ADC0832_VOLTAGE_CHANNEL(0),
	ADC0832_VOLTAGE_CHANNEL(1),
	ADC0832_VOLTAGE_CHANNEL_DIFF(0, 1),
	ADC0832_VOLTAGE_CHANNEL_DIFF(1, 0),
};

static const struct iio_chan_spec adc0834_channels[] = {
	ADC0832_VOLTAGE_CHANNEL(0),
	ADC0832_VOLTAGE_CHANNEL(1),
	ADC0832_VOLTAGE_CHANNEL(2),
	ADC0832_VOLTAGE_CHANNEL(3),
	ADC0832_VOLTAGE_CHANNEL_DIFF(0, 1),
	ADC0832_VOLTAGE_CHANNEL_DIFF(1, 0),
	ADC0832_VOLTAGE_CHANNEL_DIFF(2, 3),
	ADC0832_VOLTAGE_CHANNEL_DIFF(3, 2),
};

static const struct iio_chan_spec adc0838_channels[] = {
	ADC0832_VOLTAGE_CHANNEL(0),
	ADC0832_VOLTAGE_CHANNEL(1),
	ADC0832_VOLTAGE_CHANNEL(2),
	ADC0832_VOLTAGE_CHANNEL(3),
	ADC0832_VOLTAGE_CHANNEL(4),
	ADC0832_VOLTAGE_CHANNEL(5),
	ADC0832_VOLTAGE_CHANNEL(6),
	ADC0832_VOLTAGE_CHANNEL(7),
	ADC0832_VOLTAGE_CHANNEL_DIFF(0, 1),
	ADC0832_VOLTAGE_CHANNEL_DIFF(1, 0),
	ADC0832_VOLTAGE_CHANNEL_DIFF(2, 3),
	ADC0832_VOLTAGE_CHANNEL_DIFF(3, 2),
	ADC0832_VOLTAGE_CHANNEL_DIFF(4, 5),
	ADC0832_VOLTAGE_CHANNEL_DIFF(5, 4),
	ADC0832_VOLTAGE_CHANNEL_DIFF(6, 7),
	ADC0832_VOLTAGE_CHANNEL_DIFF(7, 6),
};

static int adc0831_adc_conversion(struct adc0832 *adc)
{
	struct spi_device *spi = adc->spi;
	int ret;

	ret = spi_read(spi, &adc->rx_buf, 2);
	if (ret)
		return ret;

	/*
	 * Skip TRI-STATE and a leading zero
	 */
	return (adc->rx_buf[0] << 2 & 0xff) | (adc->rx_buf[1] >> 6);
}

static int adc0832_adc_conversion(struct adc0832 *adc, int channel,
				bool differential)
{
	struct spi_device *spi = adc->spi;
	struct spi_transfer xfer = {
		.tx_buf = adc->tx_buf,
		.rx_buf = adc->rx_buf,
		.len = 2,
	};
	int ret;

	if (!adc->mux_bits)
		return adc0831_adc_conversion(adc);

	/* start bit */
	adc->tx_buf[0] = 1 << (adc->mux_bits + 1);
	/* single-ended or differential */
	adc->tx_buf[0] |= differential ? 0 : (1 << adc->mux_bits);
	/* odd / sign */
	adc->tx_buf[0] |= (channel % 2) << (adc->mux_bits - 1);
	/* select */
	if (adc->mux_bits > 1)
		adc->tx_buf[0] |= channel / 2;

	/* align Data output BIT7 (MSB) to 8-bit boundary */
	adc->tx_buf[0] <<= 1;

	ret = spi_sync_transfer(spi, &xfer, 1);
	if (ret)
		return ret;

	return adc->rx_buf[1];
}

static int adc0832_read_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int *value,
			int *shift, long mask)
{
	struct adc0832 *adc = iio_priv(iio);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		*value = adc0832_adc_conversion(adc, channel->channel,
						channel->differential);
		mutex_unlock(&adc->lock);
		if (*value < 0)
			return *value;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*value = regulator_get_voltage(adc->reg);
		if (*value < 0)
			return *value;

		/* convert regulator output voltage to mV */
		*value /= 1000;
		*shift = 8;

		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static const struct iio_info adc0832_info = {
	.read_raw = adc0832_read_raw,
	.driver_module = THIS_MODULE,
};

static int adc0832_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adc0832 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	mutex_init(&adc->lock);

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adc0832_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	switch (spi_get_device_id(spi)->driver_data) {
	case adc0831:
		adc->mux_bits = 0;
		indio_dev->channels = adc0831_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc0831_channels);
		break;
	case adc0832:
		adc->mux_bits = 1;
		indio_dev->channels = adc0832_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc0832_channels);
		break;
	case adc0834:
		adc->mux_bits = 2;
		indio_dev->channels = adc0834_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc0834_channels);
		break;
	case adc0838:
		adc->mux_bits = 3;
		indio_dev->channels = adc0838_channels;
		indio_dev->num_channels = ARRAY_SIZE(adc0838_channels);
		break;
	default:
		return -EINVAL;
	}

	adc->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(adc->reg))
		return PTR_ERR(adc->reg);

	ret = regulator_enable(adc->reg);
	if (ret)
		return ret;

	spi_set_drvdata(spi, indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret)
		regulator_disable(adc->reg);

	return ret;
}

static int adc0832_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adc0832 *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(adc->reg);

	return 0;
}

#ifdef CONFIG_OF

static const struct of_device_id adc0832_dt_ids[] = {
	{ .compatible = "ti,adc0831", },
	{ .compatible = "ti,adc0832", },
	{ .compatible = "ti,adc0834", },
	{ .compatible = "ti,adc0838", },
	{}
};
MODULE_DEVICE_TABLE(of, adc0832_dt_ids);

#endif

static const struct spi_device_id adc0832_id[] = {
	{ "adc0831", adc0831 },
	{ "adc0832", adc0832 },
	{ "adc0834", adc0834 },
	{ "adc0838", adc0838 },
	{}
};
MODULE_DEVICE_TABLE(spi, adc0832_id);

static struct spi_driver adc0832_driver = {
	.driver = {
		.name = "adc0832",
		.of_match_table = of_match_ptr(adc0832_dt_ids),
	},
	.probe = adc0832_probe,
	.remove = adc0832_remove,
	.id_table = adc0832_id,
};
module_spi_driver(adc0832_driver);

MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_DESCRIPTION("ADC0831/ADC0832/ADC0834/ADC0838 driver");
MODULE_LICENSE("GPL v2");
