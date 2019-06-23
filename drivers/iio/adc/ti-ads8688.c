// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Prevas A/S
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/sysfs.h>

#define ADS8688_CMD_REG(x)		(x << 8)
#define ADS8688_CMD_REG_NOOP		0x00
#define ADS8688_CMD_REG_RST		0x85
#define ADS8688_CMD_REG_MAN_CH(chan)	(0xC0 | (4 * chan))
#define ADS8688_CMD_DONT_CARE_BITS	16

#define ADS8688_PROG_REG(x)		(x << 9)
#define ADS8688_PROG_REG_RANGE_CH(chan)	(0x05 + chan)
#define ADS8688_PROG_WR_BIT		BIT(8)
#define ADS8688_PROG_DONT_CARE_BITS	8

#define ADS8688_REG_PLUSMINUS25VREF	0
#define ADS8688_REG_PLUSMINUS125VREF	1
#define ADS8688_REG_PLUSMINUS0625VREF	2
#define ADS8688_REG_PLUS25VREF		5
#define ADS8688_REG_PLUS125VREF		6

#define ADS8688_VREF_MV			4096
#define ADS8688_REALBITS		16
#define ADS8688_MAX_CHANNELS		8

/*
 * enum ads8688_range - ADS8688 reference voltage range
 * @ADS8688_PLUSMINUS25VREF: Device is configured for input range ±2.5 * VREF
 * @ADS8688_PLUSMINUS125VREF: Device is configured for input range ±1.25 * VREF
 * @ADS8688_PLUSMINUS0625VREF: Device is configured for input range ±0.625 * VREF
 * @ADS8688_PLUS25VREF: Device is configured for input range 0 - 2.5 * VREF
 * @ADS8688_PLUS125VREF: Device is configured for input range 0 - 1.25 * VREF
 */
enum ads8688_range {
	ADS8688_PLUSMINUS25VREF,
	ADS8688_PLUSMINUS125VREF,
	ADS8688_PLUSMINUS0625VREF,
	ADS8688_PLUS25VREF,
	ADS8688_PLUS125VREF,
};

struct ads8688_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

struct ads8688_state {
	struct mutex			lock;
	const struct ads8688_chip_info	*chip_info;
	struct spi_device		*spi;
	struct regulator		*reg;
	unsigned int			vref_mv;
	enum ads8688_range		range[8];
	union {
		__be32 d32;
		u8 d8[4];
	} data[2] ____cacheline_aligned;
};

enum ads8688_id {
	ID_ADS8684,
	ID_ADS8688,
};

struct ads8688_ranges {
	enum ads8688_range range;
	unsigned int scale;
	int offset;
	u8 reg;
};

static const struct ads8688_ranges ads8688_range_def[5] = {
	{
		.range = ADS8688_PLUSMINUS25VREF,
		.scale = 76295,
		.offset = -(1 << (ADS8688_REALBITS - 1)),
		.reg = ADS8688_REG_PLUSMINUS25VREF,
	}, {
		.range = ADS8688_PLUSMINUS125VREF,
		.scale = 38148,
		.offset = -(1 << (ADS8688_REALBITS - 1)),
		.reg = ADS8688_REG_PLUSMINUS125VREF,
	}, {
		.range = ADS8688_PLUSMINUS0625VREF,
		.scale = 19074,
		.offset = -(1 << (ADS8688_REALBITS - 1)),
		.reg = ADS8688_REG_PLUSMINUS0625VREF,
	}, {
		.range = ADS8688_PLUS25VREF,
		.scale = 38148,
		.offset = 0,
		.reg = ADS8688_REG_PLUS25VREF,
	}, {
		.range = ADS8688_PLUS125VREF,
		.scale = 19074,
		.offset = 0,
		.reg = ADS8688_REG_PLUS125VREF,
	}
};

static ssize_t ads8688_show_scales(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ads8688_state *st = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "0.%09u 0.%09u 0.%09u\n",
		       ads8688_range_def[0].scale * st->vref_mv,
		       ads8688_range_def[1].scale * st->vref_mv,
		       ads8688_range_def[2].scale * st->vref_mv);
}

static ssize_t ads8688_show_offsets(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d\n", ads8688_range_def[0].offset,
		       ads8688_range_def[3].offset);
}

static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO,
		       ads8688_show_scales, NULL, 0);
