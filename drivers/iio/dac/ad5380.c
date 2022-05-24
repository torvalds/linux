// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog devices AD5380, AD5381, AD5382, AD5383, AD5390, AD5391, AD5392
 * multi-channel Digital to Analog Converters driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AD5380_REG_DATA(x)	(((x) << 2) | 3)
#define AD5380_REG_OFFSET(x)	(((x) << 2) | 2)
#define AD5380_REG_GAIN(x)	(((x) << 2) | 1)
#define AD5380_REG_SF_PWR_DOWN	(8 << 2)
#define AD5380_REG_SF_PWR_UP	(9 << 2)
#define AD5380_REG_SF_CTRL	(12 << 2)

#define AD5380_CTRL_PWR_DOWN_MODE_OFFSET	13
#define AD5380_CTRL_INT_VREF_2V5		BIT(12)
#define AD5380_CTRL_INT_VREF_EN			BIT(10)

/**
 * struct ad5380_chip_info - chip specific information
 * @channel_template:	channel specification template
 * @num_channels:	number of channels
 * @int_vref:		internal vref in uV
*/

struct ad5380_chip_info {
	struct iio_chan_spec	channel_template;
	unsigned int		num_channels;
	unsigned int		int_vref;
};

/**
 * struct ad5380_state - driver instance specific data
 * @regmap:		regmap instance used by the device
 * @chip_info:		chip model specific constants, available modes etc
 * @vref_reg:		vref supply regulator
 * @vref:		actual reference voltage used in uA
 * @pwr_down:		whether the chip is currently in power down mode
 * @lock:		lock to protect the data buffer during regmap ops
 */

struct ad5380_state {
	struct regmap			*regmap;
	const struct ad5380_chip_info	*chip_info;
	struct regulator		*vref_reg;
	int				vref;
	bool				pwr_down;
	struct mutex			lock;
};

enum ad5380_type {
	ID_AD5380_3,
	ID_AD5380_5,
	ID_AD5381_3,
	ID_AD5381_5,
	ID_AD5382_3,
	ID_AD5382_5,
	ID_AD5383_3,
	ID_AD5383_5,
	ID_AD5390_3,
	ID_AD5390_5,
	ID_AD5391_3,
	ID_AD5391_5,
	ID_AD5392_3,
	ID_AD5392_5,
};

static ssize_t ad5380_read_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad5380_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5380_write_dac_powerdown(struct iio_dev *indio_dev,
	 uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	 size_t len)
{
	struct ad5380_state *st = iio_priv(indio_dev);
	bool pwr_down;
	int ret;

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	if (pwr_down)
		ret = regmap_write(st->regmap, AD5380_REG_SF_PWR_DOWN, 0);
	else
		ret = regmap_write(st->regmap, AD5380_REG_SF_PWR_UP, 0);

	st->pwr_down = pwr_down;

	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static const char * const ad5380_powerdown_modes[] = {
	"100kohm_to_gnd",
	"three_state",
};

static int ad5380_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct ad5380_state *st = iio_priv(indio_dev);
	unsigned int mode;
	int ret;

	ret = regmap_read(st->regmap, AD5380_REG_SF_CTRL, &mode);
	if (ret)
		return ret;

	mode = (mode >> AD5380_CTRL_PWR_DOWN_MODE_OFFSET) & 1;

	return mode;
}

static int ad5380_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ad5380_state *st = iio_priv(indio_dev);
	int ret;

	ret = regmap_update_bits(st->regmap, AD5380_REG_SF_CTRL,
		1 << AD5380_CTRL_PWR_DOWN_MODE_OFFSET,
		mode << AD5380_CTRL_PWR_DOWN_MODE_OFFSET);

	return ret;
}

static const struct iio_enum ad5380_powerdown_mode_enum = {
	.items = ad5380_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5380_powerdown_modes),
	.get = ad5380_get_powerdown_mode,
	.set = ad5380_set_powerdown_mode,
};

static unsigned int ad5380_info_to_reg(struct iio_chan_spec const *chan,
	long info)
{
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return AD5380_REG_DATA(chan->address);
	case IIO_CHAN_INFO_CALIBBIAS:
		return AD5380_REG_OFFSET(chan->address);
	case IIO_CHAN_INFO_CALIBSCALE:
		return AD5380_REG_GAIN(chan->address);
	default:
		break;
	}

	return 0;
}

static int ad5380_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long info)
{
	const unsigned int max_val = (1 << chan->scan_type.realbits);
	struct ad5380_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return regmap_write(st->regmap,
			ad5380_info_to_reg(chan, info),
			val << chan->scan_type.shift);
	case IIO_CHAN_INFO_CALIBBIAS:
		val += (1 << chan->scan_type.realbits) / 2;
		if (val >= max_val || val < 0)
			return -EINVAL;

		return regmap_write(st->regmap,
			AD5380_REG_OFFSET(chan->address),
			val << chan->scan_type.shift);
	default:
		break;
	}
	return -EINVAL;
}

