/*
 * AD9832 SPI DDS driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <asm/div64.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "dds.h"

#include "ad9832.h"

static unsigned long ad9832_calc_freqreg(unsigned long mclk, unsigned long fout)
{
	unsigned long long freqreg = (u64) fout *
				     (u64) ((u64) 1L << AD9832_FREQ_BITS);
	do_div(freqreg, mclk);
	return freqreg;
}

static int ad9832_write_frequency(struct ad9832_state *st,
				  unsigned addr, unsigned long fout)
{
	unsigned long regval;

	if (fout > (st->mclk / 2))
		return -EINVAL;

	regval = ad9832_calc_freqreg(st->mclk, fout);

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
	if (phase > (1 << AD9832_PHASE_BITS))
		return -EINVAL;

	st->phase_data[0] = cpu_to_be16((AD9832_CMD_PHA8BITSW << CMD_SHIFT) |
					(addr << ADD_SHIFT) |
					((phase >> 8) & 0xFF));
	st->phase_data[1] = cpu_to_be16((AD9832_CMD_PHA16BITSW << CMD_SHIFT) |
					((addr - 1) << ADD_SHIFT) |
					(phase & 0xFF));

	return spi_sync(st->spi, &st->phase_msg);
}

static ssize_t ad9832_write(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad9832_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		goto error_ret;

	mutex_lock(&indio_dev->mlock);
	switch ((u32) this_attr->address) {
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
		if (val == 1)
			st->ctrl_fp |= AD9832_FREQ;
		else if (val == 0)
			st->ctrl_fp &= ~AD9832_FREQ;
		else {
			ret = -EINVAL;
			break;
		}
		st->data = cpu_to_be16((AD9832_CMD_FPSELECT << CMD_SHIFT) |
					st->ctrl_fp);
		ret = spi_sync(st->spi, &st->msg);
		break;
	case AD9832_PHASE_SYM:
		if (val < 0 || val > 3) {
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
	mutex_unlock(&indio_dev->mlock);

error_ret:
	return ret ? ret : len;
}

/**
 * see dds.h for further information
 */

static IIO_DEV_ATTR_FREQ(0, 0, S_IWUSR, NULL, ad9832_write, AD9832_FREQ0HM);
static IIO_DEV_ATTR_FREQ(0, 1, S_IWUSR, NULL, ad9832_write, AD9832_FREQ1HM);
static IIO_DEV_ATTR_FREQSYMBOL(0, S_IWUSR, NULL, ad9832_write, AD9832_FREQ_SYM);
static IIO_CONST_ATTR_FREQ_SCALE(0, "1"); /* 1Hz */

static IIO_DEV_ATTR_PHASE(0, 0, S_IWUSR, NULL, ad9832_write, AD9832_PHASE0H);
static IIO_DEV_ATTR_PHASE(0, 1, S_IWUSR, NULL, ad9832_write, AD9832_PHASE1H);
static IIO_DEV_ATTR_PHASE(0, 2, S_IWUSR, NULL, ad9832_write, AD9832_PHASE2H);
static IIO_DEV_ATTR_PHASE(0, 3, S_IWUSR, NULL, ad9832_write, AD9832_PHASE3H);
static IIO_DEV_ATTR_PHASESYMBOL(0, S_IWUSR, NULL,
				ad9832_write, AD9832_PHASE_SYM);
static IIO_CONST_ATTR_PHASE_SCALE(0, "0.0015339808"); /* 2PI/2^12 rad*/

static IIO_DEV_ATTR_PINCONTROL_EN(0, S_IWUSR, NULL,
				ad9832_write, AD9832_PINCTRL_EN);
static IIO_DEV_ATTR_OUT_ENABLE(0, S_IWUSR, NULL,
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
	.driver_module = THIS_MODULE,
};

static int ad9832_probe(struct spi_device *spi)
{
	struct ad9832_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad9832_state *st;
	struct regulator *reg;
	int ret;

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			goto error_put_reg;
	}

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);
	st->reg = reg;
	st->mclk = pdata->mclk;
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
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
		goto error_free_device;
	}

	ret = ad9832_write_frequency(st, AD9832_FREQ0HM, pdata->freq0);
	if (ret)
		goto error_free_device;

	ret = ad9832_write_frequency(st, AD9832_FREQ1HM, pdata->freq1);
	if (ret)
		goto error_free_device;

	ret = ad9832_write_phase(st, AD9832_PHASE0H, pdata->phase0);
	if (ret)
		goto error_free_device;

	ret = ad9832_write_phase(st, AD9832_PHASE1H, pdata->phase1);
	if (ret)
		goto error_free_device;

	ret = ad9832_write_phase(st, AD9832_PHASE2H, pdata->phase2);
	if (ret)
		goto error_free_device;

	ret = ad9832_write_phase(st, AD9832_PHASE3H, pdata->phase3);
	if (ret)
		goto error_free_device;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_device;

	return 0;

error_free_device:
	iio_device_free(indio_dev);
error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(reg);
error_put_reg:
	if (!IS_ERR(reg))
		regulator_put(reg);

	return ret;
}

static int __devexit ad9832_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad9832_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

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
		.owner	= THIS_MODULE,
	},
	.probe		= ad9832_probe,
	.remove		= __devexit_p(ad9832_remove),
	.id_table	= ad9832_id,
};
module_spi_driver(ad9832_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD9832/AD9835 DDS");
MODULE_LICENSE("GPL v2");
