// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim Integrated MAX5481-MAX5484 digital potentiometer driver
 * Copyright 2016 Rockwell Collins
 *
 * Datasheet:
 * http://datasheets.maximintegrated.com/en/ds/MAX5481-MAX5484.pdf
 */

#include <linux/acpi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>

/* write wiper reg */
#define MAX5481_WRITE_WIPER (0 << 4)
/* copy wiper reg to NV reg */
#define MAX5481_COPY_AB_TO_NV (2 << 4)
/* copy NV reg to wiper reg */
#define MAX5481_COPY_NV_TO_AB (3 << 4)

#define MAX5481_MAX_POS    1023

enum max5481_variant {
	max5481,
	max5482,
	max5483,
	max5484,
};

struct max5481_cfg {
	int kohms;
};

static const struct max5481_cfg max5481_cfg[] = {
	[max5481] = { .kohms =  10, },
	[max5482] = { .kohms =  50, },
	[max5483] = { .kohms =  10, },
	[max5484] = { .kohms =  50, },
};

struct max5481_data {
	struct spi_device *spi;
	const struct max5481_cfg *cfg;
	u8 msg[3] ____cacheline_aligned;
};

#define MAX5481_CHANNEL {					\
	.type = IIO_RESISTANCE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = 0,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec max5481_channels[] = {
	MAX5481_CHANNEL,
};

static int max5481_write_cmd(struct max5481_data *data, u8 cmd, u16 val)
{
	struct spi_device *spi = data->spi;

	data->msg[0] = cmd;

	switch (cmd) {
	case MAX5481_WRITE_WIPER:
		data->msg[1] = val >> 2;
		data->msg[2] = (val & 0x3) << 6;
		return spi_write(spi, data->msg, 3);

	case MAX5481_COPY_AB_TO_NV:
	case MAX5481_COPY_NV_TO_AB:
		return spi_write(spi, data->msg, 1);

	default:
		return -EIO;
	}
}

static int max5481_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int *val, int *val2, long mask)
{
	struct max5481_data *data = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_SCALE)
		return -EINVAL;

	*val = 1000 * data->cfg->kohms;
	*val2 = MAX5481_MAX_POS;

	return IIO_VAL_FRACTIONAL;
}

static int max5481_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int val, int val2, long mask)
{
	struct max5481_data *data = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if (val < 0 || val > MAX5481_MAX_POS)
		return -EINVAL;

	return max5481_write_cmd(data, MAX5481_WRITE_WIPER, val);
}

static const struct iio_info max5481_info = {
	.read_raw = max5481_read_raw,
	.write_raw = max5481_write_raw,
};

#if defined(CONFIG_OF)
static const struct of_device_id max5481_match[] = {
	{ .compatible = "maxim,max5481", .data = &max5481_cfg[max5481] },
	{ .compatible = "maxim,max5482", .data = &max5481_cfg[max5482] },
	{ .compatible = "maxim,max5483", .data = &max5481_cfg[max5483] },
	{ .compatible = "maxim,max5484", .data = &max5481_cfg[max5484] },
	{ }
};
MODULE_DEVICE_TABLE(of, max5481_match);
#endif

static int max5481_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct max5481_data *data;
	const struct spi_device_id *id = spi_get_device_id(spi);
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, indio_dev);
	data = iio_priv(indio_dev);

	data->spi = spi;

	data->cfg = of_device_get_match_data(&spi->dev);
	if (!data->cfg)
		data->cfg = &max5481_cfg[id->driver_data];

	indio_dev->name = id->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* variant specific configuration */
	indio_dev->info = &max5481_info;
	indio_dev->channels = max5481_channels;
	indio_dev->num_channels = ARRAY_SIZE(max5481_channels);

	/* restore wiper from NV */
	ret = max5481_write_cmd(data, MAX5481_COPY_NV_TO_AB, 0);
	if (ret < 0)
		return ret;

	return iio_device_register(indio_dev);
}

static int max5481_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = dev_get_drvdata(&spi->dev);
	struct max5481_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	/* save wiper reg to NV reg */
	return max5481_write_cmd(data, MAX5481_COPY_AB_TO_NV, 0);
}

static const struct spi_device_id max5481_id_table[] = {
	{ "max5481", max5481 },
	{ "max5482", max5482 },
	{ "max5483", max5483 },
	{ "max5484", max5484 },
	{ }
};
MODULE_DEVICE_TABLE(spi, max5481_id_table);

#if defined(CONFIG_ACPI)
static const struct acpi_device_id max5481_acpi_match[] = {
	{ "max5481", max5481 },
	{ "max5482", max5482 },
	{ "max5483", max5483 },
	{ "max5484", max5484 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, max5481_acpi_match);
#endif

static struct spi_driver max5481_driver = {
	.driver = {
		.name  = "max5481",
		.of_match_table = of_match_ptr(max5481_match),
		.acpi_match_table = ACPI_PTR(max5481_acpi_match),
	},
	.probe = max5481_probe,
	.remove = max5481_remove,
	.id_table = max5481_id_table,
};

module_spi_driver(max5481_driver);

MODULE_AUTHOR("Maury Anderson <maury.anderson@rockwellcollins.com>");
MODULE_DESCRIPTION("max5481 SPI driver");
MODULE_LICENSE("GPL v2");
