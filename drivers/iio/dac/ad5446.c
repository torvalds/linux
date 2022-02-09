// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AD5446 SPI DAC driver
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <asm/unaligned.h>

#define MODE_PWRDWN_1k		0x1
#define MODE_PWRDWN_100k	0x2
#define MODE_PWRDWN_TRISTATE	0x3

/**
 * struct ad5446_state - driver instance specific data
 * @dev:		this device
 * @chip_info:		chip model specific constants, available modes etc
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 * @cached_val:		store/retrieve values during power down
 * @pwr_down_mode:	power down mode (1k, 100k or tristate)
 * @pwr_down:		true if the device is in power down
 * @lock:		lock to protect the data buffer during write ops
 */

struct ad5446_state {
	struct device		*dev;
	const struct ad5446_chip_info	*chip_info;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned			cached_val;
	unsigned			pwr_down_mode;
	unsigned			pwr_down;
	struct mutex			lock;
};

/**
 * struct ad5446_chip_info - chip specific information
 * @channel:		channel spec for the DAC
 * @int_vref_mv:	AD5620/40/60: the internal reference voltage
 * @write:		chip specific helper function to write to the register
 */

struct ad5446_chip_info {
	struct iio_chan_spec	channel;
	u16			int_vref_mv;
	int			(*write)(struct ad5446_state *st, unsigned val);
};

static const char * const ad5446_powerdown_modes[] = {
	"1kohm_to_gnd", "100kohm_to_gnd", "three_state"
};

static int ad5446_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	st->pwr_down_mode = mode + 1;

	return 0;
}

static int ad5446_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	return st->pwr_down_mode - 1;
}

static const struct iio_enum ad5446_powerdown_mode_enum = {
	.items = ad5446_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5446_powerdown_modes),
	.get = ad5446_get_powerdown_mode,
	.set = ad5446_set_powerdown_mode,
};

static ssize_t ad5446_read_dac_powerdown(struct iio_dev *indio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   char *buf)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5446_write_dac_powerdown(struct iio_dev *indio_dev,
					    uintptr_t private,
					    const struct iio_chan_spec *chan,
					    const char *buf, size_t len)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	unsigned int shift;
	unsigned int val;
	bool powerdown;
	int ret;

	ret = strtobool(buf, &powerdown);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	st->pwr_down = powerdown;

	if (st->pwr_down) {
		shift = chan->scan_type.realbits + chan->scan_type.shift;
		val = st->pwr_down_mode << shift;
	} else {
		val = st->cached_val;
	}

	ret = st->chip_info->write(st, val);
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static const struct iio_chan_spec_ext_info ad5446_ext_info_powerdown[] = {
	{
		.name = "powerdown",
		.read = ad5446_read_dac_powerdown,
		.write = ad5446_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad5446_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ad5446_powerdown_mode_enum),
	{ },
};

#define _AD5446_CHANNEL(bits, storage, _shift, ext) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = (storage), \
		.shift = (_shift), \
		}, \
	.ext_info = (ext), \
}

#define AD5446_CHANNEL(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, NULL)

#define AD5446_CHANNEL_POWERDOWN(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, ad5446_ext_info_powerdown)

