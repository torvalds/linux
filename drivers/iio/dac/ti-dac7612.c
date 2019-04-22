// SPDX-License-Identifier: GPL-2.0
/*
 * DAC7612 Dual, 12-Bit Serial input Digital-to-Analog Converter
 *
 * Copyright 2019 Qtechnology A/S
 * 2019 Ricardo Ribalda <ricardo@ribalda.com>
 *
 * Licensed under the GPL-2.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>

#define DAC7612_RESOLUTION 12
#define DAC7612_ADDRESS 4
#define DAC7612_START 5

struct dac7612 {
	struct spi_device *spi;
	struct gpio_desc *loaddacs;
	uint16_t cache[2];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	uint8_t data[2] ____cacheline_aligned;
};

static int dac7612_cmd_single(struct dac7612 *priv, int channel, u16 val)
{
	int ret;

	priv->data[0] = BIT(DAC7612_START) | (channel << DAC7612_ADDRESS);
	priv->data[0] |= val >> 8;
	priv->data[1] = val & 0xff;

	priv->cache[channel] = val;

	ret = spi_write(priv->spi, priv->data, sizeof(priv->data));
	if (ret)
		return ret;

	gpiod_set_value(priv->loaddacs, 1);
	gpiod_set_value(priv->loaddacs, 0);

	return 0;
}

#define dac7612_CHANNEL(chan, name) {				\
	.type = IIO_VOLTAGE,					\
	.channel = (chan),					\
	.indexed = 1,						\
	.output = 1,						\
	.datasheet_name = name,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec dac7612_channels[] = {
	dac7612_CHANNEL(0, "OUTA"),
	dac7612_CHANNEL(1, "OUTB"),
};

static int dac7612_read_raw(struct iio_dev *iio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct dac7612 *priv;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		priv = iio_priv(iio_dev);
		*val = priv->cache[chan->channel];
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int dac7612_write_raw(struct iio_dev *iio_dev,
			     const struct iio_chan_spec *chan,
			     int val, int val2, long mask)
{
	struct dac7612 *priv = iio_priv(iio_dev);
	int ret;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if ((val >= BIT(DAC7612_RESOLUTION)) || val < 0 || val2)
		return -EINVAL;

	if (val == priv->cache[chan->channel])
		return 0;

	mutex_lock(&iio_dev->mlock);
	ret = dac7612_cmd_single(priv, chan->channel, val);
	mutex_unlock(&iio_dev->mlock);

	return ret;
}

static const struct iio_info dac7612_info = {
	.read_raw = dac7612_read_raw,
	.write_raw = dac7612_write_raw,
};

static int dac7612_probe(struct spi_device *spi)
{
	struct iio_dev *iio_dev;
	struct dac7612 *priv;
	int i;
	int ret;

	iio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*priv));
	if (!iio_dev)
		return -ENOMEM;

	priv = iio_priv(iio_dev);
	/*
	 * LOADDACS pin can be controlled by the driver or externally.
	 * When controlled by the driver, the DAC value is updated after
	 * every write.
	 * When the driver does not control the PIN, the user or an external
	 * event can change the value of all DACs by pulsing down the LOADDACs
	 * pin.
	 */
	priv->loaddacs = devm_gpiod_get_optional(&spi->dev, "ti,loaddacs",
						 GPIOD_OUT_LOW);
	if (IS_ERR(priv->loaddacs))
		return PTR_ERR(priv->loaddacs);
	priv->spi = spi;
	spi_set_drvdata(spi, iio_dev);
	iio_dev->dev.parent = &spi->dev;
	iio_dev->info = &dac7612_info;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->channels = dac7612_channels;
	iio_dev->num_channels = ARRAY_SIZE(priv->cache);
	iio_dev->name = spi_get_device_id(spi)->name;

	for (i = 0; i < ARRAY_SIZE(priv->cache); i++) {
		ret = dac7612_cmd_single(priv, i, 0);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(&spi->dev, iio_dev);
}

static const struct spi_device_id dac7612_id[] = {
	{"ti-dac7612"},
	{}
};
MODULE_DEVICE_TABLE(spi, dac7612_id);

static const struct of_device_id dac7612_of_match[] = {
	{ .compatible = "ti,dac7612" },
	{ .compatible = "ti,dac7612u" },
	{ .compatible = "ti,dac7612ub" },
	{ },
};
MODULE_DEVICE_TABLE(of, dac7612_of_match);

static struct spi_driver dac7612_driver = {
	.driver = {
		   .name = "ti-dac7612",
		   .of_match_table = dac7612_of_match,
		   },
	.probe = dac7612_probe,
	.id_table = dac7612_id,
};
module_spi_driver(dac7612_driver);

MODULE_AUTHOR("Ricardo Ribalda <ricardo@ribalda.com>");
MODULE_DESCRIPTION("Texas Instruments DAC7612 DAC driver");
MODULE_LICENSE("GPL v2");
