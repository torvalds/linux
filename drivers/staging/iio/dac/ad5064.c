/*
 * AD5064, AD5064-1, AD5044, AD5024 Digital to analog converters  driver
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

#include "../iio.h"
#include "../sysfs.h"
#include "dac.h"

#define AD5064_DAC_CHANNELS			4

#define AD5064_ADDR(x)				((x) << 20)
#define AD5064_CMD(x)				((x) << 24)

#define AD5064_ADDR_DAC(chan)			(chan)
#define AD5064_ADDR_ALL_DAC			0xF

#define AD5064_CMD_WRITE_INPUT_N		0x0
#define AD5064_CMD_UPDATE_DAC_N			0x1
#define AD5064_CMD_WRITE_INPUT_N_UPDATE_ALL	0x2
#define AD5064_CMD_WRITE_INPUT_N_UPDATE_N	0x3
#define AD5064_CMD_POWERDOWN_DAC		0x4
#define AD5064_CMD_CLEAR			0x5
#define AD5064_CMD_LDAC_MASK			0x6
#define AD5064_CMD_RESET			0x7
#define AD5064_CMD_DAISY_CHAIN_ENABLE		0x8

#define AD5064_LDAC_PWRDN_NONE			0x0
#define AD5064_LDAC_PWRDN_1K			0x1
#define AD5064_LDAC_PWRDN_100K			0x2
#define AD5064_LDAC_PWRDN_3STATE		0x3

/**
 * struct ad5064_chip_info - chip specific information
 * @shared_vref:	whether the vref supply is shared between channels
 * @channel:		channel specification
*/

struct ad5064_chip_info {
	bool shared_vref;
	struct iio_chan_spec channel[AD5064_DAC_CHANNELS];
};

/**
 * struct ad5064_state - driver instance specific data
 * @spi:		spi_device
 * @chip_info:		chip model specific constants, available modes etc
 * @vref_reg:		vref supply regulators
 * @pwr_down:		whether channel is powered down
 * @pwr_down_mode:	channel's current power down mode
 * @dac_cache:		current DAC raw value (chip does not support readback)
 * @data:		spi transfer buffers
 */

struct ad5064_state {
	struct spi_device		*spi;
	const struct ad5064_chip_info	*chip_info;
	struct regulator_bulk_data	vref_reg[AD5064_DAC_CHANNELS];
	bool				pwr_down[AD5064_DAC_CHANNELS];
	u8				pwr_down_mode[AD5064_DAC_CHANNELS];
	unsigned int			dac_cache[AD5064_DAC_CHANNELS];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be32 data ____cacheline_aligned;
};

enum ad5064_type {
	ID_AD5024,
	ID_AD5044,
	ID_AD5064,
	ID_AD5064_1,
};

#define AD5064_CHANNEL(chan, bits) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (chan),					\
	.info_mask = (1 << IIO_CHAN_INFO_SCALE_SEPARATE),	\
	.address = AD5064_ADDR_DAC(chan),			\
	.scan_type = IIO_ST('u', (bits), 16, 20 - (bits))	\
}

static const struct ad5064_chip_info ad5064_chip_info_tbl[] = {
	[ID_AD5024] = {
		.shared_vref = false,
		.channel[0] = AD5064_CHANNEL(0, 12),
		.channel[1] = AD5064_CHANNEL(1, 12),
		.channel[2] = AD5064_CHANNEL(2, 12),
		.channel[3] = AD5064_CHANNEL(3, 12),
	},
	[ID_AD5044] = {
		.shared_vref = false,
		.channel[0] = AD5064_CHANNEL(0, 14),
		.channel[1] = AD5064_CHANNEL(1, 14),
		.channel[2] = AD5064_CHANNEL(2, 14),
		.channel[3] = AD5064_CHANNEL(3, 14),
	},
	[ID_AD5064] = {
		.shared_vref = false,
		.channel[0] = AD5064_CHANNEL(0, 16),
		.channel[1] = AD5064_CHANNEL(1, 16),
		.channel[2] = AD5064_CHANNEL(2, 16),
		.channel[3] = AD5064_CHANNEL(3, 16),
	},
	[ID_AD5064_1] = {
		.shared_vref = true,
		.channel[0] = AD5064_CHANNEL(0, 16),
		.channel[1] = AD5064_CHANNEL(1, 16),
		.channel[2] = AD5064_CHANNEL(2, 16),
		.channel[3] = AD5064_CHANNEL(3, 16),
	},
};

