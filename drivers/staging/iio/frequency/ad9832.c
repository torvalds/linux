// SPDX-License-Identifier: GPL-2.0
/*
 * AD9832 SPI DDS driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <asm/div64.h>

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ad9832.h"

#include "dds.h"

/* Registers */

#define AD9832_FREQ0LL		0x0
#define AD9832_FREQ0HL		0x1
#define AD9832_FREQ0LM		0x2
#define AD9832_FREQ0HM		0x3
#define AD9832_FREQ1LL		0x4
#define AD9832_FREQ1HL		0x5
#define AD9832_FREQ1LM		0x6
#define AD9832_FREQ1HM		0x7
#define AD9832_PHASE0L		0x8
#define AD9832_PHASE0H		0x9
#define AD9832_PHASE1L		0xA
#define AD9832_PHASE1H		0xB
#define AD9832_PHASE2L		0xC
#define AD9832_PHASE2H		0xD
#define AD9832_PHASE3L		0xE
#define AD9832_PHASE3H		0xF

#define AD9832_PHASE_SYM	0x10
#define AD9832_FREQ_SYM		0x11
#define AD9832_PINCTRL_EN	0x12
#define AD9832_OUTPUT_EN	0x13

/* Command Control Bits */

#define AD9832_CMD_PHA8BITSW	0x1
#define AD9832_CMD_PHA16BITSW	0x0
#define AD9832_CMD_FRE8BITSW	0x3
#define AD9832_CMD_FRE16BITSW	0x2
#define AD9832_CMD_FPSELECT	0x6
#define AD9832_CMD_SYNCSELSRC	0x8
#define AD9832_CMD_SLEEPRESCLR	0xC

#define AD9832_FREQ		BIT(11)
#define AD9832_PHASE(x)		(((x) & 3) << 9)
#define AD9832_SYNC		BIT(13)
#define AD9832_SELSRC		BIT(12)
#define AD9832_SLEEP		BIT(13)
#define AD9832_RESET		BIT(12)
#define AD9832_CLR		BIT(11)
#define CMD_SHIFT		12
#define ADD_SHIFT		8
#define AD9832_FREQ_BITS	32
#define AD9832_PHASE_BITS	12
#define RES_MASK(bits)		((1 << (bits)) - 1)

/**
 * struct ad9832_state - driver instance specific data
 * @spi:		spi_device
 * @avdd:		supply regulator for the analog section
 * @dvdd:		supply regulator for the digital section
 * @mclk:		external master clock
 * @ctrl_fp:		cached frequency/phase control word
 * @ctrl_ss:		cached sync/selsrc control word
 * @ctrl_src:		cached sleep/reset/clr word
 * @xfer:		default spi transfer
 * @msg:		default spi message
 * @freq_xfer:		tuning word spi transfer
 * @freq_msg:		tuning word spi message
 * @phase_xfer:		tuning word spi transfer
 * @phase_msg:		tuning word spi message
 * @lock:		protect sensor state
 * @data:		spi transmit buffer
 * @phase_data:		tuning word spi transmit buffer
 * @freq_data:		tuning word spi transmit buffer
 */

struct ad9832_state {
	struct spi_device		*spi;
	struct regulator		*avdd;
	struct regulator		*dvdd;
	struct clk			*mclk;
	unsigned short			ctrl_fp;
	unsigned short			ctrl_ss;
	unsigned short			ctrl_src;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	struct spi_transfer		freq_xfer[4];
	struct spi_message		freq_msg;
	struct spi_transfer		phase_xfer[2];
	struct spi_message		phase_msg;
	struct mutex			lock;	/* protect sensor state */
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		__be16			freq_data[4]____cacheline_aligned;
		__be16			phase_data[2];
		__be16			data;
	};
};

static unsigned long ad9832_calc_freqreg(unsigned long mclk, unsigned long fout)
{
	unsigned long long freqreg = (u64)fout *
				     (u64)((u64)1L << AD9832_FREQ_BITS);
	do_div(freqreg, mclk);
	return freqreg;
}

static int ad9832_write_frequency(struct ad9832_state *st,
				  unsigned int addr, unsigned long fout)
{
	unsigned long regval;

	if (fout > (clk_get_rate(st->mclk) / 2))
		return -EINVAL;

	regval = ad9832_calc_freqreg(clk_get_rate(st->mclk), fout);

	st->freq_data[0] = cpu_to_be16((AD9832_CMD_FRE8BITSW << CMD_SHIFT) |
					(addr << ADD_SHIFT) |
					((regval >> 24) & 0xFF));
	st->freq_data[1] = cpu_to_be16((AD9832_CMD_FRE16BITSW << CMD_SHIFT) |
					((addr - 1) << ADD_SHIFT) |
					((regval >> 16) & 0xFF));
	st->freq_data[2] = cpu_to_be16((AD9832_CMD_FRE8BITSW << CMD_SHIFT) |
					((addr - 2) << ADD_SHIFT) |
					((regval >> 8) & 0xFF));
	st->freq_data[3] = cpu_to_be16((AD9832_CMD_FRE16BITSW << CMD_SHIFT) |
					((addr - 3) << ADD_SHIFT) |
					((regval >> 0) & 0xFF));

	return spi_sync(st->spi, &st->freq_msg);
}

