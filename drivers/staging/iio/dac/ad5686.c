/*
 * AD5686R, AD5685R, AD5684R Digital to analog converters  driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include "../iio.h"
#include "../sysfs.h"
#include "dac.h"

#define AD5686_DAC_CHANNELS			4

#define AD5686_ADDR(x)				((x) << 16)
#define AD5686_CMD(x)				((x) << 20)

#define AD5686_ADDR_DAC0			0x1
#define AD5686_ADDR_DAC1			0x2
#define AD5686_ADDR_DAC2			0x4
#define AD5686_ADDR_DAC3			0x8
#define AD5686_ADDR_ALL_DAC			0xF

#define AD5686_CMD_NOOP				0x0
#define AD5686_CMD_WRITE_INPUT_N		0x1
#define AD5686_CMD_UPDATE_DAC_N			0x2
#define AD5686_CMD_WRITE_INPUT_N_UPDATE_N	0x3
#define AD5686_CMD_POWERDOWN_DAC		0x4
#define AD5686_CMD_LDAC_MASK			0x5
#define AD5686_CMD_RESET			0x6
#define AD5686_CMD_INTERNAL_REFER_SETUP		0x7
#define AD5686_CMD_DAISY_CHAIN_ENABLE		0x8
#define AD5686_CMD_READBACK_ENABLE		0x9

#define AD5686_LDAC_PWRDN_NONE			0x0
#define AD5686_LDAC_PWRDN_1K			0x1
#define AD5686_LDAC_PWRDN_100K			0x2
#define AD5686_LDAC_PWRDN_3STATE		0x3

/**
 * struct ad5686_chip_info - chip specific information
 * @int_vref_mv:	AD5620/40/60: the internal reference voltage
 * @channel:		channel specification
*/

struct ad5686_chip_info {
	u16				int_vref_mv;
	struct iio_chan_spec		channel[AD5686_DAC_CHANNELS];
};

/**
 * struct ad5446_state - driver instance specific data
 * @spi:		spi_device
 * @chip_info:		chip model specific constants, available modes etc
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 * @pwr_down_mask:	power down mask
 * @pwr_down_mode:	current power down mode
 * @data:		spi transfer buffers
 */

struct ad5686_state {
	struct spi_device		*spi;
	const struct ad5686_chip_info	*chip_info;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned			pwr_down_mask;
	unsigned			pwr_down_mode;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */

	union {
		u32 d32;
		u8 d8[4];
	} data[3] ____cacheline_aligned;
};

/**
 * ad5686_supported_device_ids:
 */

enum ad5686_supported_device_ids {
	ID_AD5684,
	ID_AD5685,
	ID_AD5686,
};

static const struct ad5686_chip_info ad5686_chip_info_tbl[] = {
	[ID_AD5684] = {
		.channel[0] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 0, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC0,
				    0, IIO_ST('u', 12, 16, 4), 0),
		.channel[1] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 1, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC1,
				    1, IIO_ST('u', 12, 16, 4), 0),
		.channel[2] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 2, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC2,
				    2, IIO_ST('u', 12, 16, 4), 0),
		.channel[3] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 3, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC3,
				    3, IIO_ST('u', 12, 16, 4), 0),
		.int_vref_mv = 2500,
	},
	[ID_AD5685] = {
		.channel[0] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 0, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC0,
				    0, IIO_ST('u', 14, 16, 2), 0),
		.channel[1] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 1, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC1,
				    1, IIO_ST('u', 14, 16, 2), 0),
		.channel[2] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 2, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC2,
				    2, IIO_ST('u', 14, 16, 2), 0),
		.channel[3] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 3, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC3,
				    3, IIO_ST('u', 14, 16, 2), 0),
		.int_vref_mv = 2500,
	},
	[ID_AD5686] = {
		.channel[0] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 0, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC0,
				    0, IIO_ST('u', 16, 16, 0), 0),
		.channel[1] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 1, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC1,
				    1, IIO_ST('u', 16, 16, 0), 0),
		.channel[2] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 2, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC2,
				    2, IIO_ST('u', 16, 16, 0), 0),
		.channel[3] = IIO_CHAN(IIO_OUT, 0, 1, 0, NULL, 3, 0,
				    (1 << IIO_CHAN_INFO_SCALE_SHARED),
				    AD5686_ADDR_DAC3,
				    3, IIO_ST('u', 16, 16, 0), 0),
		.int_vref_mv = 2500,
	},
};

static int ad5686_spi_write(struct ad5686_state *st,
			     u8 cmd, u8 addr, u16 val, u8 shift)
{
	val <<= shift;

	st->data[0].d32 = cpu_to_be32(AD5686_CMD(cmd) |
			      AD5686_ADDR(addr) |
			      val);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5686_spi_read(struct ad5686_state *st, u8 addr)
{
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.len = 3,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1].d8[1],
			.rx_buf = &st->data[2].d8[1],
			.len = 3,
		},
	};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	st->data[0].d32 = cpu_to_be32(AD5686_CMD(AD5686_CMD_READBACK_ENABLE) |
			      AD5686_ADDR(addr));
	st->data[1].d32 = cpu_to_be32(AD5686_CMD(AD5686_CMD_NOOP));

	ret = spi_sync(st->spi, &m);
	if (ret < 0)
		return ret;

	return be32_to_cpu(st->data[2].d32);
}

static ssize_t ad5686_read_powerdown_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5686_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	char mode[][15] = {"", "1kohm_to_gnd", "100kohm_to_gnd", "three_state"};

	return sprintf(buf, "%s\n", mode[(st->pwr_down_mode >>
					 (this_attr->address * 2)) & 0x3]);
}