static int ad5064_spi_write(struct ad5064_state *st, unsigned int cmd,
	unsigned int addr, unsigned int val, unsigned int shift)
{
	val <<= shift;

	st->data = cpu_to_be32(AD5064_CMD(cmd) | AD5064_ADDR(addr) | val);

	return spi_write(st->spi, &st->data, sizeof(st->data));
}

static int ad5064_sync_powerdown_mode(struct ad5064_state *st,
	unsigned int channel)
{
	unsigned int val;
	int ret;

	val = (0x1 << channel);

	if (st->pwr_down[channel])
		val |= st->pwr_down_mode[channel] << 8;

	ret = ad5064_spi_write(st, AD5064_CMD_POWERDOWN_DAC, 0, val, 0);

	return ret;
}

static const char ad5064_powerdown_modes[][15] = {
	[AD5064_LDAC_PWRDN_NONE]	= "",
	[AD5064_LDAC_PWRDN_1K]		= "1kohm_to_gnd",
	[AD5064_LDAC_PWRDN_100K]	= "100kohm_to_gnd",
	[AD5064_LDAC_PWRDN_3STATE]	= "three_state",
};

static ssize_t ad5064_read_powerdown_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5064_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%s\n",
		ad5064_powerdown_modes[st->pwr_down_mode[this_attr->address]]);
}

static ssize_t ad5064_write_powerdown_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5064_state *st = iio_priv(indio_dev);
	unsigned int mode, i;
	int ret;

	mode = 0;

	for (i = 1; i < ARRAY_SIZE(ad5064_powerdown_modes); ++i) {
		if (sysfs_streq(buf, ad5064_powerdown_modes[i])) {
			mode = i;
			break;
		}
	}
	if (mode == 0)
		return  -EINVAL;

	mutex_lock(&indio_dev->mlock);
	st->pwr_down_mode[this_attr->address] = mode;

	ret = ad5064_sync_powerdown_mode(st, this_attr->address);
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static ssize_t ad5064_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5064_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	return sprintf(buf, "%d\n", st->pwr_down[this_attr->address]);
}

static ssize_t ad5064_write_dac_powerdown(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5064_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	bool pwr_down;
	int ret;

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	st->pwr_down[this_attr->address] = pwr_down;

	ret = ad5064_sync_powerdown_mode(st, this_attr->address);
	mutex_unlock(&indio_dev->mlock);
	return ret ? ret : len;
}

static IIO_CONST_ATTR(out_voltage_powerdown_mode_available,
			"1kohm_to_gnd 100kohm_to_gnd three_state");

#define IIO_DEV_ATTR_DAC_POWERDOWN_MODE(_chan) \
	IIO_DEVICE_ATTR(out_voltage##_chan##_powerdown_mode, \
			S_IRUGO | S_IWUSR, \
			ad5064_read_powerdown_mode, \
			ad5064_write_powerdown_mode, _chan);

#define IIO_DEV_ATTR_DAC_POWERDOWN(_chan)				\
	IIO_DEVICE_ATTR(out_voltage##_chan##_powerdown,			\
			S_IRUGO | S_IWUSR,				\
			ad5064_read_dac_powerdown,			\
			ad5064_write_dac_powerdown, _chan)

static IIO_DEV_ATTR_DAC_POWERDOWN(0);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(0);
static IIO_DEV_ATTR_DAC_POWERDOWN(1);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(1);
static IIO_DEV_ATTR_DAC_POWERDOWN(2);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(2);
static IIO_DEV_ATTR_DAC_POWERDOWN(3);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(3);

static struct attribute *ad5064_attributes[] = {
	&iio_dev_attr_out_voltage0_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage1_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage2_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage3_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage0_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out_voltage1_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out_voltage2_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out_voltage3_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_voltage_powerdown_mode_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5064_attribute_group = {
	.attrs = ad5064_attributes,
};

static int ad5064_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5064_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;
	unsigned int vref;

