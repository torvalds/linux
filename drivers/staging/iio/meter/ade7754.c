/*
 * ADE7754 Polyphase Multifunction Energy Metering IC Driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "meter.h"
#include "ade7754.h"

static int ade7754_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7754_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7754_spi_write_reg_16(struct device *dev,
		u8 reg_address,
		u16 value)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7754_WRITE_REG(reg_address);
	st->tx[1] = (value >> 8) & 0xFF;
	st->tx[2] = value & 0xFF;
	ret = spi_write(st->us, st->tx, 3);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7754_spi_read_reg_8(struct device *dev,
		u8 reg_address,
		u8 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_w8r8(st->us, ADE7754_READ_REG(reg_address));
	if (ret < 0) {
		dev_err(&st->us->dev, "problem when reading 8 bit register 0x%02X",
				reg_address);
		return ret;
	}
	*val = ret;

	return 0;
}

static int ade7754_spi_read_reg_16(struct device *dev,
		u8 reg_address,
		u16 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_w8r16(st->us, ADE7754_READ_REG(reg_address));
	if (ret < 0) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
			reg_address);
		return ret;
	}

	*val = ret;
	*val = be16_to_cpup(val);

	return 0;
}

static int ade7754_spi_read_reg_24(struct device *dev,
		u8 reg_address,
		u32 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 4,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7754_READ_REG(reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;

	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 24 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = (st->rx[1] << 16) | (st->rx[2] << 8) | st->rx[3];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t ade7754_read_8bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u8 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7754_spi_read_reg_8(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7754_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7754_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7754_read_24bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u32 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7754_spi_read_reg_24(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val & 0xFFFFFF);
}

static ssize_t ade7754_write_8bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = ade7754_spi_write_reg_8(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7754_write_16bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	u16 val;

	ret = kstrtou16(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = ade7754_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int ade7754_reset(struct device *dev)
{
	u8 val;

	ade7754_spi_read_reg_8(dev, ADE7754_OPMODE, &val);
	val |= 1 << 6; /* Software Chip Reset */
	return ade7754_spi_write_reg_8(dev, ADE7754_OPMODE, val);
}

static IIO_DEV_ATTR_AENERGY(ade7754_read_24bit, ADE7754_AENERGY);
static IIO_DEV_ATTR_LAENERGY(ade7754_read_24bit, ADE7754_LAENERGY);
static IIO_DEV_ATTR_VAENERGY(ade7754_read_24bit, ADE7754_VAENERGY);
static IIO_DEV_ATTR_LVAENERGY(ade7754_read_24bit, ADE7754_LVAENERGY);
static IIO_DEV_ATTR_VPEAK(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VPEAK);
static IIO_DEV_ATTR_IPEAK(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VPEAK);
static IIO_DEV_ATTR_APHCAL(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_APHCAL);
static IIO_DEV_ATTR_BPHCAL(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_BPHCAL);
static IIO_DEV_ATTR_CPHCAL(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_CPHCAL);
static IIO_DEV_ATTR_AAPOS(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AAPOS);
static IIO_DEV_ATTR_BAPOS(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BAPOS);
static IIO_DEV_ATTR_CAPOS(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CAPOS);
static IIO_DEV_ATTR_WDIV(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_WDIV);
static IIO_DEV_ATTR_VADIV(S_IWUSR | S_IRUGO,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VADIV);
static IIO_DEV_ATTR_CFNUM(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CFNUM);
static IIO_DEV_ATTR_CFDEN(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CFDEN);
static IIO_DEV_ATTR_ACTIVE_POWER_A_GAIN(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AAPGAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_B_GAIN(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BAPGAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_C_GAIN(S_IWUSR | S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CAPGAIN);
static IIO_DEV_ATTR_AIRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_AIRMS);
static IIO_DEV_ATTR_BIRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_BIRMS);
static IIO_DEV_ATTR_CIRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_CIRMS);
static IIO_DEV_ATTR_AVRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_AVRMS);
static IIO_DEV_ATTR_BVRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_BVRMS);
static IIO_DEV_ATTR_CVRMS(S_IRUGO,
		ade7754_read_24bit,
		NULL,
		ADE7754_CVRMS);
static IIO_DEV_ATTR_AIRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AIRMSOS);
static IIO_DEV_ATTR_BIRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BIRMSOS);
static IIO_DEV_ATTR_CIRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CIRMSOS);
static IIO_DEV_ATTR_AVRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AVRMSOS);
static IIO_DEV_ATTR_BVRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BVRMSOS);
static IIO_DEV_ATTR_CVRMSOS(S_IRUGO,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CVRMSOS);