static int ad9832_write_phase(struct ad9832_state *st,
			      unsigned long addr, unsigned long phase)
{
	if (phase > BIT(AD9832_PHASE_BITS))
		return -EINVAL;

	st->phase_data[0] = cpu_to_be16((AD9832_CMD_PHA8BITSW << CMD_SHIFT) |
					(addr << ADD_SHIFT) |
					((phase >> 8) & 0xFF));
	st->phase_data[1] = cpu_to_be16((AD9832_CMD_PHA16BITSW << CMD_SHIFT) |
					((addr - 1) << ADD_SHIFT) |
					(phase & 0xFF));

	return spi_sync(st->spi, &st->phase_msg);
}

static ssize_t ad9832_write(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9832_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		goto error_ret;

	mutex_lock(&st->lock);
	switch ((u32)this_attr->address) {
	case AD9832_FREQ0HM:
	case AD9832_FREQ1HM:
		ret = ad9832_write_frequency(st, this_attr->address, val);
		break;
	case AD9832_PHASE0H:
	case AD9832_PHASE1H:
	case AD9832_PHASE2H:
	case AD9832_PHASE3H:
		ret = ad9832_write_phase(st, this_attr->address, val);
		break;
	case AD9832_PINCTRL_EN:
		if (val)
			st->ctrl_ss &= ~AD9832_SELSRC;
		else
			st->ctrl_ss |= AD9832_SELSRC;
		st->data = cpu_to_be16((AD9832_CMD_SYNCSELSRC << CMD_SHIFT) |
					st->ctrl_ss);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9832_FREQ_SYM:
		if (val == 1) {
			st->ctrl_fp |= AD9832_FREQ;
		} else if (val == 0) {
			st->ctrl_fp &= ~AD9832_FREQ;
		} else {
			ret = -EINVAL;
			break;
		}
		st->data = cpu_to_be16((AD9832_CMD_FPSELECT << CMD_SHIFT) |
					st->ctrl_fp);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9832_PHASE_SYM:
		if (val > 3) {
			ret = -EINVAL;
			break;
		}

		st->ctrl_fp &= ~AD9832_PHASE(3);
		st->ctrl_fp |= AD9832_PHASE(val);

		st->data = cpu_to_be16((AD9832_CMD_FPSELECT << CMD_SHIFT) |
					st->ctrl_fp);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9832_OUTPUT_EN:
		if (val)
			st->ctrl_src &= ~(AD9832_RESET | AD9832_SLEEP |
					AD9832_CLR);
		else
			st->ctrl_src |= AD9832_RESET;

		st->data = cpu_to_be16((AD9832_CMD_SLEEPRESCLR << CMD_SHIFT) |
					st->ctrl_src);
		ret = spi_sync(st->spi, &st->msg);
		break;
	default:
		ret = -ENODEV;
	}
	mutex_unlock(&st->lock);

error_ret:
	return ret ? ret : len;
}

/*
 * see dds.h for further information
 */

static IIO_DEV_ATTR_FREQ(0, 0, 0200, NULL, ad9832_write, AD9832_FREQ0HM);
static IIO_DEV_ATTR_FREQ(0, 1, 0200, NULL, ad9832_write, AD9832_FREQ1HM);
static IIO_DEV_ATTR_FREQSYMBOL(0, 0200, NULL, ad9832_write, AD9832_FREQ_SYM);
static IIO_CONST_ATTR_FREQ_SCALE(0, "1"); /* 1Hz */

static IIO_DEV_ATTR_PHASE(0, 0, 0200, NULL, ad9832_write, AD9832_PHASE0H);
static IIO_DEV_ATTR_PHASE(0, 1, 0200, NULL, ad9832_write, AD9832_PHASE1H);
static IIO_DEV_ATTR_PHASE(0, 2, 0200, NULL, ad9832_write, AD9832_PHASE2H);
static IIO_DEV_ATTR_PHASE(0, 3, 0200, NULL, ad9832_write, AD9832_PHASE3H);
static IIO_DEV_ATTR_PHASESYMBOL(0, 0200, NULL,
				ad9832_write, AD9832_PHASE_SYM);
static IIO_CONST_ATTR_PHASE_SCALE(0, "0.0015339808"); /* 2PI/2^12 rad*/

static IIO_DEV_ATTR_PINCONTROL_EN(0, 0200, NULL,
				ad9832_write, AD9832_PINCTRL_EN);
static IIO_DEV_ATTR_OUT_ENABLE(0, 0200, NULL,
				ad9832_write, AD9832_OUTPUT_EN);