static int ad5446_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		*val = st->cached_val;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int ad5446_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		val <<= chan->scan_type.shift;
		mutex_lock(&st->lock);
		st->cached_val = val;
		if (!st->pwr_down)
			ret = st->chip_info->write(st, val);
		mutex_unlock(&st->lock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5446_info = {
	.read_raw = ad5446_read_raw,
	.write_raw = ad5446_write_raw,
};

static int ad5446_probe(struct device *dev, const char *name,
			const struct ad5446_chip_info *chip_info)
{
	struct ad5446_state *st;
	struct iio_dev *indio_dev;
	struct regulator *reg;
	int ret, voltage_uv = 0;

	reg = devm_regulator_get(dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(reg);
		if (ret < 0)
			goto error_disable_reg;

		voltage_uv = ret;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	st = iio_priv(indio_dev);
	st->chip_info = chip_info;

	dev_set_drvdata(dev, indio_dev);
	st->reg = reg;
	st->dev = dev;

	indio_dev->name = name;
	indio_dev->info = &ad5446_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;

	mutex_init(&st->lock);

	st->pwr_down_mode = MODE_PWRDWN_1k;

	if (st->chip_info->int_vref_mv)
		st->vref_mv = st->chip_info->int_vref_mv;
	else if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else
		dev_warn(dev, "reference voltage unspecified\n");

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(reg);
	return ret;
}

static void ad5446_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
}

#if IS_ENABLED(CONFIG_SPI_MASTER)

static int ad5446_write(struct ad5446_state *st, unsigned val)
{
	struct spi_device *spi = to_spi_device(st->dev);
	__be16 data = cpu_to_be16(val);

	return spi_write(spi, &data, sizeof(data));
}

static int ad5660_write(struct ad5446_state *st, unsigned val)
{
	struct spi_device *spi = to_spi_device(st->dev);
	uint8_t data[3];

	put_unaligned_be24(val, &data[0]);

	return spi_write(spi, data, sizeof(data));
}

/*
 * ad5446_supported_spi_device_ids:
 * The AD5620/40/60 parts are available in different fixed internal reference
 * voltage options. The actual part numbers may look differently
 * (and a bit cryptic), however this style is used to make clear which
 * parts are supported here.
 */
enum ad5446_supported_spi_device_ids {
	ID_AD5300,
	ID_AD5310,
	ID_AD5320,
	ID_AD5444,
	ID_AD5446,
	ID_AD5450,
	ID_AD5451,
	ID_AD5541A,
	ID_AD5512A,
	ID_AD5553,
	ID_AD5600,
	ID_AD5601,
	ID_AD5611,
	ID_AD5621,
	ID_AD5641,
	ID_AD5620_2500,
	ID_AD5620_1250,
	ID_AD5640_2500,
	ID_AD5640_1250,
	ID_AD5660_2500,
	ID_AD5660_1250,
	ID_AD5662,
};

static const struct ad5446_chip_info ad5446_spi_chip_info[] = {
	[ID_AD5300] = {
		.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5310] = {
		.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 2),
		.write = ad5446_write,
	},
	[ID_AD5320] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5444] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.write = ad5446_write,
	},
	[ID_AD5446] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5450] = {
		.channel = AD5446_CHANNEL(8, 16, 6),
		.write = ad5446_write,
	},
	[ID_AD5451] = {
		.channel = AD5446_CHANNEL(10, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5541A] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5512A] = {
		.channel = AD5446_CHANNEL(12, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5553] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5600] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5601] = {
		.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 6),
		.write = ad5446_write,
	},
	[ID_AD5611] = {
		.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5621] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.write = ad5446_write,
	},
	[ID_AD5641] = {
		.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5620_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.int_vref_mv = 2500,
		.write = ad5446_write,
	},
	[ID_AD5620_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.int_vref_mv = 1250,
		.write = ad5446_write,
	},
	[ID_AD5640_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
		.int_vref_mv = 2500,
		.write = ad5446_write,
	},
	[ID_AD5640_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
		.int_vref_mv = 1250,
		.write = ad5446_write,
	},
	[ID_AD5660_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.int_vref_mv = 2500,
		.write = ad5660_write,
	},
	[ID_AD5660_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.int_vref_mv = 1250,
		.write = ad5660_write,
	},
	[ID_AD5662] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.write = ad5660_write,
	},
};

static const struct spi_device_id ad5446_spi_ids[] = {
	{"ad5300", ID_AD5300},
	{"ad5310", ID_AD5310},
	{"ad5320", ID_AD5320},
	{"ad5444", ID_AD5444},
	{"ad5446", ID_AD5446},
	{"ad5450", ID_AD5450},
	{"ad5451", ID_AD5451},
	{"ad5452", ID_AD5444}, /* ad5452 is compatible to the ad5444 */
	{"ad5453", ID_AD5446}, /* ad5453 is compatible to the ad5446 */
	{"ad5512a", ID_AD5512A},
	{"ad5541a", ID_AD5541A},
	{"ad5542a", ID_AD5541A}, /* ad5541a and ad5542a are compatible */
	{"ad5543", ID_AD5541A}, /* ad5541a and ad5543 are compatible */
	{"ad5553", ID_AD5553},
	{"ad5600", ID_AD5600},
	{"ad5601", ID_AD5601},
	{"ad5611", ID_AD5611},
	{"ad5621", ID_AD5621},
	{"ad5641", ID_AD5641},
	{"ad5620-2500", ID_AD5620_2500}, /* AD5620/40/60: */
	{"ad5620-1250", ID_AD5620_1250}, /* part numbers may look differently */
	{"ad5640-2500", ID_AD5640_2500},
	{"ad5640-1250", ID_AD5640_1250},
	{"ad5660-2500", ID_AD5660_2500},
	{"ad5660-1250", ID_AD5660_1250},
	{"ad5662", ID_AD5662},
	{"dac081s101", ID_AD5300}, /* compatible Texas Instruments chips */
	{"dac101s101", ID_AD5310},
	{"dac121s101", ID_AD5320},
	{"dac7512", ID_AD5320},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5446_spi_ids);

