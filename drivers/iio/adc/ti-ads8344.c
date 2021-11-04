// SPDX-License-Identifier: GPL-2.0+
/*
 * ADS8344 16-bit 8-Channel ADC driver
 *
 * Author: Gregory CLEMENT <gregory.clement@bootlin.com>
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/ads8344.pdf
 */

#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define ADS8344_START BIT(7)
#define ADS8344_SINGLE_END BIT(2)
#define ADS8344_CHANNEL(channel) ((channel) << 4)
#define ADS8344_CLOCK_INTERNAL 0x2 /* PD1 = 1 and PD0 = 0 */

struct ads8344 {
	struct spi_device *spi;
	struct regulator *reg;
	/*
	 * Lock protecting access to adc->tx_buff and rx_buff,
	 * especially from concurrent read on sysfs file.
	 */
	struct mutex lock;

	u8 tx_buf ____cacheline_aligned;
	u8 rx_buf[3];
};

#define ADS8344_VOLTAGE_CHANNEL(chan, addr)				\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = chan,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.address = addr,					\
	}

#define ADS8344_VOLTAGE_CHANNEL_DIFF(chan1, chan2, addr)		\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = (chan1),					\
		.channel2 = (chan2),					\
		.differential = 1,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.address = addr,					\
	}

static const struct iio_chan_spec ads8344_channels[] = {
	ADS8344_VOLTAGE_CHANNEL(0, 0),
	ADS8344_VOLTAGE_CHANNEL(1, 4),
	ADS8344_VOLTAGE_CHANNEL(2, 1),
	ADS8344_VOLTAGE_CHANNEL(3, 5),
	ADS8344_VOLTAGE_CHANNEL(4, 2),
	ADS8344_VOLTAGE_CHANNEL(5, 6),
	ADS8344_VOLTAGE_CHANNEL(6, 3),
	ADS8344_VOLTAGE_CHANNEL(7, 7),
	ADS8344_VOLTAGE_CHANNEL_DIFF(0, 1, 8),
	ADS8344_VOLTAGE_CHANNEL_DIFF(2, 3, 9),
	ADS8344_VOLTAGE_CHANNEL_DIFF(4, 5, 10),
	ADS8344_VOLTAGE_CHANNEL_DIFF(6, 7, 11),
	ADS8344_VOLTAGE_CHANNEL_DIFF(1, 0, 12),
	ADS8344_VOLTAGE_CHANNEL_DIFF(3, 2, 13),
	ADS8344_VOLTAGE_CHANNEL_DIFF(5, 4, 14),
	ADS8344_VOLTAGE_CHANNEL_DIFF(7, 6, 15),
};

static int ads8344_adc_conversion(struct ads8344 *adc, int channel,
				  bool differential)
{
	struct spi_device *spi = adc->spi;
	int ret;

	adc->tx_buf = ADS8344_START;
	if (!differential)
		adc->tx_buf |= ADS8344_SINGLE_END;
	adc->tx_buf |= ADS8344_CHANNEL(channel);
	adc->tx_buf |= ADS8344_CLOCK_INTERNAL;

	ret = spi_write(spi, &adc->tx_buf, 1);
	if (ret)
		return ret;

	udelay(9);

	ret = spi_read(spi, adc->rx_buf, sizeof(adc->rx_buf));
	if (ret)
		return ret;

	return adc->rx_buf[0] << 9 | adc->rx_buf[1] << 1 | adc->rx_buf[2] >> 7;
}

static int ads8344_read_raw(struct iio_dev *iio,
			    struct iio_chan_spec const *channel, int *value,
			    int *shift, long mask)
{
	struct ads8344 *adc = iio_priv(iio);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		*value = ads8344_adc_conversion(adc, channel->address,
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
		*shift = 16;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ads8344_info = {
	.read_raw = ads8344_read_raw,
};

static void ads8344_reg_disable(void *data)
{
	regulator_disable(data);
}

static int ads8344_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ads8344 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	mutex_init(&adc->lock);

	indio_dev->name = dev_name(&spi->dev);
	indio_dev->info = &ads8344_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ads8344_channels;
	indio_dev->num_channels = ARRAY_SIZE(ads8344_channels);

	adc->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(adc->reg))
		return PTR_ERR(adc->reg);

	ret = regulator_enable(adc->reg);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ads8344_reg_disable, adc->reg);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ads8344_of_match[] = {
	{ .compatible = "ti,ads8344", },
	{}
};
MODULE_DEVICE_TABLE(of, ads8344_of_match);

static struct spi_driver ads8344_driver = {
	.driver = {
		.name = "ads8344",
		.of_match_table = ads8344_of_match,
	},
	.probe = ads8344_probe,
};
module_spi_driver(ads8344_driver);

MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@bootlin.com>");
MODULE_DESCRIPTION("ADS8344 driver");
MODULE_LICENSE("GPL");
