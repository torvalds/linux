/*
 * ADE7753 Single-Phase Multifunction Metering IC with di/dt Sensor Interface
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
#include "ade7753.h"

static int ade7753_spi_write_reg_8(struct device *dev,
				   u8 reg_address,
				   u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7753_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7753_spi_write_reg_16(struct device *dev,
		u8 reg_address,
		u16 value)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7753_WRITE_REG(reg_address);
	st->tx[1] = (value >> 8) & 0xFF;
	st->tx[2] = value & 0xFF;
	ret = spi_write(st->us, st->tx, 3);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7753_spi_read_reg_8(struct device *dev,
		u8 reg_address,
		u8 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);
	ssize_t ret;

	ret = spi_w8r8(st->us, ADE7753_READ_REG(reg_address));
	if (ret < 0) {
		dev_err(&st->us->dev, "problem when reading 8 bit register 0x%02X",
				reg_address);
		return ret;
	}
	*val = ret;

	return 0;
}

static int ade7753_spi_read_reg_16(struct device *dev,
		u8 reg_address,
		u16 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);
	ssize_t ret;

	ret = spi_w8r16(st->us, ADE7753_READ_REG(reg_address));
	if (ret < 0) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
			reg_address);
		return ret;
	}

	*val = ret;
	*val = be16_to_cpup(val);

	return 0;
}

static int ade7753_spi_read_reg_24(struct device *dev,
		u8 reg_address,
		u32 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 1,
		}, {
			.rx_buf = st->tx,
			.bits_per_word = 8,
			.len = 3,
		}
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7753_READ_REG(reg_address);

	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 24 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 16) | (st->rx[1] << 8) | st->rx[2];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t ade7753_read_8bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u8 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7753_spi_read_reg_8(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7753_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7753_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7753_read_24bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u32 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7753_spi_read_reg_24(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7753_write_8bit(struct device *dev,
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
	ret = ade7753_spi_write_reg_8(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7753_write_16bit(struct device *dev,
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
	ret = ade7753_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int ade7753_reset(struct device *dev)
{
	u16 val;

	ade7753_spi_read_reg_16(dev, ADE7753_MODE, &val);
	val |= 1 << 6; /* Software Chip Reset */

	return ade7753_spi_write_reg_16(dev, ADE7753_MODE, val);
}

static IIO_DEV_ATTR_AENERGY(ade7753_read_24bit, ADE7753_AENERGY);
static IIO_DEV_ATTR_LAENERGY(ade7753_read_24bit, ADE7753_LAENERGY);
static IIO_DEV_ATTR_VAENERGY(ade7753_read_24bit, ADE7753_VAENERGY);
static IIO_DEV_ATTR_LVAENERGY(ade7753_read_24bit, ADE7753_LVAENERGY);
static IIO_DEV_ATTR_CFDEN(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_CFDEN);
static IIO_DEV_ATTR_CFNUM(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_CFNUM);
static IIO_DEV_ATTR_CHKSUM(ade7753_read_8bit, ADE7753_CHKSUM);
static IIO_DEV_ATTR_PHCAL(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_PHCAL);
static IIO_DEV_ATTR_APOS(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_APOS);
static IIO_DEV_ATTR_SAGCYC(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_SAGCYC);
static IIO_DEV_ATTR_SAGLVL(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_SAGLVL);
static IIO_DEV_ATTR_LINECYC(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_LINECYC);
static IIO_DEV_ATTR_WDIV(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_WDIV);
static IIO_DEV_ATTR_IRMS(S_IWUSR | S_IRUGO,
		ade7753_read_24bit,
		NULL,
		ADE7753_IRMS);
static IIO_DEV_ATTR_VRMS(S_IRUGO,
		ade7753_read_24bit,
		NULL,
		ADE7753_VRMS);
static IIO_DEV_ATTR_IRMSOS(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_IRMSOS);
static IIO_DEV_ATTR_VRMSOS(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_VRMSOS);
static IIO_DEV_ATTR_WGAIN(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_WGAIN);
static IIO_DEV_ATTR_VAGAIN(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_VAGAIN);
static IIO_DEV_ATTR_PGA_GAIN(S_IWUSR | S_IRUGO,
		ade7753_read_16bit,
		ade7753_write_16bit,
		ADE7753_GAIN);
static IIO_DEV_ATTR_IPKLVL(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_IPKLVL);
static IIO_DEV_ATTR_VPKLVL(S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_VPKLVL);
static IIO_DEV_ATTR_IPEAK(S_IRUGO,
		ade7753_read_24bit,
		NULL,
		ADE7753_IPEAK);
static IIO_DEV_ATTR_VPEAK(S_IRUGO,
		ade7753_read_24bit,
		NULL,
		ADE7753_VPEAK);
static IIO_DEV_ATTR_VPERIOD(S_IRUGO,
		ade7753_read_16bit,
		NULL,
		ADE7753_PERIOD);
static IIO_DEV_ATTR_CH_OFF(1, S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_CH1OS);
static IIO_DEV_ATTR_CH_OFF(2, S_IWUSR | S_IRUGO,
		ade7753_read_8bit,
		ade7753_write_8bit,
		ADE7753_CH2OS);

