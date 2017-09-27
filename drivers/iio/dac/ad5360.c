/*
 * Analog devices AD5360, AD5361, AD5362, AD5363, AD5370, AD5371, AD5373
 * multi-channel Digital to Analog Converters driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AD5360_CMD(x)				((x) << 22)
#define AD5360_ADDR(x)				((x) << 16)

#define AD5360_READBACK_TYPE(x)			((x) << 13)
#define AD5360_READBACK_ADDR(x)			((x) << 7)

#define AD5360_CHAN_ADDR(chan)			((chan) + 0x8)

#define AD5360_CMD_WRITE_DATA			0x3
#define AD5360_CMD_WRITE_OFFSET			0x2
#define AD5360_CMD_WRITE_GAIN			0x1
#define AD5360_CMD_SPECIAL_FUNCTION		0x0

/* Special function register addresses */
#define AD5360_REG_SF_NOP			0x0
#define AD5360_REG_SF_CTRL			0x1
#define AD5360_REG_SF_OFS(x)			(0x2 + (x))
#define AD5360_REG_SF_READBACK			0x5

#define AD5360_SF_CTRL_PWR_DOWN			BIT(0)

#define AD5360_READBACK_X1A			0x0
#define AD5360_READBACK_X1B			0x1
#define AD5360_READBACK_OFFSET			0x2
#define AD5360_READBACK_GAIN			0x3
#define AD5360_READBACK_SF			0x4


/**
 * struct ad5360_chip_info - chip specific information
 * @channel_template:	channel specification template
 * @num_channels:	number of channels
 * @channels_per_group:	number of channels per group
 * @num_vrefs:		number of vref supplies for the chip
*/

struct ad5360_chip_info {
	struct iio_chan_spec	channel_template;
	unsigned int		num_channels;
	unsigned int		channels_per_group;
	unsigned int		num_vrefs;
};

/**
 * struct ad5360_state - driver instance specific data
 * @spi:		spi_device
 * @chip_info:		chip model specific constants, available modes etc
 * @vref_reg:		vref supply regulators
 * @ctrl:		control register cache
 * @data:		spi transfer buffers
 */

struct ad5360_state {
	struct spi_device		*spi;
	const struct ad5360_chip_info	*chip_info;
	struct regulator_bulk_data	vref_reg[3];
	unsigned int			ctrl;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		__be32 d32;
		u8 d8[4];
	} data[2] ____cacheline_aligned;
};

enum ad5360_type {
	ID_AD5360,
	ID_AD5361,
	ID_AD5362,
	ID_AD5363,
	ID_AD5370,
	ID_AD5371,
	ID_AD5372,
	ID_AD5373,
};

#define AD5360_CHANNEL(bits) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_OFFSET) |				\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |			\
		BIT(IIO_CHAN_INFO_CALIBBIAS),			\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = (bits),				\
		.storagebits = 16,				\
		.shift = 16 - (bits),				\
	},							\
}

static const struct ad5360_chip_info ad5360_chip_info_tbl[] = {
	[ID_AD5360] = {
		.channel_template = AD5360_CHANNEL(16),
		.num_channels = 16,
		.channels_per_group = 8,
		.num_vrefs = 2,
	},
	[ID_AD5361] = {
		.channel_template = AD5360_CHANNEL(14),
		.num_channels = 16,
		.channels_per_group = 8,
		.num_vrefs = 2,
	},
	[ID_AD5362] = {
		.channel_template = AD5360_CHANNEL(16),
		.num_channels = 8,
		.channels_per_group = 4,
		.num_vrefs = 2,
	},
	[ID_AD5363] = {
		.channel_template = AD5360_CHANNEL(14),
		.num_channels = 8,
		.channels_per_group = 4,
		.num_vrefs = 2,
	},
	[ID_AD5370] = {
		.channel_template = AD5360_CHANNEL(16),
		.num_channels = 40,
		.channels_per_group = 8,
		.num_vrefs = 2,
	},
	[ID_AD5371] = {
		.channel_template = AD5360_CHANNEL(14),
		.num_channels = 40,
		.channels_per_group = 8,
		.num_vrefs = 3,
	},
	[ID_AD5372] = {
		.channel_template = AD5360_CHANNEL(16),
		.num_channels = 32,
		.channels_per_group = 8,
		.num_vrefs = 2,
	},
	[ID_AD5373] = {
		.channel_template = AD5360_CHANNEL(14),
		.num_channels = 32,
		.channels_per_group = 8,
		.num_vrefs = 2,
	},
};