static int ad5380_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct ad5380_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_CALIBSCALE:
		ret = regmap_read(st->regmap, ad5380_info_to_reg(chan, info),
					val);
		if (ret)
			return ret;
		*val >>= chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = regmap_read(st->regmap, AD5380_REG_OFFSET(chan->address),
					val);
		if (ret)
			return ret;
		*val >>= chan->scan_type.shift;
		*val -= (1 << chan->scan_type.realbits) / 2;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 2 * st->vref;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info ad5380_info = {
	.read_raw = ad5380_read_raw,
	.write_raw = ad5380_write_raw,
};

static const struct iio_chan_spec_ext_info ad5380_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5380_read_dac_powerdown,
		.write = ad5380_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SHARED_BY_TYPE,
		 &ad5380_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ad5380_powerdown_mode_enum),
	{ },
};

#define AD5380_CHANNEL(_bits) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |			\
		BIT(IIO_CHAN_INFO_CALIBBIAS),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = (_bits),				\
		.storagebits =  16,				\
		.shift = 14 - (_bits),				\
	},							\
	.ext_info = ad5380_ext_info,				\
}

static const struct ad5380_chip_info ad5380_chip_info_tbl[] = {
	[ID_AD5380_3] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 40,
		.int_vref = 1250,
	},
	[ID_AD5380_5] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 40,
		.int_vref = 2500,
	},
	[ID_AD5381_3] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 16,
		.int_vref = 1250,
	},
	[ID_AD5381_5] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 16,
		.int_vref = 2500,
	},
	[ID_AD5382_3] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 32,
		.int_vref = 1250,
	},
	[ID_AD5382_5] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 32,
		.int_vref = 2500,
	},
	[ID_AD5383_3] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 32,
		.int_vref = 1250,
	},
	[ID_AD5383_5] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 32,
		.int_vref = 2500,
	},
	[ID_AD5390_3] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 16,
		.int_vref = 1250,
	},
	[ID_AD5390_5] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 16,
		.int_vref = 2500,
	},
	[ID_AD5391_3] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 16,
		.int_vref = 1250,
	},
	[ID_AD5391_5] = {
		.channel_template = AD5380_CHANNEL(12),
		.num_channels = 16,
		.int_vref = 2500,
	},
	[ID_AD5392_3] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 8,
		.int_vref = 1250,
	},
	[ID_AD5392_5] = {
		.channel_template = AD5380_CHANNEL(14),
		.num_channels = 8,
		.int_vref = 2500,
	},
};

static int ad5380_alloc_channels(struct iio_dev *indio_dev)
{
	struct ad5380_state *st = iio_priv(indio_dev);
	struct iio_chan_spec *channels;
	unsigned int i;

	channels = kcalloc(st->chip_info->num_channels,
			   sizeof(struct iio_chan_spec), GFP_KERNEL);

	if (!channels)
		return -ENOMEM;

	for (i = 0; i < st->chip_info->num_channels; ++i) {
		channels[i] = st->chip_info->channel_template;
		channels[i].channel = i;
		channels[i].address = i;
	}

	indio_dev->channels = channels;

	return 0;
}

static int ad5380_probe(struct device *dev, struct regmap *regmap,
			enum ad5380_type type, const char *name)
{
	struct iio_dev *indio_dev;
	struct ad5380_state *st;
	unsigned int ctrl = 0;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (indio_dev == NULL) {
		dev_err(dev, "Failed to allocate iio device\n");
		return -ENOMEM;
	}

	st = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);

	st->chip_info = &ad5380_chip_info_tbl[type];
	st->regmap = regmap;

	indio_dev->name = name;
	indio_dev->info = &ad5380_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = st->chip_info->num_channels;

	mutex_init(&st->lock);

	ret = ad5380_alloc_channels(indio_dev);
	if (ret) {
		dev_err(dev, "Failed to allocate channel spec: %d\n", ret);
		return ret;
	}

	if (st->chip_info->int_vref == 2500)
		ctrl |= AD5380_CTRL_INT_VREF_2V5;

	st->vref_reg = devm_regulator_get(dev, "vref");
	if (!IS_ERR(st->vref_reg)) {
		ret = regulator_enable(st->vref_reg);
		if (ret) {
			dev_err(dev, "Failed to enable vref regulators: %d\n",
				ret);
			goto error_free_reg;
		}

		ret = regulator_get_voltage(st->vref_reg);
		if (ret < 0)
			goto error_disable_reg;

		st->vref = ret / 1000;
	} else {
		st->vref = st->chip_info->int_vref;
		ctrl |= AD5380_CTRL_INT_VREF_EN;
	}

	ret = regmap_write(st->regmap, AD5380_REG_SF_CTRL, ctrl);
	if (ret) {
		dev_err(dev, "Failed to write to device: %d\n", ret);
		goto error_disable_reg;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "Failed to register iio device: %d\n", ret);
		goto error_disable_reg;
	}

	return 0;

error_disable_reg:
	if (!IS_ERR(st->vref_reg))
		regulator_disable(st->vref_reg);
error_free_reg:
	kfree(indio_dev->channels);

	return ret;
}

