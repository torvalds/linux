// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Angelo Compagnucci <angelo.compagnucci@gmail.com>
 *
 * Driver for Texas Instruments' ADC128S052, ADC122S021 and ADC124S021 ADC chip.
 * Datasheets can be found here:
 * https://www.ti.com/lit/ds/symlink/adc128s052.pdf
 * https://www.ti.com/lit/ds/symlink/adc122s021.pdf
 * https://www.ti.com/lit/ds/symlink/adc124s021.pdf
 */

#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

struct adc128_configuration {
	const struct iio_chan_spec	*channels;
	u8				num_channels;
};

struct adc128 {
	struct spi_device *spi;

	struct regulator *reg;
	struct mutex lock;

	u8 buffer[2] __aligned(IIO_DMA_MINALIGN);
};

static int adc128_adc_conversion(struct adc128 *adc, u8 channel)
{
	int ret;

	mutex_lock(&adc->lock);

	adc->buffer[0] = channel << 3;
	adc->buffer[1] = 0;

	ret = spi_write(adc->spi, &adc->buffer, 2);
	if (ret < 0) {
		mutex_unlock(&adc->lock);
		return ret;
	}

	ret = spi_read(adc->spi, &adc->buffer, 2);

	mutex_unlock(&adc->lock);

	if (ret < 0)
		return ret;

	return ((adc->buffer[0] << 8 | adc->buffer[1]) & 0xFFF);
}

static int adc128_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *channel, int *val,
			   int *val2, long mask)
{
	struct adc128 *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:

		ret = adc128_adc_conversion(adc, channel->channel);
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:

		ret = regulator_get_voltage(adc->reg);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}

}

#define ADC128_VOLTAGE_CHANNEL(num)	\
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = (num), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
	}

static const struct iio_chan_spec adc128s052_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
	ADC128_VOLTAGE_CHANNEL(2),
	ADC128_VOLTAGE_CHANNEL(3),
	ADC128_VOLTAGE_CHANNEL(4),
	ADC128_VOLTAGE_CHANNEL(5),
	ADC128_VOLTAGE_CHANNEL(6),
	ADC128_VOLTAGE_CHANNEL(7),
};

static const struct iio_chan_spec adc122s021_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
};

static const struct iio_chan_spec adc124s021_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
	ADC128_VOLTAGE_CHANNEL(2),
	ADC128_VOLTAGE_CHANNEL(3),
};

static const struct adc128_configuration adc128_config[] = {
	{ adc128s052_channels, ARRAY_SIZE(adc128s052_channels) },
	{ adc122s021_channels, ARRAY_SIZE(adc122s021_channels) },
	{ adc124s021_channels, ARRAY_SIZE(adc124s021_channels) },
};

static const struct iio_info adc128_info = {
	.read_raw = adc128_read_raw,
};

static void adc128_disable_regulator(void *reg)
{
	regulator_disable(reg);
}

static int adc128_probe(struct spi_device *spi)
{
	const struct adc128_configuration *config;
	struct iio_dev *indio_dev;
	struct adc128 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &adc128_info;

	config = spi_get_device_match_data(spi);

	indio_dev->channels = config->channels;
	indio_dev->num_channels = config->num_channels;

	adc->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(adc->reg))
		return PTR_ERR(adc->reg);

	ret = regulator_enable(adc->reg);
	if (ret < 0)
		return ret;
	ret = devm_add_action_or_reset(&spi->dev, adc128_disable_regulator,
				       adc->reg);
	if (ret)
		return ret;

	mutex_init(&adc->lock);

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id adc128_of_match[] = {
	{ .compatible = "ti,adc128s052", .data = &adc128_config[0] },
	{ .compatible = "ti,adc122s021", .data = &adc128_config[1] },
	{ .compatible = "ti,adc122s051", .data = &adc128_config[1] },
	{ .compatible = "ti,adc122s101", .data = &adc128_config[1] },
	{ .compatible = "ti,adc124s021", .data = &adc128_config[2] },
	{ .compatible = "ti,adc124s051", .data = &adc128_config[2] },
	{ .compatible = "ti,adc124s101", .data = &adc128_config[2] },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, adc128_of_match);

static const struct spi_device_id adc128_id[] = {
	{ "adc128s052", (kernel_ulong_t)&adc128_config[0] },
	{ "adc122s021",	(kernel_ulong_t)&adc128_config[1] },
	{ "adc122s051",	(kernel_ulong_t)&adc128_config[1] },
	{ "adc122s101",	(kernel_ulong_t)&adc128_config[1] },
	{ "adc124s021", (kernel_ulong_t)&adc128_config[2] },
	{ "adc124s051", (kernel_ulong_t)&adc128_config[2] },
	{ "adc124s101", (kernel_ulong_t)&adc128_config[2] },
	{ }
};
MODULE_DEVICE_TABLE(spi, adc128_id);

static const struct acpi_device_id adc128_acpi_match[] = {
	{ "AANT1280", (kernel_ulong_t)&adc128_config[2] },
	{ }
};
MODULE_DEVICE_TABLE(acpi, adc128_acpi_match);

static struct spi_driver adc128_driver = {
	.driver = {
		.name = "adc128s052",
		.of_match_table = adc128_of_match,
		.acpi_match_table = adc128_acpi_match,
	},
	.probe = adc128_probe,
	.id_table = adc128_id,
};
module_spi_driver(adc128_driver);

MODULE_AUTHOR("Angelo Compagnucci <angelo.compagnucci@gmail.com>");
MODULE_DESCRIPTION("Texas Instruments ADC128S052");
MODULE_LICENSE("GPL v2");