static const struct of_device_id ad5446_of_ids[] = {
	{ .compatible = "ti,dac7512" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5446_of_ids);

static int ad5446_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	return ad5446_probe(&spi->dev, id->name,
		&ad5446_spi_chip_info[id->driver_data]);
}

static void ad5446_spi_remove(struct spi_device *spi)
{
	ad5446_remove(&spi->dev);
}

static struct spi_driver ad5446_spi_driver = {
	.driver = {
		.name	= "ad5446",
		.of_match_table = ad5446_of_ids,
	},
	.probe		= ad5446_spi_probe,
	.remove		= ad5446_spi_remove,
	.id_table	= ad5446_spi_ids,
};

static int __init ad5446_spi_register_driver(void)
{
	return spi_register_driver(&ad5446_spi_driver);
}

static void ad5446_spi_unregister_driver(void)
{
	spi_unregister_driver(&ad5446_spi_driver);
}

#else

static inline int ad5446_spi_register_driver(void) { return 0; }
static inline void ad5446_spi_unregister_driver(void) { }

#endif

#if IS_ENABLED(CONFIG_I2C)

static int ad5622_write(struct ad5446_state *st, unsigned val)
{
	struct i2c_client *client = to_i2c_client(st->dev);
	__be16 data = cpu_to_be16(val);
	int ret;

	ret = i2c_master_send(client, (char *)&data, sizeof(data));
	if (ret < 0)
		return ret;
	if (ret != sizeof(data))
		return -EIO;

	return 0;
}

/*
 * ad5446_supported_i2c_device_ids:
 * The AD5620/40/60 parts are available in different fixed internal reference
 * voltage options. The actual part numbers may look differently
 * (and a bit cryptic), however this style is used to make clear which
 * parts are supported here.
 */
enum ad5446_supported_i2c_device_ids {
	ID_AD5602,
	ID_AD5612,
	ID_AD5622,
};

static const struct ad5446_chip_info ad5446_i2c_chip_info[] = {
	[ID_AD5602] = {
		.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 4),
		.write = ad5622_write,
	},
	[ID_AD5612] = {
		.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 2),
		.write = ad5622_write,
	},
	[ID_AD5622] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 0),
		.write = ad5622_write,
	},
};

static int ad5446_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	return ad5446_probe(&i2c->dev, id->name,
		&ad5446_i2c_chip_info[id->driver_data]);
}

static int ad5446_i2c_remove(struct i2c_client *i2c)
{
	ad5446_remove(&i2c->dev);

	return 0;
}

static const struct i2c_device_id ad5446_i2c_ids[] = {
	{"ad5301", ID_AD5602},
	{"ad5311", ID_AD5612},
	{"ad5321", ID_AD5622},
	{"ad5602", ID_AD5602},
	{"ad5612", ID_AD5612},
	{"ad5622", ID_AD5622},
	{}
};
MODULE_DEVICE_TABLE(i2c, ad5446_i2c_ids);

static struct i2c_driver ad5446_i2c_driver = {
	.driver = {
		   .name = "ad5446",
	},
	.probe = ad5446_i2c_probe,
	.remove = ad5446_i2c_remove,
	.id_table = ad5446_i2c_ids,
};

static int __init ad5446_i2c_register_driver(void)
{
	return i2c_add_driver(&ad5446_i2c_driver);
}

static void __exit ad5446_i2c_unregister_driver(void)
{
	i2c_del_driver(&ad5446_i2c_driver);
}

#else

static inline int ad5446_i2c_register_driver(void) { return 0; }
static inline void ad5446_i2c_unregister_driver(void) { }

#endif

static int __init ad5446_init(void)
{
	int ret;

	ret = ad5446_spi_register_driver();
	if (ret)
		return ret;

	ret = ad5446_i2c_register_driver();
	if (ret) {
		ad5446_spi_unregister_driver();
		return ret;
	}

	return 0;
}
module_init(ad5446_init);

static void __exit ad5446_exit(void)
{
	ad5446_i2c_unregister_driver();
	ad5446_spi_unregister_driver();
}
module_exit(ad5446_exit);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5444/AD5446 DAC");
MODULE_LICENSE("GPL v2");