static unsigned int ad5360_get_channel_vref_index(struct ad5360_state *st,
	unsigned int channel)
{
	unsigned int i;

	/* The first groups have their own vref, while the remaining groups
	 * share the last vref */
	i = channel / st->chip_info->channels_per_group;
	if (i >= st->chip_info->num_vrefs)
		i = st->chip_info->num_vrefs - 1;

	return i;
}

static int ad5360_get_channel_vref(struct ad5360_state *st,
	unsigned int channel)
{
	unsigned int i = ad5360_get_channel_vref_index(st, channel);

	return regulator_get_voltage(st->vref_reg[i].consumer);
}


static int ad5360_write_unlocked(struct iio_dev *indio_dev,
	unsigned int cmd, unsigned int addr, unsigned int val,
	unsigned int shift)
{
	struct ad5360_state *st = iio_priv(indio_dev);

	val <<= shift;
	val |= AD5360_CMD(cmd) | AD5360_ADDR(addr);
	st->data[0].d32 = cpu_to_be32(val);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5360_write(struct iio_dev *indio_dev, unsigned int cmd,
	unsigned int addr, unsigned int val, unsigned int shift)
{
	int ret;

	mutex_lock(&indio_dev->mlock);
	ret = ad5360_write_unlocked(indio_dev, cmd, addr, val, shift);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad5360_read(struct iio_dev *indio_dev, unsigned int type,
	unsigned int addr)
{
	struct ad5360_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.len = 3,
			.cs_change = 1,
		}, {
			.rx_buf = &st->data[1].d8[1],
			.len = 3,
		},
	};

	mutex_lock(&indio_dev->mlock);

	st->data[0].d32 = cpu_to_be32(AD5360_CMD(AD5360_CMD_SPECIAL_FUNCTION) |
		AD5360_ADDR(AD5360_REG_SF_READBACK) |
		AD5360_READBACK_TYPE(type) |
		AD5360_READBACK_ADDR(addr));

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret >= 0)
		ret = be32_to_cpu(st->data[1].d32) & 0xffff;

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t ad5360_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5360_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", (bool)(st->ctrl & AD5360_SF_CTRL_PWR_DOWN));
}