static struct attribute *ad9832_attributes[] = {
	&iio_dev_attr_out_altvoltage0_frequency0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequency1.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_frequency_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase0.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase1.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase2.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phase3.dev_attr.attr,
	&iio_const_attr_out_altvoltage0_phase_scale.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_pincontrol_en.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_frequencysymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_phasesymbol.dev_attr.attr,
	&iio_dev_attr_out_altvoltage0_out_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9832_attribute_group = {
	.attrs = ad9832_attributes,
};

static const struct iio_info ad9832_info = {
	.attrs = &ad9832_attribute_group,
};

static int ad9832_probe(struct spi_device *spi)
{
	struct ad9832_platform_data *pdata = dev_get_platdata(&spi->dev);
	struct iio_dev *indio_dev;
	struct ad9832_state *st;
	int ret;

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);

	st->avdd = devm_regulator_get(&spi->dev, "avdd");
	if (IS_ERR(st->avdd))
		return PTR_ERR(st->avdd);

	ret = regulator_enable(st->avdd);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified AVDD supply\n");
		return ret;
	}

	st->dvdd = devm_regulator_get(&spi->dev, "dvdd");
	if (IS_ERR(st->dvdd)) {
		ret = PTR_ERR(st->dvdd);
		goto error_disable_avdd;
	}

	ret = regulator_enable(st->dvdd);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified DVDD supply\n");
		goto error_disable_avdd;
	}

	st->mclk = devm_clk_get(&spi->dev, "mclk");
	if (IS_ERR(st->mclk)) {
		ret = PTR_ERR(st->mclk);
		goto error_disable_dvdd;
	}

	ret = clk_prepare_enable(st->mclk);
	if (ret < 0)
		goto error_disable_dvdd;

	st->spi = spi;
	mutex_init(&st->lock);

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad9832_info;
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
	st->freq_xfer[1].cs_change = 1;
	st->freq_xfer[2].tx_buf = &st->freq_data[2];
	st->freq_xfer[2].len = 2;
	st->freq_xfer[2].cs_change = 1;
	st->freq_xfer[3].tx_buf = &st->freq_data[3];
	st->freq_xfer[3].len = 2;

	spi_message_init(&st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[0], &st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[1], &st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[2], &st->freq_msg);
	spi_message_add_tail(&st->freq_xfer[3], &st->freq_msg);

	st->phase_xfer[0].tx_buf = &st->phase_data[0];
	st->phase_xfer[0].len = 2;
	st->phase_xfer[0].cs_change = 1;
	st->phase_xfer[1].tx_buf = &st->phase_data[1];
	st->phase_xfer[1].len = 2;

	spi_message_init(&st->phase_msg);
	spi_message_add_tail(&st->phase_xfer[0], &st->phase_msg);
	spi_message_add_tail(&st->phase_xfer[1], &st->phase_msg);

	st->ctrl_src = AD9832_SLEEP | AD9832_RESET | AD9832_CLR;
	st->data = cpu_to_be16((AD9832_CMD_SLEEPRESCLR << CMD_SHIFT) |
					st->ctrl_src);
	ret = spi_sync(st->spi, &st->msg);
	if (ret) {
		dev_err(&spi->dev, "device init failed\n");
		goto error_unprepare_mclk;
	}

	ret = ad9832_write_frequency(st, AD9832_FREQ0HM, pdata->freq0);
	if (ret)
		goto error_unprepare_mclk;

	ret = ad9832_write_frequency(st, AD9832_FREQ1HM, pdata->freq1);
	if (ret)
		goto error_unprepare_mclk;

	ret = ad9832_write_phase(st, AD9832_PHASE0H, pdata->phase0);
	if (ret)
		goto error_unprepare_mclk;

	ret = ad9832_write_phase(st, AD9832_PHASE1H, pdata->phase1);
	if (ret)
		goto error_unprepare_mclk;

	ret = ad9832_write_phase(st, AD9832_PHASE2H, pdata->phase2);
	if (ret)
		goto error_unprepare_mclk;

	ret = ad9832_write_phase(st, AD9832_PHASE3H, pdata->phase3);
	if (ret)
		goto error_unprepare_mclk;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unprepare_mclk;

	return 0;

error_unprepare_mclk:
	clk_disable_unprepare(st->mclk);
error_disable_dvdd:
	regulator_disable(st->dvdd);
error_disable_avdd:
	regulator_disable(st->avdd);

	return ret;
}

static int ad9832_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad9832_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(st->mclk);
	regulator_disable(st->dvdd);
	regulator_disable(st->avdd);

	return 0;
}

static const struct spi_device_id ad9832_id[] = {
	{"ad9832", 0},
	{"ad9835", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ad9832_id);

static struct spi_driver ad9832_driver = {
	.driver = {
		.name	= "ad9832",
	},
	.probe		= ad9832_probe,
	.remove		= ad9832_remove,
	.id_table	= ad9832_id,
};
module_spi_driver(ad9832_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9832/AD9835 DDS");
MODULE_LICENSE("GPL v2");