	switch (m) {
	case 0:
		*val = st->dac_cache[chan->channel];
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_SCALE_SEPARATE):
		vref = st->chip_info->shared_vref ? 0 : chan->channel;
		scale_uv = regulator_get_voltage(st->vref_reg[vref].consumer);
		if (scale_uv < 0)
			return scale_uv;

		scale_uv = (scale_uv * 100) >> chan->scan_type.realbits;
		*val =  scale_uv / 100000;
		*val2 = (scale_uv % 100000) * 10;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		break;
	}
	return -EINVAL;
}

static int ad5064_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ad5064_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case 0:
		if (val > (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		mutex_lock(&indio_dev->mlock);
		ret = ad5064_spi_write(st, AD5064_CMD_WRITE_INPUT_N_UPDATE_N,
				chan->address, val, chan->scan_type.shift);
		if (ret == 0)
			st->dac_cache[chan->channel] = val;
		mutex_unlock(&indio_dev->mlock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5064_info = {
	.read_raw = ad5064_read_raw,
	.write_raw = ad5064_write_raw,
	.attrs = &ad5064_attribute_group,
	.driver_module = THIS_MODULE,
};

static inline unsigned int ad5064_num_vref(struct ad5064_state *st)
{
	return st->chip_info->shared_vref ? 1 : AD5064_DAC_CHANNELS;
}

static const char * const ad5064_vref_names[] = {
	"vrefA",
	"vrefB",
	"vrefC",
	"vrefD",
};

static const char * const ad5064_vref_name(struct ad5064_state *st,
	unsigned int vref)
{
	return st->chip_info->shared_vref ? "vref" : ad5064_vref_names[vref];
}

static int __devinit ad5064_probe(struct spi_device *spi)
{
	enum ad5064_type type = spi_get_device_id(spi)->driver_data;
	struct iio_dev *indio_dev;
	struct ad5064_state *st;
	unsigned int i;
	int ret;

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL)
		return  -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->chip_info = &ad5064_chip_info_tbl[type];
	st->spi = spi;

	for (i = 0; i < ad5064_num_vref(st); ++i)
		st->vref_reg[i].supply = ad5064_vref_name(st, i);

	ret = regulator_bulk_get(&st->spi->dev, ad5064_num_vref(st),
		st->vref_reg);
	if (ret)
		goto error_free;

	ret = regulator_bulk_enable(ad5064_num_vref(st), st->vref_reg);
	if (ret)
		goto error_free_reg;

	for (i = 0; i < AD5064_DAC_CHANNELS; ++i) {
		st->pwr_down_mode[i] = AD5064_LDAC_PWRDN_1K;
		st->dac_cache[i] = 0x8000;
	}

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5064_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = AD5064_DAC_CHANNELS;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	regulator_bulk_disable(ad5064_num_vref(st), st->vref_reg);
error_free_reg:
	regulator_bulk_free(ad5064_num_vref(st), st->vref_reg);
error_free:
	iio_free_device(indio_dev);

	return ret;
}


static int __devexit ad5064_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5064_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	regulator_bulk_disable(ad5064_num_vref(st), st->vref_reg);
	regulator_bulk_free(ad5064_num_vref(st), st->vref_reg);

	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id ad5064_id[] = {
	{"ad5024", ID_AD5024},
	{"ad5044", ID_AD5044},
	{"ad5064", ID_AD5064},
	{"ad5064-1", ID_AD5064_1},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5064_id);

static struct spi_driver ad5064_driver = {
	.driver = {
		   .name = "ad5064",
		   .owner = THIS_MODULE,
	},
	.probe = ad5064_probe,
	.remove = __devexit_p(ad5064_remove),
	.id_table = ad5064_id,
};

static __init int ad5064_spi_init(void)
{
	return spi_register_driver(&ad5064_driver);
}
module_init(ad5064_spi_init);

static __exit void ad5064_spi_exit(void)
{
	spi_unregister_driver(&ad5064_driver);
}
module_exit(ad5064_spi_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5064/64-1/44/24 DAC");
MODULE_LICENSE("GPL v2");
