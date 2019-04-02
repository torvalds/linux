// SPDX-License-Identifier: GPL-2.0
/*
 * AD9833/AD9834/AD9837/AD9838 SPI DDS driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <asm/div64.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "dds.h"

#include "ad9834.h"

/* Registers */

#define AD9834_REG_CMD		0
#define AD9834_REG_FREQ0	BIT(14)
#define AD9834_REG_FREQ1	BIT(15)
#define AD9834_REG_PHASE0	(BIT(15) | BIT(14))
#define AD9834_REG_PHASE1	(BIT(15) | BIT(14) | BIT(13))

/* Command Control Bits */

#define AD9834_B28		BIT(13)
#define AD9834_HLB		BIT(12)
#define AD9834_FSEL		BIT(11)
#define AD9834_PSEL		BIT(10)
#define AD9834_PIN_SW		BIT(9)
#define AD9834_RESET		BIT(8)
#define AD9834_SLEEP1		BIT(7)
#define AD9834_SLEEP12		BIT(6)
#define AD9834_OPBITEN		BIT(5)
#define AD9834_SIGN_PIB		BIT(4)
#define AD9834_DIV2		BIT(3)
#define AD9834_MODE		BIT(1)

#define AD9834_FREQ_BITS	28
#define AD9834_PHASE_BITS	12

#define RES_MASK(bits)	(BIT(bits) - 1)

/**
 * struct ad9834_state - driver instance specific data
 * @spi:		spi_device
 * @reg:		supply regulator
 * @mclk:		external master clock
 * @control:		cached control word
 * @xfer:		default spi transfer
 * @msg:		default spi message
 * @freq_xfer:		tuning word spi transfer
 * @freq_msg:		tuning word spi message
 * @lock:		protect sensor state
 * @data:		spi transmit buffer
 * @freq_data:		tuning word spi transmit buffer
 */

struct ad9834_state {
	struct spi_device		*spi;
	struct regulator		*reg;
	struct clk			*mclk;
	unsigned short			control;
	unsigned short			devid;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	struct spi_transfer		freq_xfer[2];
	struct spi_message		freq_msg;
	struct mutex                    lock;   /* protect sensor state */

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be16				data ____cacheline_aligned;
	__be16				freq_data[2];
};

/**
 * ad9834_supported_device_ids:
 */

enum ad9834_supported_device_ids {
	ID_AD9833,
	ID_AD9834,
	ID_AD9837,
	ID_AD9838,
};

static unsigned int ad9834_calc_freqreg(unsigned long mclk, unsigned long fout)
{
	unsigned long long freqreg = (u64)fout * (u64)BIT(AD9834_FREQ_BITS);

	do_div(freqreg, mclk);
	return freqreg;
}

static int ad9834_write_frequency(struct ad9834_state *st,
				  unsigned long addr, unsigned long fout)
{
	unsigned long clk_freq;
	unsigned long regval;

	clk_freq = clk_get_rate(st->mclk);

	if (fout > (clk_freq / 2))
		return -EINVAL;

	regval = ad9834_calc_freqreg(clk_freq, fout);

	st->freq_data[0] = cpu_to_be16(addr | (regval &
				       RES_MASK(AD9834_FREQ_BITS / 2)));
	st->freq_data[1] = cpu_to_be16(addr | ((regval >>
				       (AD9834_FREQ_BITS / 2)) &
				       RES_MASK(AD9834_FREQ_BITS / 2)));

	return spi_sync(st->spi, &st->freq_msg);
}

static int ad9834_write_phase(struct ad9834_state *st,
			      unsigned long addr, unsigned long phase)
{
	if (phase > BIT(AD9834_PHASE_BITS))
		return -EINVAL;
	st->data = cpu_to_be16(addr | phase);

	return spi_sync(st->spi, &st->msg);
}