static int ade7753_set_irq(struct device *dev, bool enable)
{
	int ret;
	u8 irqen;
	ret = ade7753_spi_read_reg_8(dev, ADE7753_IRQEN, &irqen);
	if (ret)
		goto error_ret;

	if (enable)
		irqen |= 1 << 3; /* Enables an interrupt when a data is
				    present in the waveform register */
	else
		irqen &= ~(1 << 3);

	ret = ade7753_spi_write_reg_8(dev, ADE7753_IRQEN, irqen);

error_ret:
	return ret;
}

/* Power down the device */
static int ade7753_stop_device(struct device *dev)
{
	u16 val;

	ade7753_spi_read_reg_16(dev, ADE7753_MODE, &val);
	val |= 1 << 4;  /* AD converters can be turned off */

	return ade7753_spi_write_reg_16(dev, ADE7753_MODE, val);
}

static int ade7753_initial_setup(struct iio_dev *indio_dev)
{
	int ret;
	struct device *dev = &indio_dev->dev;
	struct ade7753_state *st = iio_priv(indio_dev);

	/* use low spi speed for init */
	st->us->mode = SPI_MODE_3;
	spi_setup(st->us);

	/* Disable IRQ */
	ret = ade7753_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	ade7753_reset(dev);
	msleep(ADE7753_STARTUP_DELAY);

err_ret:
	return ret;
}

static ssize_t ade7753_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret, len = 0;
	u16 t;
	int sps;
	ret = ade7753_spi_read_reg_16(dev, ADE7753_MODE, &t);
	if (ret)
		return ret;

	t = (t >> 11) & 0x3;
	sps = 27900 / (1 + t);

	len = sprintf(buf, "%d\n", sps);
	return len;
}

static ssize_t ade7753_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7753_state *st = iio_priv(indio_dev);
	u16 val;
	int ret;
	u16 reg, t;

	ret = kstrtou16(buf, 10, &val);
	if (ret)
		return ret;
	if (val == 0)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);

	t = (27900 / val);
	if (t > 0)
		t--;

	if (t > 1)
		st->us->max_speed_hz = ADE7753_SPI_SLOW;
	else
		st->us->max_speed_hz = ADE7753_SPI_FAST;

	ret = ade7753_spi_read_reg_16(dev, ADE7753_MODE, &reg);
	if (ret)
		goto out;

	reg &= ~(3 << 11);
	reg |= t << 11;

	ret = ade7753_spi_write_reg_16(dev, ADE7753_MODE, reg);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_TEMP_RAW(ade7753_read_8bit);
static IIO_CONST_ATTR(in_temp_offset, "-25 C");
static IIO_CONST_ATTR(in_temp_scale, "0.67 C");

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ade7753_read_frequency,
		ade7753_write_frequency);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("27900 14000 7000 3500");

static struct attribute *ade7753_attributes[] = {
	&iio_dev_attr_in_temp_raw.dev_attr.attr,
	&iio_const_attr_in_temp_offset.dev_attr.attr,
	&iio_const_attr_in_temp_scale.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_phcal.dev_attr.attr,
	&iio_dev_attr_cfden.dev_attr.attr,
	&iio_dev_attr_aenergy.dev_attr.attr,
	&iio_dev_attr_laenergy.dev_attr.attr,
	&iio_dev_attr_vaenergy.dev_attr.attr,
	&iio_dev_attr_lvaenergy.dev_attr.attr,
	&iio_dev_attr_cfnum.dev_attr.attr,
	&iio_dev_attr_apos.dev_attr.attr,
	&iio_dev_attr_sagcyc.dev_attr.attr,
	&iio_dev_attr_saglvl.dev_attr.attr,
	&iio_dev_attr_linecyc.dev_attr.attr,
	&iio_dev_attr_chksum.dev_attr.attr,
	&iio_dev_attr_pga_gain.dev_attr.attr,
	&iio_dev_attr_wgain.dev_attr.attr,
	&iio_dev_attr_choff_1.dev_attr.attr,
	&iio_dev_attr_choff_2.dev_attr.attr,
	&iio_dev_attr_wdiv.dev_attr.attr,
	&iio_dev_attr_irms.dev_attr.attr,
	&iio_dev_attr_vrms.dev_attr.attr,
	&iio_dev_attr_irmsos.dev_attr.attr,
	&iio_dev_attr_vrmsos.dev_attr.attr,
	&iio_dev_attr_vagain.dev_attr.attr,
	&iio_dev_attr_ipklvl.dev_attr.attr,
	&iio_dev_attr_vpklvl.dev_attr.attr,
	&iio_dev_attr_ipeak.dev_attr.attr,
	&iio_dev_attr_vpeak.dev_attr.attr,
	&iio_dev_attr_vperiod.dev_attr.attr,
	NULL,
};

static const struct attribute_group ade7753_attribute_group = {
	.attrs = ade7753_attributes,
};

static const struct iio_info ade7753_info = {
	.attrs = &ade7753_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ade7753_probe(struct spi_device *spi)
{
	int ret;
	struct ade7753_state *st;
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
	indio_dev->info = &ade7753_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Get the device into a sane initial state */
	ret = ade7753_initial_setup(indio_dev);
	if (ret)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret)
		return ret;

	return 0;
}

/* fixme, confirm ordering in this function */
static int ade7753_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
	ade7753_stop_device(&indio_dev->dev);

	return 0;
}

static struct spi_driver ade7753_driver = {
	.driver = {
		.name = "ade7753",
		.owner = THIS_MODULE,
	},
	.probe = ade7753_probe,
	.remove = ade7753_remove,
};
module_spi_driver(ade7753_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7753/6 Single-Phase Multifunction Meter");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ade7753");
