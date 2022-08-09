// SPDX-License-Identifier: GPL-2.0-only
/*
 * ti-dac082s085.c - Texas Instruments 8/10/12-bit 2/4-channel DAC driver
 *
 * Copyright (C) 2017 KUNBUS GmbH
 *
 * https://www.ti.com/lit/ds/symlink/dac082s085.pdf
 * https://www.ti.com/lit/ds/symlink/dac102s085.pdf
 * https://www.ti.com/lit/ds/symlink/dac122s085.pdf
 * https://www.ti.com/lit/ds/symlink/dac084s085.pdf
 * https://www.ti.com/lit/ds/symlink/dac104s085.pdf
 * https://www.ti.com/lit/ds/symlink/dac124s085.pdf
 */

#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

enum { dual_8bit, dual_10bit, dual_12bit, quad_8bit, quad_10bit, quad_12bit };

struct ti_dac_spec {
	u8 num_channels;
	u8 resolution;
};

static const struct ti_dac_spec ti_dac_spec[] = {
	[dual_8bit]  = { .num_channels = 2, .resolution = 8  },
	[dual_10bit] = { .num_channels = 2, .resolution = 10 },
	[dual_12bit] = { .num_channels = 2, .resolution = 12 },
	[quad_8bit]  = { .num_channels = 4, .resolution = 8  },
	[quad_10bit] = { .num_channels = 4, .resolution = 10 },
	[quad_12bit] = { .num_channels = 4, .resolution = 12 },
};

/**
 * struct ti_dac_chip - TI DAC chip
 * @lock: protects write sequences
 * @vref: regulator generating Vref
 * @mesg: SPI message to perform a write
 * @xfer: SPI transfer used by @mesg
 * @val: cached value of each output
 * @powerdown: whether the chip is powered down
 * @powerdown_mode: selected by the user
 * @resolution: resolution of the chip
 * @buf: buffer for @xfer
 */
struct ti_dac_chip {
	struct mutex lock;
	struct regulator *vref;
	struct spi_message mesg;
	struct spi_transfer xfer;
	u16 val[4];
	bool powerdown;
	u8 powerdown_mode;
	u8 resolution;
	u8 buf[2] ____cacheline_aligned;
};

#define WRITE_NOT_UPDATE(chan)	(0x00 | (chan) << 6)
#define WRITE_AND_UPDATE(chan)	(0x10 | (chan) << 6)
#define WRITE_ALL_UPDATE	 0x20
#define POWERDOWN(mode) 	(0x30 | ((mode) + 1) << 6)

static int ti_dac_cmd(struct ti_dac_chip *ti_dac, u8 cmd, u16 val)
{
	u8 shift = 12 - ti_dac->resolution;

	ti_dac->buf[0] = cmd | (val >> (8 - shift));
	ti_dac->buf[1] = (val << shift) & 0xff;
	return spi_sync(ti_dac->mesg.spi, &ti_dac->mesg);
}

static const char * const ti_dac_powerdown_modes[] = {
	"2.5kohm_to_gnd", "100kohm_to_gnd", "three_state",
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
	int ret = 0;

	if (ti_dac->powerdown_mode == mode)
		return 0;

	mutex_lock(&ti_dac->lock);
	if (ti_dac->powerdown) {
		ret = ti_dac_cmd(ti_dac, POWERDOWN(mode), 0);
		if (ret)
			goto out;
	}
	ti_dac->powerdown_mode = mode;

out:
	mutex_unlock(&ti_dac->lock);
	return ret;
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
	int ret;

	ret = strtobool(buf, &powerdown);
	if (ret)
		return ret;

	if (ti_dac->powerdown == powerdown)
		return len;

	mutex_lock(&ti_dac->lock);
	if (powerdown)
		ret = ti_dac_cmd(ti_dac, POWERDOWN(ti_dac->powerdown_mode), 0);
	else
		ret = ti_dac_cmd(ti_dac, WRITE_AND_UPDATE(0), ti_dac->val[0]);
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
	.address = (chan),					\
	.indexed = true,					\
	.output = true,						\
	.datasheet_name = (const char[]){ 'A' + (chan), 0 },	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = ti_dac_ext_info,				\
}

static const struct iio_chan_spec ti_dac_channels[] = {
	TI_DAC_CHANNEL(0),
	TI_DAC_CHANNEL(1),
	TI_DAC_CHANNEL(2),
	TI_DAC_CHANNEL(3),
};

static int ti_dac_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = ti_dac->val[chan->channel];
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(ti_dac->vref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = ti_dac->resolution;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ti_dac_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ti_dac_chip *ti_dac = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (ti_dac->val[chan->channel] == val)
			return 0;

		if (val >= (1 << ti_dac->resolution) || val < 0)
			return -EINVAL;

		if (ti_dac->powerdown)
			return -EBUSY;

		mutex_lock(&ti_dac->lock);
		ret = ti_dac_cmd(ti_dac, WRITE_AND_UPDATE(chan->channel), val);
		if (!ret)
			ti_dac->val[chan->channel] = val;
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
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &ti_dac_info;
	indio_dev->name = spi->modalias;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ti_dac_channels;
	spi_set_drvdata(spi, indio_dev);

	ti_dac = iio_priv(indio_dev);
	ti_dac->xfer.tx_buf = &ti_dac->buf;
	ti_dac->xfer.len = sizeof(ti_dac->buf);
	spi_message_init_with_transfers(&ti_dac->mesg, &ti_dac->xfer, 1);
	ti_dac->mesg.spi = spi;

	spec = &ti_dac_spec[spi_get_device_id(spi)->driver_data];
	indio_dev->num_channels = spec->num_channels;
	ti_dac->resolution = spec->resolution;

	ti_dac->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(ti_dac->vref))
		return PTR_ERR(ti_dac->vref);

	ret = regulator_enable(ti_dac->vref);
	if (ret < 0)
		return ret;

	mutex_init(&ti_dac->lock);

	ret = ti_dac_cmd(ti_dac, WRITE_ALL_UPDATE, 0);
	if (ret) {
		dev_err(dev, "failed to initialize outputs to 0\n");
		goto err;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err;

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
	{ .compatible = "ti,dac082s085" },
	{ .compatible = "ti,dac102s085" },
	{ .compatible = "ti,dac122s085" },
	{ .compatible = "ti,dac084s085" },
	{ .compatible = "ti,dac104s085" },
	{ .compatible = "ti,dac124s085" },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_dac_of_id);

static const struct spi_device_id ti_dac_spi_id[] = {
	{ "dac082s085", dual_8bit  },
	{ "dac102s085", dual_10bit },
	{ "dac122s085", dual_12bit },
	{ "dac084s085", quad_8bit  },
	{ "dac104s085", quad_10bit },
	{ "dac124s085", quad_12bit },
	{ }
};
MODULE_DEVICE_TABLE(spi, ti_dac_spi_id);

static struct spi_driver ti_dac_driver = {
	.driver = {
		.name		= "ti-dac082s085",
		.of_match_table	= ti_dac_of_id,
	},
	.probe	  = ti_dac_probe,
	.remove   = ti_dac_remove,
	.id_table = ti_dac_spi_id,
};
module_spi_driver(ti_dac_driver);

MODULE_AUTHOR("Lukas Wunner <lukas@wunner.de>");
MODULE_DESCRIPTION("Texas Instruments 8/10/12-bit 2/4-channel DAC driver");
MODULE_LICENSE("GPL v2");
