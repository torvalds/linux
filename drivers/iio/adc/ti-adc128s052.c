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

#include <linux/cleanup.h>
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
	const char			*refname;
	int				num_other_regulators;
	const char * const		(*other_regulators)[];
};

struct adc128 {
	struct spi_device *spi;

	/*
	 * Serialize the SPI 'write-channel + read data' accesses and protect
	 * the shared buffer.
	 */
	struct mutex lock;
	int vref_mv;
	union {
		__be16 buffer16;
		u8 buffer[2];
	} __aligned(IIO_DMA_MINALIGN);
};

static int adc128_adc_conversion(struct adc128 *adc, u8 channel)
{
	int ret;

	guard(mutex)(&adc->lock);

	adc->buffer[0] = channel << 3;
	adc->buffer[1] = 0;

	ret = spi_write(adc->spi, &adc->buffer, sizeof(adc->buffer));
	if (ret < 0)
		return ret;

	ret = spi_read(adc->spi, &adc->buffer16, sizeof(adc->buffer16));
	if (ret < 0)
		return ret;

	return be16_to_cpu(adc->buffer16) & 0xFFF;
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

		*val = adc->vref_mv;
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

static const struct iio_chan_spec simple_1chan_adc_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
};

static const struct iio_chan_spec simple_2chan_adc_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
};

static const struct iio_chan_spec simple_4chan_adc_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
	ADC128_VOLTAGE_CHANNEL(2),
	ADC128_VOLTAGE_CHANNEL(3),
};

static const struct iio_chan_spec simple_8chan_adc_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
	ADC128_VOLTAGE_CHANNEL(2),
	ADC128_VOLTAGE_CHANNEL(3),
	ADC128_VOLTAGE_CHANNEL(4),
	ADC128_VOLTAGE_CHANNEL(5),
	ADC128_VOLTAGE_CHANNEL(6),
	ADC128_VOLTAGE_CHANNEL(7),
};

static const char * const bd79104_regulators[] = { "iovdd" };

static const struct adc128_configuration adc122s_config = {
	.channels = simple_2chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_2chan_adc_channels),
	.refname = "vref",
};

static const struct adc128_configuration adc124s_config = {
	.channels = simple_4chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_4chan_adc_channels),
	.refname = "vref",
};

static const struct adc128_configuration adc128s_config = {
	.channels = simple_8chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_8chan_adc_channels),
	.refname = "vref",
};

static const struct adc128_configuration bd79100_config = {
	.channels = simple_1chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_1chan_adc_channels),
	.refname = "vdd",
	.other_regulators = &bd79104_regulators,
	.num_other_regulators = 1,
};

static const struct adc128_configuration bd79101_config = {
	.channels = simple_2chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_2chan_adc_channels),
	.refname = "vdd",
	.other_regulators = &bd79104_regulators,
	.num_other_regulators = 1,
};

static const struct adc128_configuration bd79102_config = {
	.channels = simple_4chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_4chan_adc_channels),
	.refname = "vdd",
	.other_regulators = &bd79104_regulators,
	.num_other_regulators = 1,
};

static const struct adc128_configuration bd79104_config = {
	.channels = simple_8chan_adc_channels,
	.num_channels = ARRAY_SIZE(simple_8chan_adc_channels),
	.refname = "vdd",
	.other_regulators = &bd79104_regulators,
	.num_other_regulators = 1,
};

static const struct iio_info adc128_info = {
	.read_raw = adc128_read_raw,
};

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

	ret = devm_regulator_get_enable_read_voltage(&spi->dev,
						     config->refname);
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret,
				     "failed to read '%s' voltage",
				     config->refname);

	adc->vref_mv = ret / 1000;

	if (config->num_other_regulators) {
		ret = devm_regulator_bulk_get_enable(&spi->dev,
						config->num_other_regulators,
						*config->other_regulators);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
					     "Failed to enable regulators\n");
	}

	ret = devm_mutex_init(&spi->dev, &adc->lock);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id adc128_of_match[] = {
	{ .compatible = "ti,adc128s052", .data = &adc128s_config },
	{ .compatible = "ti,adc122s021", .data = &adc122s_config },
	{ .compatible = "ti,adc122s051", .data = &adc122s_config },
	{ .compatible = "ti,adc122s101", .data = &adc122s_config },
	{ .compatible = "ti,adc124s021", .data = &adc124s_config },
	{ .compatible = "ti,adc124s051", .data = &adc124s_config },
	{ .compatible = "ti,adc124s101", .data = &adc124s_config },
	{ .compatible = "rohm,bd79100", .data = &bd79100_config },
	{ .compatible = "rohm,bd79101", .data = &bd79101_config },
	{ .compatible = "rohm,bd79102", .data = &bd79102_config },
	{ .compatible = "rohm,bd79103", .data = &bd79104_config },
	{ .compatible = "rohm,bd79104", .data = &bd79104_config },
	{ }
};
MODULE_DEVICE_TABLE(of, adc128_of_match);

static const struct spi_device_id adc128_id[] = {
	{ "adc128s052", (kernel_ulong_t)&adc128s_config },
	{ "adc122s021",	(kernel_ulong_t)&adc122s_config },
	{ "adc122s051",	(kernel_ulong_t)&adc122s_config },
	{ "adc122s101",	(kernel_ulong_t)&adc122s_config },
	{ "adc124s021", (kernel_ulong_t)&adc124s_config },
	{ "adc124s051", (kernel_ulong_t)&adc124s_config },
	{ "adc124s101", (kernel_ulong_t)&adc124s_config },
	{ "bd79100", (kernel_ulong_t)&bd79100_config },
	{ "bd79101", (kernel_ulong_t)&bd79101_config },
	{ "bd79102", (kernel_ulong_t)&bd79102_config },
	{ "bd79103", (kernel_ulong_t)&bd79104_config },
	{ "bd79104", (kernel_ulong_t)&bd79104_config },
	{ }
};
MODULE_DEVICE_TABLE(spi, adc128_id);

static const struct acpi_device_id adc128_acpi_match[] = {
	{ "AANT1280", (kernel_ulong_t)&adc124s_config },
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