static int ad5360_update_ctrl(struct iio_dev *indio_dev, unsigned int set,
	unsigned int clr)
{
	struct ad5360_state *st = iio_priv(indio_dev);
	unsigned int ret;

	mutex_lock(&indio_dev->mlock);

	st->ctrl |= set;
	st->ctrl &= ~clr;

	ret = ad5360_write_unlocked(indio_dev, AD5360_CMD_SPECIAL_FUNCTION,
			AD5360_REG_SF_CTRL, st->ctrl, 0);

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t ad5360_write_dac_powerdown(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool pwr_down;
	int ret;

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	if (pwr_down)
		ret = ad5360_update_ctrl(indio_dev, AD5360_SF_CTRL_PWR_DOWN, 0);
	else
		ret = ad5360_update_ctrl(indio_dev, 0, AD5360_SF_CTRL_PWR_DOWN);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(out_voltage_powerdown,
			S_IRUGO | S_IWUSR,
			ad5360_read_dac_powerdown,
			ad5360_write_dac_powerdown, 0);

static struct attribute *ad5360_attributes[] = {
	&iio_dev_attr_out_voltage_powerdown.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5360_attribute_group = {
	.attrs = ad5360_attributes,
};

static int ad5360_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5360_state *st = iio_priv(indio_dev);
	int max_val = (1 << chan->scan_type.realbits);
	unsigned int ofs_index;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5360_write(indio_dev, AD5360_CMD_WRITE_DATA,
				 chan->address, val, chan->scan_type.shift);

	case IIO_CHAN_INFO_CALIBBIAS:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5360_write(indio_dev, AD5360_CMD_WRITE_OFFSET,
				 chan->address, val, chan->scan_type.shift);

	case IIO_CHAN_INFO_CALIBSCALE:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5360_write(indio_dev, AD5360_CMD_WRITE_GAIN,
				 chan->address, val, chan->scan_type.shift);

	case IIO_CHAN_INFO_OFFSET:
		if (val <= -max_val || val > 0)
			return -EINVAL;

		val = -val;

		/* offset is supposed to have the same scale as raw, but it
		 * is always 14bits wide, so on a chip where the raw value has
		 * more bits, we need to shift offset. */
		val >>= (chan->scan_type.realbits - 14);

		/* There is one DAC offset register per vref. Changing one
		 * channels offset will also change the offset for all other
		 * channels which share the same vref supply. */
		ofs_index = ad5360_get_channel_vref_index(st, chan->channel);
		return ad5360_write(indio_dev, AD5360_CMD_SPECIAL_FUNCTION,
				 AD5360_REG_SF_OFS(ofs_index), val, 0);
	default:
		break;
	}

	return -EINVAL;
}

static int ad5360_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5360_state *st = iio_priv(indio_dev);
	unsigned int ofs_index;
	int scale_uv;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5360_read(indio_dev, AD5360_READBACK_X1A,
			chan->address);
		if (ret < 0)
			return ret;
		*val = ret >> chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = ad5360_get_channel_vref(st, chan->channel);
		if (scale_uv < 0)
			return scale_uv;

		/* vout = 4 * vref * dac_code */
		*val = scale_uv * 4 / 1000;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = ad5360_read(indio_dev, AD5360_READBACK_OFFSET,
			chan->address);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		ret = ad5360_read(indio_dev, AD5360_READBACK_GAIN,
			chan->address);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		ofs_index = ad5360_get_channel_vref_index(st, chan->channel);
		ret = ad5360_read(indio_dev, AD5360_READBACK_SF,
			AD5360_REG_SF_OFS(ofs_index));
		if (ret < 0)
			return ret;

		ret <<= (chan->scan_type.realbits - 14);
		*val = -ret;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info ad5360_info = {
	.read_raw = ad5360_read_raw,
	.write_raw = ad5360_write_raw,
	.attrs = &ad5360_attribute_group,
	.driver_module = THIS_MODULE,
};

static const char * const ad5360_vref_name[] = {
	 "vref0", "vref1", "vref2"
};

static int ad5360_alloc_channels(struct iio_dev *indio_dev)
{
	struct ad5360_state *st = iio_priv(indio_dev);
	struct iio_chan_spec *channels;
	unsigned int i;

	channels = kcalloc(st->chip_info->num_channels,
			   sizeof(struct iio_chan_spec), GFP_KERNEL);

	if (!channels)
		return -ENOMEM;

	for (i = 0; i < st->chip_info->num_channels; ++i) {
		channels[i] = st->chip_info->channel_template;
		channels[i].channel = i;
		channels[i].address = AD5360_CHAN_ADDR(i);
	}

	indio_dev->channels = channels;

	return 0;
}

static int ad5360_probe(struct spi_device *spi)
{
	enum ad5360_type type = spi_get_device_id(spi)->driver_data;
	struct iio_dev *indio_dev;
	struct ad5360_state *st;
	unsigned int i;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL) {
		dev_err(&spi->dev, "Failed to allocate iio device\n");
		return  -ENOMEM;
	}

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->chip_info = &ad5360_chip_info_tbl[type];
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5360_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = st->chip_info->num_channels;

	ret = ad5360_alloc_channels(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "Failed to allocate channel spec: %d\n", ret);
		return ret;
	}

	for (i = 0; i < st->chip_info->num_vrefs; ++i)
		st->vref_reg[i].supply = ad5360_vref_name[i];

	ret = devm_regulator_bulk_get(&st->spi->dev, st->chip_info->num_vrefs,
		st->vref_reg);
	if (ret) {
		dev_err(&spi->dev, "Failed to request vref regulators: %d\n", ret);
		goto error_free_channels;
	}

	ret = regulator_bulk_enable(st->chip_info->num_vrefs, st->vref_reg);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable vref regulators: %d\n", ret);
		goto error_free_channels;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "Failed to register iio device: %d\n", ret);
		goto error_disable_reg;
	}

	return 0;

error_disable_reg:
	regulator_bulk_disable(st->chip_info->num_vrefs, st->vref_reg);
error_free_channels:
	kfree(indio_dev->channels);

	return ret;
}

static int ad5360_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5360_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	kfree(indio_dev->channels);

	regulator_bulk_disable(st->chip_info->num_vrefs, st->vref_reg);

	return 0;
}

static const struct spi_device_id ad5360_ids[] = {
	{ "ad5360", ID_AD5360 },
	{ "ad5361", ID_AD5361 },
	{ "ad5362", ID_AD5362 },
	{ "ad5363", ID_AD5363 },
	{ "ad5370", ID_AD5370 },
	{ "ad5371", ID_AD5371 },
	{ "ad5372", ID_AD5372 },
	{ "ad5373", ID_AD5373 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad5360_ids);

static struct spi_driver ad5360_driver = {
	.driver = {
		   .name = "ad5360",
	},
	.probe = ad5360_probe,
	.remove = ad5360_remove,
	.id_table = ad5360_ids,
};
module_spi_driver(ad5360_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5360/61/62/63/70/71/72/73 DAC");
MODULE_LICENSE("GPL v2");
