// SPDX-License-Identifier: GPL-2.0-only
/*
 * BD79703 ROHM Digital to Analog converter
 *
 * Copyright (c) 2024, ROHM Semiconductor.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

#define BD79703_MAX_REGISTER 0xf
#define BD79703_DAC_BITS 8
#define BD79703_REG_OUT_ALL GENMASK(2, 0)

/*
 * The BD79703 uses 12-bit SPI commands. First four bits (high bits) define
 * channel(s) which are operated on, and also the mode. The mode can be to set
 * a DAC word only, or set DAC word and output. The data-sheet is not very
 * specific on how a previously set DAC word can be 'taken in to use'. Thus
 * this driver only uses the 'set DAC and output it' -mode.
 *
 * The BD79703 latches last 12-bits when the chip-select is toggled. Thus we
 * can use 16-bit transfers which should be widely supported. To simplify this
 * further, we treat the last 8 bits as a value, and first 8 bits as an
 * address. This allows us to separate channels/mode by address and treat the
 * 8-bit register value as DAC word. The highest 4 bits of address will be
 * discarded when the transfer is latched.
 */
static const struct regmap_config bd79703_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BD79703_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

struct bd79703_data {
	struct regmap *regmap;
	int vfs;
};

static int bd79703_read_raw(struct iio_dev *idev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct bd79703_data *data = iio_priv(idev);

	if (mask != IIO_CHAN_INFO_SCALE)
		return -EINVAL;

	*val = data->vfs / 1000;
	*val2 = BD79703_DAC_BITS;

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int bd79703_write_raw(struct iio_dev *idev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct bd79703_data *data = iio_priv(idev);

	if (val < 0 || val >= 1 << BD79703_DAC_BITS)
		return -EINVAL;

	return regmap_write(data->regmap, chan->channel + 1, val);
};

static const struct iio_info bd79703_info = {
	.read_raw = bd79703_read_raw,
	.write_raw = bd79703_write_raw,
};

#define BD79703_CHAN(_chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (_chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.address = (_chan),					\
}

static const struct iio_chan_spec bd79703_channels[] = {
	BD79703_CHAN(0),
	BD79703_CHAN(1),
	BD79703_CHAN(2),
	BD79703_CHAN(3),
	BD79703_CHAN(4),
	BD79703_CHAN(5),
};

static int bd79703_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct bd79703_data *data;
	struct iio_dev *idev;
	int ret;

	idev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!idev)
		return -ENOMEM;

	data = iio_priv(idev);

	data->regmap = devm_regmap_init_spi(spi, &bd79703_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Failed to initialize Regmap\n");

	ret = devm_regulator_get_enable(dev, "vcc");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable VCC\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "vfs");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get Vfs\n");

	data->vfs = ret;
	idev->channels = bd79703_channels;
	idev->num_channels = ARRAY_SIZE(bd79703_channels);
	idev->modes = INDIO_DIRECT_MODE;
	idev->info = &bd79703_info;
	idev->name = "bd79703";

	/* Initialize all to output zero */
	ret = regmap_write(data->regmap, BD79703_REG_OUT_ALL, 0);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, idev);
}

static const struct spi_device_id bd79703_id[] = {
	{ "bd79703", },
	{ }
};
MODULE_DEVICE_TABLE(spi, bd79703_id);

static const struct of_device_id bd79703_of_match[] = {
	{ .compatible = "rohm,bd79703", },
	{ }
};
MODULE_DEVICE_TABLE(of, bd79703_of_match);

static struct spi_driver bd79703_driver = {
	.driver = {
		.name = "bd79703",
		.of_match_table = bd79703_of_match,
	},
	.probe = bd79703_probe,
	.id_table = bd79703_id,
};
module_spi_driver(bd79703_driver);

MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("ROHM BD79703 DAC driver");
MODULE_LICENSE("GPL");