static IIO_DEVICE_ATTR(in_voltage_offset_available, S_IRUGO,
		       ads8688_show_offsets, NULL, 0);

static struct attribute *ads8688_attributes[] = {
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_offset_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ads8688_attribute_group = {
	.attrs = ads8688_attributes,
};

#define ADS8688_CHAN(index)					\
{								\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = index,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)		\
			      | BIT(IIO_CHAN_INFO_SCALE)	\
			      | BIT(IIO_CHAN_INFO_OFFSET),	\
	.scan_index = index,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_BE,				\
	},							\
}

static const struct iio_chan_spec ads8684_channels[] = {
	ADS8688_CHAN(0),
	ADS8688_CHAN(1),
	ADS8688_CHAN(2),
	ADS8688_CHAN(3),
};

static const struct iio_chan_spec ads8688_channels[] = {
	ADS8688_CHAN(0),
	ADS8688_CHAN(1),
	ADS8688_CHAN(2),
	ADS8688_CHAN(3),
	ADS8688_CHAN(4),
	ADS8688_CHAN(5),
	ADS8688_CHAN(6),
	ADS8688_CHAN(7),
};

static int ads8688_prog_write(struct iio_dev *indio_dev, unsigned int addr,
			      unsigned int val)
{
	struct ads8688_state *st = iio_priv(indio_dev);
	u32 tmp;

	tmp = ADS8688_PROG_REG(addr) | ADS8688_PROG_WR_BIT | val;
	tmp <<= ADS8688_PROG_DONT_CARE_BITS;
	st->data[0].d32 = cpu_to_be32(tmp);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ads8688_reset(struct iio_dev *indio_dev)
{
	struct ads8688_state *st = iio_priv(indio_dev);
	u32 tmp;

	tmp = ADS8688_CMD_REG(ADS8688_CMD_REG_RST);
	tmp <<= ADS8688_CMD_DONT_CARE_BITS;
	st->data[0].d32 = cpu_to_be32(tmp);

	return spi_write(st->spi, &st->data[0].d8[0], 4);
}

static int ads8688_read(struct iio_dev *indio_dev, unsigned int chan)
{
	struct ads8688_state *st = iio_priv(indio_dev);
	int ret;
	u32 tmp;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[0],
			.len = 4,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1].d8[0],
			.rx_buf = &st->data[1].d8[0],
			.len = 4,
		},
	};

	tmp = ADS8688_CMD_REG(ADS8688_CMD_REG_MAN_CH(chan));
	tmp <<= ADS8688_CMD_DONT_CARE_BITS;
	st->data[0].d32 = cpu_to_be32(tmp);

	tmp = ADS8688_CMD_REG(ADS8688_CMD_REG_NOOP);
	tmp <<= ADS8688_CMD_DONT_CARE_BITS;
	st->data[1].d32 = cpu_to_be32(tmp);

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret < 0)
		return ret;

	return be32_to_cpu(st->data[1].d32) & 0xffff;
}

static int ads8688_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long m)
{
	int ret, offset;
	unsigned long scale_mv;

	struct ads8688_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ads8688_read(indio_dev, chan->channel);
		mutex_unlock(&st->lock);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_mv = st->vref_mv;
		scale_mv *= ads8688_range_def[st->range[chan->channel]].scale;
		*val = 0;
		*val2 = scale_mv;
		mutex_unlock(&st->lock);
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		offset = ads8688_range_def[st->range[chan->channel]].offset;
		*val = offset;
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;
	}
	mutex_unlock(&st->lock);

	return -EINVAL;
}

static int ads8688_write_reg_range(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   enum ads8688_range range)
{
	unsigned int tmp;
	int ret;

	tmp = ADS8688_PROG_REG_RANGE_CH(chan->channel);
	ret = ads8688_prog_write(indio_dev, tmp, range);

	return ret;
}

