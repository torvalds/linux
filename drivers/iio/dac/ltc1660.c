// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Linear Technology LTC1665/LTC1660, 8 channels DAC
 *
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 */
#include <linux/bitops.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#define LTC1660_REG_WAKE	0x0
#define LTC1660_REG_DAC_A	0x1
#define LTC1660_REG_DAC_B	0x2
#define LTC1660_REG_DAC_C	0x3
#define LTC1660_REG_DAC_D	0x4
#define LTC1660_REG_DAC_E	0x5
#define LTC1660_REG_DAC_F	0x6
#define LTC1660_REG_DAC_G	0x7
#define LTC1660_REG_DAC_H	0x8
#define LTC1660_REG_SLEEP	0xe

#define LTC1660_NUM_CHANNELS	8

static const struct regmap_config ltc1660_regmap_config = {
	.reg_bits = 4,
	.val_bits = 12,
};

enum ltc1660_supported_device_ids {
	ID_LTC1660,
	ID_LTC1665,
};

struct ltc1660_priv {
	struct spi_device *spi;
	struct regmap *regmap;
	struct regulator *vref_reg;
	unsigned int value[LTC1660_NUM_CHANNELS];
	unsigned int vref_mv;
};

static int ltc1660_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int *val,
		int *val2,
		long mask)
{
	struct ltc1660_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = priv->value[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = regulator_get_voltage(priv->vref_reg);
		if (*val < 0) {
			dev_err(&priv->spi->dev, "failed to read vref regulator: %d\n",
					*val);
			return *val;
		}

		/* Convert to mV */
		*val /= 1000;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int ltc1660_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int val,
		int val2,
		long mask)
{
	struct ltc1660_priv *priv = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val2 != 0)
			return -EINVAL;

		if (val < 0 || val > GENMASK(chan->scan_type.realbits - 1, 0))
			return -EINVAL;

		ret = regmap_write(priv->regmap, chan->channel,
					(val << chan->scan_type.shift));
		if (!ret)
			priv->value[chan->channel] = val;

		return ret;
	default:
		return -EINVAL;
	}
}

#define LTC1660_CHAN(chan, bits) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.output = 1,					\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_type = {					\
		.sign = 'u',				\
		.realbits = (bits),			\
		.storagebits = 16,			\
		.shift = 12 - (bits),			\
	},						\
}

#define  LTC1660_OCTAL_CHANNELS(bits) {			\
		LTC1660_CHAN(LTC1660_REG_DAC_A, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_B, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_C, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_D, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_E, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_F, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_G, bits),	\
		LTC1660_CHAN(LTC1660_REG_DAC_H, bits),	\
}

static const struct iio_chan_spec ltc1660_channels[][LTC1660_NUM_CHANNELS] = {
	[ID_LTC1660] = LTC1660_OCTAL_CHANNELS(10),
	[ID_LTC1665] = LTC1660_OCTAL_CHANNELS(8),
};

static const struct iio_info ltc1660_info = {
	.read_raw = &ltc1660_read_raw,
	.write_raw = &ltc1660_write_raw,
};

static int __maybe_unused ltc1660_suspend(struct device *dev)
{
	struct ltc1660_priv *priv = iio_priv(spi_get_drvdata(
						to_spi_device(dev)));
	return regmap_write(priv->regmap, LTC1660_REG_SLEEP, 0x00);
}

static int __maybe_unused ltc1660_resume(struct device *dev)
{
	struct ltc1660_priv *priv = iio_priv(spi_get_drvdata(
						to_spi_device(dev)));
	return regmap_write(priv->regmap, LTC1660_REG_WAKE, 0x00);
}
static SIMPLE_DEV_PM_OPS(ltc1660_pm_ops, ltc1660_suspend, ltc1660_resume);

static int ltc1660_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ltc1660_priv *priv;
	const struct spi_device_id *id = spi_get_device_id(spi);
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*priv));
	if (indio_dev == NULL)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->regmap = devm_regmap_init_spi(spi, &ltc1660_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&spi->dev, "failed to register spi regmap %ld\n",
			PTR_ERR(priv->regmap));
		return PTR_ERR(priv->regmap);
	}

	priv->vref_reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(priv->vref_reg))
		return dev_err_probe(&spi->dev, PTR_ERR(priv->vref_reg),
				     "vref regulator not specified\n");

	ret = regulator_enable(priv->vref_reg);
	if (ret) {
		dev_err(&spi->dev, "failed to enable vref regulator: %d\n",
				ret);
		return ret;
	}

	priv->spi = spi;
	spi_set_drvdata(spi, indio_dev);
	indio_dev->info = &ltc1660_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc1660_channels[id->driver_data];
	indio_dev->num_channels = LTC1660_NUM_CHANNELS;
	indio_dev->name = id->name;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "failed to register iio device: %d\n",
				ret);
		goto error_disable_reg;
	}

	return 0;

error_disable_reg:
	regulator_disable(priv->vref_reg);

	return ret;
}

static void ltc1660_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ltc1660_priv *priv = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(priv->vref_reg);
}

static const struct of_device_id ltc1660_dt_ids[] = {
	{ .compatible = "lltc,ltc1660", .data = (void *)ID_LTC1660 },
	{ .compatible = "lltc,ltc1665", .data = (void *)ID_LTC1665 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ltc1660_dt_ids);

static const struct spi_device_id ltc1660_id[] = {
	{"ltc1660", ID_LTC1660},
	{"ltc1665", ID_LTC1665},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, ltc1660_id);

static struct spi_driver ltc1660_driver = {
	.driver = {
		.name = "ltc1660",
		.of_match_table = ltc1660_dt_ids,
		.pm = &ltc1660_pm_ops,
	},
	.probe	= ltc1660_probe,
	.remove = ltc1660_remove,
	.id_table = ltc1660_id,
};
module_spi_driver(ltc1660_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_DESCRIPTION("Linear Technology LTC1660/LTC1665 DAC");
MODULE_LICENSE("GPL v2");
