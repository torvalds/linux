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
	.cache_type = REGCACHE_MAPLE,
};

/* Dynamic driver private data */
struct bd79703_data {
	struct regmap *regmap;
	int vfs;
};

/* Static, IC type specific data for different variants */
struct bd7970x_chip_data {
	const char *name;
	const struct iio_chan_spec *channels;
	int num_channels;
	bool has_vfs;
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

	return regmap_write(data->regmap, chan->address, val);
};

static const struct iio_info bd79703_info = {
	.read_raw = bd79703_read_raw,
	.write_raw = bd79703_write_raw,
};

#define BD79703_CHAN_ADDR(_chan, _addr) {			\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (_chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.address = (_addr),					\
}

#define BD79703_CHAN(_chan) BD79703_CHAN_ADDR((_chan), (_chan) + 1)

static const struct iio_chan_spec bd79700_channels[] = {
	BD79703_CHAN(0),
	BD79703_CHAN(1),
};

static const struct iio_chan_spec bd79701_channels[] = {
	BD79703_CHAN(0),
	BD79703_CHAN(1),
	BD79703_CHAN(2),
};

/*
 * The BD79702 has 4 channels. They aren't mapped to BD79703 channels 0, 1, 2
 * and 3, but to the channels 0, 1, 4, 5. So the addressing used with SPI
 * accesses is 1, 2, 5 and 6 for them. Thus, they're not constant offset to
 * the channel number as with other IC variants.
 */
static const struct iio_chan_spec bd79702_channels[] = {
	BD79703_CHAN_ADDR(0, 1),
	BD79703_CHAN_ADDR(1, 2),
	BD79703_CHAN_ADDR(2, 5),
	BD79703_CHAN_ADDR(3, 6),
};

static const struct iio_chan_spec bd79703_channels[] = {
	BD79703_CHAN(0),
	BD79703_CHAN(1),
	BD79703_CHAN(2),
	BD79703_CHAN(3),
	BD79703_CHAN(4),
	BD79703_CHAN(5),
};

static const struct bd7970x_chip_data bd79700_chip_data = {
	.name = "bd79700",
	.channels = bd79700_channels,
	.num_channels = ARRAY_SIZE(bd79700_channels),
	.has_vfs = false,
};

static const struct bd7970x_chip_data bd79701_chip_data = {
	.name = "bd79701",
	.channels = bd79701_channels,
	.num_channels = ARRAY_SIZE(bd79701_channels),
	.has_vfs = false,
};

static const struct bd7970x_chip_data bd79702_chip_data = {
	.name = "bd79702",
	.channels = bd79702_channels,
	.num_channels = ARRAY_SIZE(bd79702_channels),
	.has_vfs = true,
};

static const struct bd7970x_chip_data bd79703_chip_data = {
	.name = "bd79703",
	.channels = bd79703_channels,
	.num_channels = ARRAY_SIZE(bd79703_channels),
	.has_vfs = true,
};

static int bd79703_probe(struct spi_device *spi)
{
	const struct bd7970x_chip_data *cd;
	struct device *dev = &spi->dev;
	struct bd79703_data *data;
	struct iio_dev *idev;
	int ret;

	cd = spi_get_device_match_data(spi);
	if (!cd)
		return -ENODEV;

	idev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!idev)
		return -ENOMEM;

	data = iio_priv(idev);

	data->regmap = devm_regmap_init_spi(spi, &bd79703_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Failed to initialize Regmap\n");

	/*
	 * BD79703 has a separate VFS pin, whereas the BD79700 and BD79701 use
	 * VCC for their full-scale output voltage.
	 */
	if (cd->has_vfs) {
		ret = devm_regulator_get_enable(dev, "vcc");
		if (ret)
			return dev_err_probe(dev, ret, "Failed to enable VCC\n");

		ret = devm_regulator_get_enable_read_voltage(dev, "vfs");
		if (ret < 0)
			return dev_err_probe(dev, ret, "Failed to get Vfs\n");
	} else {
		ret = devm_regulator_get_enable_read_voltage(dev, "vcc");
		if (ret < 0)
			return dev_err_probe(dev, ret, "Failed to get VCC\n");
	}
	data->vfs = ret;

	idev->channels = cd->channels;
	idev->num_channels = cd->num_channels;
	idev->modes = INDIO_DIRECT_MODE;
	idev->info = &bd79703_info;
	idev->name = cd->name;

	/* Initialize all to output zero */
	ret = regmap_write(data->regmap, BD79703_REG_OUT_ALL, 0);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, idev);
}

static const struct spi_device_id bd79703_id[] = {
	{ "bd79700", (kernel_ulong_t)&bd79700_chip_data },
	{ "bd79701", (kernel_ulong_t)&bd79701_chip_data },
	{ "bd79702", (kernel_ulong_t)&bd79702_chip_data },
	{ "bd79703", (kernel_ulong_t)&bd79703_chip_data },
	{ }
};
MODULE_DEVICE_TABLE(spi, bd79703_id);

static const struct of_device_id bd79703_of_match[] = {
	{ .compatible = "rohm,bd79700", .data = &bd79700_chip_data },
	{ .compatible = "rohm,bd79701", .data = &bd79701_chip_data },
	{ .compatible = "rohm,bd79702", .data = &bd79702_chip_data },
	{ .compatible = "rohm,bd79703", .data = &bd79703_chip_data },
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