static void ad5380_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5380_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	kfree(indio_dev->channels);

	if (!IS_ERR(st->vref_reg))
		regulator_disable(st->vref_reg);
}

static bool ad5380_reg_false(struct device *dev, unsigned int reg)
{
	return false;
}

static const struct regmap_config ad5380_regmap_config = {
	.reg_bits = 10,
	.val_bits = 14,

	.max_register = AD5380_REG_DATA(40),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = ad5380_reg_false,
	.readable_reg = ad5380_reg_false,
};

#if IS_ENABLED(CONFIG_SPI_MASTER)

static int ad5380_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &ad5380_regmap_config);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return ad5380_probe(&spi->dev, regmap, id->driver_data, id->name);
}

static void ad5380_spi_remove(struct spi_device *spi)
{
	ad5380_remove(&spi->dev);
}

static const struct spi_device_id ad5380_spi_ids[] = {
	{ "ad5380-3", ID_AD5380_3 },
	{ "ad5380-5", ID_AD5380_5 },
	{ "ad5381-3", ID_AD5381_3 },
	{ "ad5381-5", ID_AD5381_5 },
	{ "ad5382-3", ID_AD5382_3 },
	{ "ad5382-5", ID_AD5382_5 },
	{ "ad5383-3", ID_AD5383_3 },
	{ "ad5383-5", ID_AD5383_5 },
	{ "ad5384-3", ID_AD5380_3 },
	{ "ad5384-5", ID_AD5380_5 },
	{ "ad5390-3", ID_AD5390_3 },
	{ "ad5390-5", ID_AD5390_5 },
	{ "ad5391-3", ID_AD5391_3 },
	{ "ad5391-5", ID_AD5391_5 },
	{ "ad5392-3", ID_AD5392_3 },
	{ "ad5392-5", ID_AD5392_5 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad5380_spi_ids);

static struct spi_driver ad5380_spi_driver = {
	.driver = {
		   .name = "ad5380",
	},
	.probe = ad5380_spi_probe,
	.remove = ad5380_spi_remove,
	.id_table = ad5380_spi_ids,
};

static inline int ad5380_spi_register_driver(void)
{
	return spi_register_driver(&ad5380_spi_driver);
}

static inline void ad5380_spi_unregister_driver(void)
{
	spi_unregister_driver(&ad5380_spi_driver);
}

#else

static inline int ad5380_spi_register_driver(void)
{
	return 0;
}

static inline void ad5380_spi_unregister_driver(void)
{
}

#endif

#if IS_ENABLED(CONFIG_I2C)

static int ad5380_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &ad5380_regmap_config);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return ad5380_probe(&i2c->dev, regmap, id->driver_data, id->name);
}

static int ad5380_i2c_remove(struct i2c_client *i2c)
{
	ad5380_remove(&i2c->dev);

	return 0;
}

static const struct i2c_device_id ad5380_i2c_ids[] = {
	{ "ad5380-3", ID_AD5380_3 },
	{ "ad5380-5", ID_AD5380_5 },
	{ "ad5381-3", ID_AD5381_3 },
	{ "ad5381-5", ID_AD5381_5 },
	{ "ad5382-3", ID_AD5382_3 },
	{ "ad5382-5", ID_AD5382_5 },
	{ "ad5383-3", ID_AD5383_3 },
	{ "ad5383-5", ID_AD5383_5 },
	{ "ad5384-3", ID_AD5380_3 },
	{ "ad5384-5", ID_AD5380_5 },
	{ "ad5390-3", ID_AD5390_3 },
	{ "ad5390-5", ID_AD5390_5 },
	{ "ad5391-3", ID_AD5391_3 },
	{ "ad5391-5", ID_AD5391_5 },
	{ "ad5392-3", ID_AD5392_3 },
	{ "ad5392-5", ID_AD5392_5 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5380_i2c_ids);

static struct i2c_driver ad5380_i2c_driver = {
	.driver = {
		   .name = "ad5380",
	},
	.probe = ad5380_i2c_probe,
	.remove = ad5380_i2c_remove,
	.id_table = ad5380_i2c_ids,
};

static inline int ad5380_i2c_register_driver(void)
{
	return i2c_add_driver(&ad5380_i2c_driver);
}

static inline void ad5380_i2c_unregister_driver(void)
{
	i2c_del_driver(&ad5380_i2c_driver);
}

#else

static inline int ad5380_i2c_register_driver(void)
{
	return 0;
}

static inline void ad5380_i2c_unregister_driver(void)
{
}

#endif

static int __init ad5380_spi_init(void)
{
	int ret;

	ret = ad5380_spi_register_driver();
	if (ret)
		return ret;

	ret = ad5380_i2c_register_driver();
	if (ret) {
		ad5380_spi_unregister_driver();
		return ret;
	}

	return 0;
}
module_init(ad5380_spi_init);

static void __exit ad5380_spi_exit(void)
{
	ad5380_i2c_unregister_driver();
	ad5380_spi_unregister_driver();

}
module_exit(ad5380_spi_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5380/81/82/83/84/90/91/92 DAC");
MODULE_LICENSE("GPL v2");
