// SPDX-License-Identifier: GPL-2.0
/* ti-dac7311.c - Texas Instruments 8/10/12-bit 1-channel DAC driver
 *
 * Copyright (C) 2018 CMC NV
 *
 * https://www.ti.com/lit/ds/symlink/dac7311.pdf
 */

#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

enum {
	ID_DAC5311 = 0,
	ID_DAC6311,
	ID_DAC7311,
};

enum {
	POWER_1KOHM_TO_GND = 0,
	POWER_100KOHM_TO_GND,
	POWER_TRI_STATE,
};

struct ti_dac_spec {
	u8 resolution;
};

static const struct ti_dac_spec ti_dac_spec[] = {
	[ID_DAC5311] = { .resolution = 8 },
	[ID_DAC6311] = { .resolution = 10 },
	[ID_DAC7311] = { .resolution = 12 },
};

/**
 * struct ti_dac_chip - TI DAC chip
 * @lock: protects write sequences
 * @vref: regulator generating Vref
 * @spi: SPI device to send data to the device
 * @val: cached value
 * @powerdown: whether the chip is powered down
 * @powerdown_mode: selected by the user
 * @resolution: resolution of the chip
 * @buf: buffer for transfer data
 */
struct ti_dac_chip {
	struct mutex lock;
	struct regulator *vref;
	struct spi_device *spi;
	u16 val;
	bool powerdown;
	u8 powerdown_mode;
	u8 resolution;
	u8 buf[2] ____cacheline_aligned;
};

static u8 ti_dac_get_power(struct ti_dac_chip *ti_dac, bool powerdown)
{
	if (powerdown)
		return ti_dac->powerdown_mode + 1;

	return 0;
}

static int ti_dac_cmd(struct ti_dac_chip *ti_dac, u8 power, u16 val)
{
	u8 shift = 14 - ti_dac->resolution;

	ti_dac->buf[0] = (val << shift) & 0xFF;
	ti_dac->buf[1] = (power << 6) | (val >> (8 - shift));
	return spi_write(ti_dac->spi, ti_dac->buf, 2);
}

static const char * const ti_dac_powerdown_modes[] = {
	"1kohm_to_gnd",
	"100kohm_to_gnd",
	"three_state",
};

static int ti_dac_get_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);

	return ti_dac->powerdown_mode;
}

static int ti_dac_set_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     unsigned int mode)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);

	ti_dac->powerdown_mode = mode;
	return 0;
}

static const struct iio_enum ti_dac_powerdown_mode = {
	.items = ti_dac_powerdown_modes,
	.num_items = ARRAY_SIZE(ti_dac_powerdown_modes),
	.get = ti_dac_get_powerdown_mode,
	.set = ti_dac_set_powerdown_mode,
};

static ssize_t ti_dac_read_powerdown(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     char *buf)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", ti_dac->powerdown);
}

static ssize_t ti_dac_write_powerdown(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);
	bool powerdown;
	u8 power;
	int ret;

	ret = strtobool(buf, &powerdown);
	if (ret)
		return ret;

	power = ti_dac_get_power(ti_dac, powerdown);

	mutex_lock(&ti_dac->lock);
	ret = ti_dac_cmd(ti_dac, power, 0);
	if (!ret)
		ti_dac->powerdown = powerdown;
	mutex_unlock(&ti_dac->lock);

	return ret ? ret : len;
}

static const struct iio_chan_spec_ext_info ti_dac_ext_info[] = {
	{
		.name	   = "powerdown",
		.read	   = ti_dac_read_powerdown,
		.write	   = ti_dac_write_powerdown,
		.shared	   = IIO_SHARED_BY_TYPE,
	},
	IIO_ENUM("powerdown_mode", IIO_SHARED_BY_TYPE, &ti_dac_powerdown_mode),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ti_dac_powerdown_mode),
	{ },
};

#define TI_DAC_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.channel = (chan),					\
	.output = true,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = ti_dac_ext_info,				\
}

static const struct iio_chan_spec ti_dac_channels[] = {
	TI_DAC_CHANNEL(0),
};

static int ti_dac_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = ti_dac->val;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(ti_dac->vref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = ti_dac->resolution;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int ti_dac_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);
	u8 power = ti_dac_get_power(ti_dac, ti_dac->powerdown);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (ti_dac->val == val)
			return 0;

		if (val >= (1 << ti_dac->resolution) || val < 0)
			return -EINVAL;

		if (ti_dac->powerdown)
			return -EBUSY;

		mutex_lock(&ti_dac->lock);
		ret = ti_dac_cmd(ti_dac, power, val);
		if (!ret)
			ti_dac->val = val;
		mutex_unlock(&ti_dac->lock);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ti_dac_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long mask)
{
	return IIO_VAL_INT;
}

static const struct iio_info ti_dac_info = {
	.read_raw	   = ti_dac_read_raw,
	.write_raw	   = ti_dac_write_raw,
	.write_raw_get_fmt = ti_dac_write_raw_get_fmt,
};

static int ti_dac_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct ti_dac_spec *spec;
	struct ti_dac_chip *ti_dac;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ti_dac));
	if (!indio_dev) {
		dev_err(dev, "can not allocate iio device\n");
		return -ENOMEM;
	}

	spi->mode = SPI_MODE_1;
	spi->bits_per_word = 16;
	spi_setup(spi);

	indio_dev->info = &ti_dac_info;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ti_dac_channels;
	spi_set_drvdata(spi, indio_dev);

	ti_dac = iio_priv(indio_dev);
	ti_dac->powerdown = false;
	ti_dac->spi = spi;

	spec = &ti_dac_spec[spi_get_device_id(spi)->driver_data];
	indio_dev->num_channels = 1;
	ti_dac->resolution = spec->resolution;

	ti_dac->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(ti_dac->vref))
		return dev_err_probe(dev, PTR_ERR(ti_dac->vref),
				     "error to get regulator\n");

	ret = regulator_enable(ti_dac->vref);
	if (ret < 0) {
		dev_err(dev, "can not enable regulator\n");
		return ret;
	}

	mutex_init(&ti_dac->lock);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "fail to register iio device: %d\n", ret);
		goto err;
	}

	return 0;

err:
	mutex_destroy(&ti_dac->lock);
	regulator_disable(ti_dac->vref);
	return ret;
}

static int ti_dac_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	mutex_destroy(&ti_dac->lock);
	regulator_disable(ti_dac->vref);
	return 0;
}

static const struct of_device_id ti_dac_of_id[] = {
	{ .compatible = "ti,dac5311" },
	{ .compatible = "ti,dac6311" },
	{ .compatible = "ti,dac7311" },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_dac_of_id);

static const struct spi_device_id ti_dac_spi_id[] = {
	{ "dac5311", ID_DAC5311  },
	{ "dac6311", ID_DAC6311 },
	{ "dac7311", ID_DAC7311 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ti_dac_spi_id);

static struct spi_driver ti_dac_driver = {
	.driver = {
		.name		= "ti-dac7311",
		.of_match_table	= ti_dac_of_id,
	},
	.probe	  = ti_dac_probe,
	.remove   = ti_dac_remove,
	.id_table = ti_dac_spi_id,
};
module_spi_driver(ti_dac_driver);

MODULE_AUTHOR("Charles-Antoine Couret <charles-antoine.couret@essensium.com>");
MODULE_DESCRIPTION("Texas Instruments 8/10/12-bit 1-channel DAC driver");
MODULE_LICENSE("GPL v2");