static int ads8688_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct ads8688_state *st = iio_priv(indio_dev);
	unsigned int scale = 0;
	int ret = -EINVAL, i, offset = 0;

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		/* If the offset is 0 the ±2.5 * VREF mode is not available */
		offset = ads8688_range_def[st->range[chan->channel]].offset;
		if (offset == 0 && val2 == ads8688_range_def[0].scale * st->vref_mv) {
			mutex_unlock(&st->lock);
			return -EINVAL;
		}

		/* Lookup new mode */
		for (i = 0; i < ARRAY_SIZE(ads8688_range_def); i++)
			if (val2 == ads8688_range_def[i].scale * st->vref_mv &&
			    offset == ads8688_range_def[i].offset) {
				ret = ads8688_write_reg_range(indio_dev, chan,
					ads8688_range_def[i].reg);
				break;
			}
		break;
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * There are only two available offsets:
		 * 0 and -(1 << (ADS8688_REALBITS - 1))
		 */
		if (!(ads8688_range_def[0].offset == val ||
		    ads8688_range_def[3].offset == val)) {
			mutex_unlock(&st->lock);
			return -EINVAL;
		}

		/*
		 * If the device are in ±2.5 * VREF mode, it's not allowed to
		 * switch to a mode where the offset is 0
		 */
		if (val == 0 &&
		    st->range[chan->channel] == ADS8688_PLUSMINUS25VREF) {
			mutex_unlock(&st->lock);
			return -EINVAL;
		}

		scale = ads8688_range_def[st->range[chan->channel]].scale;

		/* Lookup new mode */
		for (i = 0; i < ARRAY_SIZE(ads8688_range_def); i++)
			if (val == ads8688_range_def[i].offset &&
			    scale == ads8688_range_def[i].scale) {
				ret = ads8688_write_reg_range(indio_dev, chan,
					ads8688_range_def[i].reg);
				break;
			}
		break;
	}

	if (!ret)
		st->range[chan->channel] = ads8688_range_def[i].range;

	mutex_unlock(&st->lock);

	return ret;
}

static int ads8688_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info ads8688_info = {
	.read_raw = &ads8688_read_raw,
	.write_raw = &ads8688_write_raw,
	.write_raw_get_fmt = &ads8688_write_raw_get_fmt,
	.attrs = &ads8688_attribute_group,
};

static irqreturn_t ads8688_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	u16 buffer[ADS8688_MAX_CHANNELS + sizeof(s64)/sizeof(u16)];
	int i, j = 0;

	for (i = 0; i < indio_dev->masklength; i++) {
		if (!test_bit(i, indio_dev->active_scan_mask))
			continue;
		buffer[j] = ads8688_read(indio_dev, i);
		j++;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
			iio_get_time_ns(indio_dev));

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct ads8688_chip_info ads8688_chip_info_tbl[] = {
	[ID_ADS8684] = {
		.channels = ads8684_channels,
		.num_channels = ARRAY_SIZE(ads8684_channels),
	},
	[ID_ADS8688] = {
		.channels = ads8688_channels,
		.num_channels = ARRAY_SIZE(ads8688_channels),
	},
};

static int ads8688_probe(struct spi_device *spi)
{
	struct ads8688_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get_optional(&spi->dev, "vref");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(st->reg);
		if (ret < 0)
			goto err_regulator_disable;

		st->vref_mv = ret / 1000;
	} else {
		/* Use internal reference */
		st->vref_mv = ADS8688_VREF_MV;
	}

	st->chip_info =	&ads8688_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi->mode = SPI_MODE_1;

	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->info = &ads8688_info;

	ads8688_reset(indio_dev);

	mutex_init(&st->lock);

	ret = iio_triggered_buffer_setup(indio_dev, NULL, ads8688_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&spi->dev, "iio triggered buffer setup failed\n");
		goto err_regulator_disable;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_buffer_cleanup;

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);

err_regulator_disable:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return ret;
}

static int ads8688_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ads8688_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ads8688_id[] = {
	{"ads8684", ID_ADS8684},
	{"ads8688", ID_ADS8688},
	{}
};
MODULE_DEVICE_TABLE(spi, ads8688_id);

static const struct of_device_id ads8688_of_match[] = {
	{ .compatible = "ti,ads8684" },
	{ .compatible = "ti,ads8688" },
	{ }
};
MODULE_DEVICE_TABLE(of, ads8688_of_match);

static struct spi_driver ads8688_driver = {
	.driver = {
		.name	= "ads8688",
	},
	.probe		= ads8688_probe,
	.remove		= ads8688_remove,
	.id_table	= ads8688_id,
};
module_spi_driver(ads8688_driver);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.dk>");
MODULE_DESCRIPTION("Texas Instruments ADS8688 driver");
MODULE_LICENSE("GPL v2");