static ssize_t ad9834_write(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9834_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	switch ((u32)this_attr->address) {
	case AD9834_REG_FREQ0:
	case AD9834_REG_FREQ1:
		ret = ad9834_write_frequency(st, this_attr->address, val);
		break;
	case AD9834_REG_PHASE0:
	case AD9834_REG_PHASE1:
		ret = ad9834_write_phase(st, this_attr->address, val);
		break;
	case AD9834_OPBITEN:
		if (st->control & AD9834_MODE) {
			ret = -EINVAL;  /* AD9843 reserved mode */
			break;
		}

		if (val)
			st->control |= AD9834_OPBITEN;
		else
			st->control &= ~AD9834_OPBITEN;

		st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9834_PIN_SW:
		if (val)
			st->control |= AD9834_PIN_SW;
		else
			st->control &= ~AD9834_PIN_SW;
		st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9834_FSEL:
	case AD9834_PSEL:
		if (!val) {
			st->control &= ~(this_attr->address | AD9834_PIN_SW);
		} else if (val == 1) {
			st->control |= this_attr->address;
			st->control &= ~AD9834_PIN_SW;
		} else {
			ret = -EINVAL;
			break;
		}
		st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9834_RESET:
		if (val)
			st->control &= ~AD9834_RESET;
		else
			st->control |= AD9834_RESET;

		st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
		ret = spi_sync(st->spi, &st->msg);
		break;
	default:
		ret = -ENODEV;
	}
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static ssize_t ad9834_store_wavetype(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9834_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret = 0;
	bool is_ad9833_7 = (st->devid == ID_AD9833) || (st->devid == ID_AD9837);

	mutex_lock(&st->lock);

	switch ((u32)this_attr->address) {
	case 0:
		if (sysfs_streq(buf, "sine")) {
			st->control &= ~AD9834_MODE;
			if (is_ad9833_7)
				st->control &= ~AD9834_OPBITEN;
		} else if (sysfs_streq(buf, "triangle")) {
			if (is_ad9833_7) {
				st->control &= ~AD9834_OPBITEN;
				st->control |= AD9834_MODE;
			} else if (st->control & AD9834_OPBITEN) {
				ret = -EINVAL;	/* AD9843 reserved mode */
			} else {
				st->control |= AD9834_MODE;
			}
		} else if (is_ad9833_7 && sysfs_streq(buf, "square")) {
			st->control &= ~AD9834_MODE;
			st->control |= AD9834_OPBITEN;
		} else {
			ret = -EINVAL;
		}

		break;
	case 1:
		if (sysfs_streq(buf, "square") &&
		    !(st->control & AD9834_MODE)) {
			st->control &= ~AD9834_MODE;
			st->control |= AD9834_OPBITEN;
		} else {
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
		ret = spi_sync(st->spi, &st->msg);
	}
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static
ssize_t ad9834_show_out0_wavetype_available(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9834_state *st = iio_priv(indio_dev);
	char *str;

	if ((st->devid == ID_AD9833) || (st->devid == ID_AD9837))
		str = "sine triangle square";
	else if (st->control & AD9834_OPBITEN)
		str = "sine";
	else
		str = "sine triangle";

	return sprintf(buf, "%s\n", str);
}

static IIO_DEVICE_ATTR(out_altvoltage0_out0_wavetype_available, 0444,
		       ad9834_show_out0_wavetype_available, NULL, 0);

static
ssize_t ad9834_show_out1_wavetype_available(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9834_state *st = iio_priv(indio_dev);
	char *str;

	if (st->control & AD9834_MODE)
		str = "";
	else
		str = "square";

	return sprintf(buf, "%s\n", str);
}

static IIO_DEVICE_ATTR(out_altvoltage0_out1_wavetype_available, 0444,
		       ad9834_show_out1_wavetype_available, NULL, 0);

/**
 * see dds.h for further information
 */

static IIO_DEV_ATTR_FREQ(0, 0, 0200, NULL, ad9834_write, AD9834_REG_FREQ0);
static IIO_DEV_ATTR_FREQ(0, 1, 0200, NULL, ad9834_write, AD9834_REG_FREQ1);
static IIO_DEV_ATTR_FREQSYMBOL(0, 0200, NULL, ad9834_write, AD9834_FSEL);
static IIO_CONST_ATTR_FREQ_SCALE(0, "1"); /* 1Hz */

static IIO_DEV_ATTR_PHASE(0, 0, 0200, NULL, ad9834_write, AD9834_REG_PHASE0);
static IIO_DEV_ATTR_PHASE(0, 1, 0200, NULL, ad9834_write, AD9834_REG_PHASE1);
static IIO_DEV_ATTR_PHASESYMBOL(0, 0200, NULL, ad9834_write, AD9834_PSEL);
static IIO_CONST_ATTR_PHASE_SCALE(0, "0.0015339808"); /* 2PI/2^12 rad*/

static IIO_DEV_ATTR_PINCONTROL_EN(0, 0200, NULL,
	ad9834_write, AD9834_PIN_SW);
static IIO_DEV_ATTR_OUT_ENABLE(0, 0200, NULL, ad9834_write, AD9834_RESET);
static IIO_DEV_ATTR_OUTY_ENABLE(0, 1, 0200, NULL,
	ad9834_write, AD9834_OPBITEN);
static IIO_DEV_ATTR_OUT_WAVETYPE(0, 0, ad9834_store_wavetype, 0);
static IIO_DEV_ATTR_OUT_WAVETYPE(0, 1, ad9834_store_wavetype, 1);

static struct attribute *ad9834_attributes[] = {
	&iio_dev_attr_out_altvoltage0_frequency0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequency1.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_frequency_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase1.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_phase_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_pincontrol_en.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequencysymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phasesymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out_enable.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out1_enable.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out0_wavetype.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out1_wavetype.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out0_wavetype_available.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out1_wavetype_available.dev_attr.attr,
	NULL,
};

static struct attribute *ad9833_attributes[] = {
	&iio_dev_attr_out_altvoltage0_frequency0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequency1.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_frequency_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase1.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_phase_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequencysymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phasesymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out_enable.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out0_wavetype.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out0_wavetype_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9834_attribute_group = {
	.attrs = ad9834_attributes,
};

static const struct attribute_group ad9833_attribute_group = {
	.attrs = ad9833_attributes,
};

static const struct iio_info ad9834_info = {
	.attrs = &ad9834_attribute_group,
};

static const struct iio_info ad9833_info = {
	.attrs = &ad9833_attribute_group,
};

static int ad9834_probe(struct spi_device *spi)
{
	struct ad9834_state *st;
	struct iio_dev *indio_dev;
	struct regulator *reg;
	int ret;


	reg = devm_regulator_get(&spi->dev, "avdd");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ret = regulator_enable(reg);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified AVDD supply\n");
		return ret;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	st->mclk = devm_clk_get(&spi->dev, NULL);

	ret = clk_prepare_enable(st->mclk);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable master clock\n");
		goto error_disable_reg;
	}

	st->spi = spi;
	st->devid = spi_get_device_id(spi)->driver_data;
	st->reg = reg;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	switch (st->devid) {
	case ID_AD9833:
	case ID_AD9837:
		indio_dev->info = &ad9833_info;
		break;
	default:
		indio_dev->info = &ad9834_info;
		break;
	}
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default messages */

	st->xfer.tx_buf = &st->data;
	st->xfer.len = 2;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	st->freq_xfer[0].tx_buf = &st->freq_data[0];
	st->freq_xfer[0].len = 2;
	st->freq_xfer[0].cs_change = 1;
	st->freq_xfer[1].tx_buf = &st->freq_data[1];
	st->freq_xfer[1].len = 2;

	spi_message_init(&st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[0], &st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[1], &st->freq_msg);

	st->control = AD9834_B28 | AD9834_RESET;
	st->control |= AD9834_DIV2;

	if (st->devid == ID_AD9834)
		st->control |= AD9834_SIGN_PIB;

	st->data = cpu_to_be16(AD9834_REG_CMD | st->control);
	ret = spi_sync(st->spi, &st->msg);
	if (ret) {
		dev_err(&spi->dev, "device init failed\n");
		goto error_clock_unprepare;
	}

	ret = ad9834_write_frequency(st, AD9834_REG_FREQ0, 1000000);
	if (ret)
		goto error_clock_unprepare;

	ret = ad9834_write_frequency(st, AD9834_REG_FREQ1, 5000000);
	if (ret)
		goto error_clock_unprepare;

	ret = ad9834_write_phase(st, AD9834_REG_PHASE0, 512);
	if (ret)
		goto error_clock_unprepare;

	ret = ad9834_write_phase(st, AD9834_REG_PHASE1, 1024);
	if (ret)
		goto error_clock_unprepare;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_clock_unprepare;

	return 0;
error_clock_unprepare:
	clk_disable_unprepare(st->mclk);
error_disable_reg:
	regulator_disable(reg);

	return ret;
}

static int ad9834_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad9834_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(st->mclk);
	regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad9834_id[] = {
	{"ad9833", ID_AD9833},
	{"ad9834", ID_AD9834},
	{"ad9837", ID_AD9837},
	{"ad9838", ID_AD9838},
	{}
};
MODULE_DEVICE_TABLE(spi, ad9834_id);

static struct spi_driver ad9834_driver = {
	.driver = {
		.name	= "ad9834",
	},
	.probe		= ad9834_probe,
	.remove		= ad9834_remove,
	.id_table	= ad9834_id,
};
module_spi_driver(ad9834_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9833/AD9834/AD9837/AD9838 DDS");
MODULE_LICENSE("GPL v2");