static ssize_t ad5686_write_powerdown_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5686_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	unsigned mode;

	if (sysfs_streq(buf, "1kohm_to_gnd"))
		mode = AD5686_LDAC_PWRDN_1K;
	else if (sysfs_streq(buf, "100kohm_to_gnd"))
		mode = AD5686_LDAC_PWRDN_100K;
	else if (sysfs_streq(buf, "three_state"))
		mode = AD5686_LDAC_PWRDN_3STATE;
	else
		return  -EINVAL;

	st->pwr_down_mode &= ~(0x3 << (this_attr->address * 2));
	st->pwr_down_mode |= (mode << (this_attr->address * 2));

	return len;
}

static ssize_t ad5686_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5686_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	return sprintf(buf, "%d\n", !!(st->pwr_down_mask &
			(0x3 << (this_attr->address * 2))));
}

static ssize_t ad5686_write_dac_powerdown(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	bool readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5686_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strtobool(buf, &readin);
	if (ret)
		return ret;

	if (readin == true)
		st->pwr_down_mask |= (0x3 << (this_attr->address * 2));
	else
		st->pwr_down_mask &= ~(0x3 << (this_attr->address * 2));

	ret = ad5686_spi_write(st, AD5686_CMD_POWERDOWN_DAC, 0,
			       st->pwr_down_mask & st->pwr_down_mode, 0);

	return ret ? ret : len;
}

static IIO_CONST_ATTR(out_powerdown_mode_available,
			"1kohm_to_gnd 100kohm_to_gnd three_state");

#define IIO_DEV_ATTR_DAC_POWERDOWN_MODE(_num) \
	IIO_DEVICE_ATTR(out##_num##_powerdown_mode, S_IRUGO | S_IWUSR,	\
			ad5686_read_powerdown_mode,			\
			ad5686_write_powerdown_mode, _num)

static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(0);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(1);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(2);
static IIO_DEV_ATTR_DAC_POWERDOWN_MODE(3);

#define IIO_DEV_ATTR_DAC_POWERDOWN(_num)	\
	IIO_DEVICE_ATTR(out##_num##_powerdown, S_IRUGO | S_IWUSR,	\
			ad5686_read_dac_powerdown,			\
			ad5686_write_dac_powerdown, _num)

static IIO_DEV_ATTR_DAC_POWERDOWN(0);
static IIO_DEV_ATTR_DAC_POWERDOWN(1);
static IIO_DEV_ATTR_DAC_POWERDOWN(2);
static IIO_DEV_ATTR_DAC_POWERDOWN(3);

static struct attribute *ad5686_attributes[] = {
	&iio_dev_attr_out0_powerdown.dev_attr.attr,
	&iio_dev_attr_out1_powerdown.dev_attr.attr,
	&iio_dev_attr_out2_powerdown.dev_attr.attr,
	&iio_dev_attr_out3_powerdown.dev_attr.attr,
	&iio_dev_attr_out0_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out1_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out2_powerdown_mode.dev_attr.attr,
	&iio_dev_attr_out3_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_powerdown_mode_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5686_attribute_group = {
	.attrs = ad5686_attributes,
};

static int ad5686_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5686_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;
	int ret;

	switch (m) {
	case 0:
		mutex_lock(&indio_dev->mlock);
		ret = ad5686_spi_read(st, chan->address);
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
		break;
	case (1 << IIO_CHAN_INFO_SCALE_SHARED):
		scale_uv = (st->vref_mv * 100000)
			>> (chan->scan_type.realbits);
		*val =  scale_uv / 100000;
		*val2 = (scale_uv % 100000) * 10;
		return IIO_VAL_INT_PLUS_MICRO;

	}
	return -EINVAL;
}

static int ad5686_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5686_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case 0:
		if (val > (1 << chan->scan_type.realbits))
			return -EINVAL;

		mutex_lock(&indio_dev->mlock);
		ret = ad5686_spi_write(st,
				 AD5686_CMD_WRITE_INPUT_N_UPDATE_N,
				 chan->address,
				 val,
				 chan->scan_type.shift);
		mutex_unlock(&indio_dev->mlock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5686_info = {
	.read_raw = ad5686_read_raw,
	.write_raw = ad5686_write_raw,
	.attrs = &ad5686_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5686_probe(struct spi_device *spi)
{
	struct ad5686_state *st;
	struct iio_dev *indio_dev;
	int ret, regdone = 0, voltage_uv = 0;

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL)
		return  -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	st->chip_info =
		&ad5686_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else
		st->vref_mv = st->chip_info->int_vref_mv;

	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5686_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = AD5686_DAC_CHANNELS;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	regdone = 1;
	ret = ad5686_spi_write(st, AD5686_CMD_INTERNAL_REFER_SETUP, 0,
				!!voltage_uv, 0);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);

	if (regdone)
		iio_device_unregister(indio_dev);
	else
		iio_free_device(indio_dev);

	return ret;
}

static int __devexit ad5686_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5686_state *st = iio_priv(indio_dev);
	struct regulator *reg = st->reg;

	if (!IS_ERR(reg)) {
		regulator_disable(reg);
		regulator_put(reg);
	}

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct spi_device_id ad5686_id[] = {
	{"ad5684", ID_AD5684},
	{"ad5685", ID_AD5685},
	{"ad5686", ID_AD5686},
	{}
};

static struct spi_driver ad5686_driver = {
	.driver = {
		   .name = "ad5686",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5686_probe,
	.remove = __devexit_p(ad5686_remove),
	.id_table = ad5686_id,
};

static __init int ad5686_spi_init(void)
{
	return spi_register_driver(&ad5686_driver);
}
module_init(ad5686_spi_init);

static __exit void ad5686_spi_exit(void)
{
	spi_unregister_driver(&ad5686_driver);
}
module_exit(ad5686_spi_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5686/85/84 DAC");
MODULE_LICENSE("GPL v2");