static int ade7754_set_irq(struct device *dev, bool enable)
{
	int ret;
	u16 irqen;
	ret = ade7754_spi_read_reg_16(dev, ADE7754_IRQEN, &irqen);
	if (ret)
		goto error_ret;

	if (enable)
		irqen |= 1 << 14; /* Enables an interrupt when a data is
				     present in the waveform register */
	else
		irqen &= ~(1 << 14);

	ret = ade7754_spi_write_reg_16(dev, ADE7754_IRQEN, irqen);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
static int ade7754_stop_device(struct device *dev)
{
	u8 val;

	ade7754_spi_read_reg_8(dev, ADE7754_OPMODE, &val);
	val |= 7 << 3;  /* ADE7754 powered down */
	return ade7754_spi_write_reg_8(dev, ADE7754_OPMODE, val);
}

static int ade7754_initial_setup(struct iio_dev *indio_dev)
{
	int ret;
	struct ade7754_state *st = iio_priv(indio_dev);
	struct device *dev = &indio_dev->dev;

	/* use low spi speed for init */
	st->us->mode = SPI_MODE_3;
	spi_setup(st->us);

	/* Disable IRQ */
	ret = ade7754_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	ade7754_reset(dev);
	msleep(ADE7754_STARTUP_DELAY);

err_ret:
	return ret;
}

static ssize_t ade7754_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u8 t;
	int sps;
	ret = ade7754_spi_read_reg_8(dev,
			ADE7754_WAVMODE,
			&t);
	if (ret)
		return ret;

	t = (t >> 3) & 0x3;
	sps = 26000 / (1 + t);

	return sprintf(buf, "%d\n", sps);
}

static ssize_t ade7754_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);
	u16 val;
	int ret;
	u8 reg, t;

	ret = kstrtou16(buf, 10, &val);
	if (ret)
		return ret;
	if (val == 0)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);

	t = (26000 / val);
	if (t > 0)
		t--;

	if (t > 1)
		st->us->max_speed_hz = ADE7754_SPI_SLOW;
	else
		st->us->max_speed_hz = ADE7754_SPI_FAST;

	ret = ade7754_spi_read_reg_8(dev, ADE7754_WAVMODE, &reg);
	if (ret)
		goto out;

	reg &= ~(3 << 3);
	reg |= t << 3;

	ret = ade7754_spi_write_reg_8(dev, ADE7754_WAVMODE, reg);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}
static IIO_DEV_ATTR_TEMP_RAW(ade7754_read_8bit);
static IIO_CONST_ATTR(in_temp_offset, "129 C");
static IIO_CONST_ATTR(in_temp_scale, "4 C");

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ade7754_read_frequency,
		ade7754_write_frequency);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("26000 13000 65000 33000");

static struct attribute *ade7754_attributes[] = {
	&iio_dev_attr_in_temp_raw.dev_attr.attr,
	&iio_const_attr_in_temp_offset.dev_attr.attr,
	&iio_const_attr_in_temp_scale.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_aenergy.dev_attr.attr,
	&iio_dev_attr_laenergy.dev_attr.attr,
	&iio_dev_attr_vaenergy.dev_attr.attr,
	&iio_dev_attr_lvaenergy.dev_attr.attr,
	&iio_dev_attr_vpeak.dev_attr.attr,
	&iio_dev_attr_ipeak.dev_attr.attr,
	&iio_dev_attr_aphcal.dev_attr.attr,
	&iio_dev_attr_bphcal.dev_attr.attr,
	&iio_dev_attr_cphcal.dev_attr.attr,
	&iio_dev_attr_aapos.dev_attr.attr,
	&iio_dev_attr_bapos.dev_attr.attr,
	&iio_dev_attr_capos.dev_attr.attr,
	&iio_dev_attr_wdiv.dev_attr.attr,
	&iio_dev_attr_vadiv.dev_attr.attr,
	&iio_dev_attr_cfnum.dev_attr.attr,
	&iio_dev_attr_cfden.dev_attr.attr,
	&iio_dev_attr_active_power_a_gain.dev_attr.attr,
	&iio_dev_attr_active_power_b_gain.dev_attr.attr,
	&iio_dev_attr_active_power_c_gain.dev_attr.attr,
	&iio_dev_attr_airms.dev_attr.attr,
	&iio_dev_attr_birms.dev_attr.attr,
	&iio_dev_attr_cirms.dev_attr.attr,
	&iio_dev_attr_avrms.dev_attr.attr,
	&iio_dev_attr_bvrms.dev_attr.attr,
	&iio_dev_attr_cvrms.dev_attr.attr,
	&iio_dev_attr_airmsos.dev_attr.attr,
	&iio_dev_attr_birmsos.dev_attr.attr,
	&iio_dev_attr_cirmsos.dev_attr.attr,
	&iio_dev_attr_avrmsos.dev_attr.attr,
	&iio_dev_attr_bvrmsos.dev_attr.attr,
	&iio_dev_attr_cvrmsos.dev_attr.attr,
	NULL,
};

static const struct attribute_group ade7754_attribute_group = {
	.attrs = ade7754_attributes,
};

static const struct iio_info ade7754_info = {
	.attrs = &ade7754_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ade7754_probe(struct spi_device *spi)
{
	int ret;
	struct ade7754_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	st = iio_priv(indio_dev);
	st->us = spi;
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ade7754_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Get the device into a sane initial state */
	ret = ade7754_initial_setup(indio_dev);
	if (ret)
		return ret;
	ret = iio_device_register(indio_dev);
	if (ret)
		return ret;

	return 0;
}

/* fixme, confirm ordering in this function */
static int ade7754_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
	ade7754_stop_device(&indio_dev->dev);

	return 0;
}

static struct spi_driver ade7754_driver = {
	.driver = {
		.name = "ade7754",
		.owner = THIS_MODULE,
	},
	.probe = ade7754_probe,
	.remove = ade7754_remove,
};
module_spi_driver(ade7754_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7754 Polyphase Multifunction Energy Metering IC Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad7754");
