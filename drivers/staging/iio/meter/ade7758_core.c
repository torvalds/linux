/*
 * ADE7758 Poly Phase Multifunction Energy Metering IC driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "meter.h"
#include "ade7758.h"

int ade7758_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7758_spi_write_reg_16(struct device *dev,
		u8 reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 3,
		}
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_WRITE_REG(reg_address);
	st->tx[1] = (value >> 8) & 0xFF;
	st->tx[2] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7758_spi_write_reg_24(struct device *dev,
		u8 reg_address,
		u32 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 4,
		}
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_WRITE_REG(reg_address);
	st->tx[1] = (value >> 16) & 0xFF;
	st->tx[2] = (value >> 8) & 0xFF;
	st->tx[3] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

int ade7758_spi_read_reg_8(struct device *dev,
		u8 reg_address,
		u8 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 1,
			.delay_usecs = 4,
		},
		{
			.tx_buf = &st->tx[1],
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 1,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_READ_REG(reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 8 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = st->rx[0];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7758_spi_read_reg_16(struct device *dev,
		u8 reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 1,
			.delay_usecs = 4,
		},
		{
			.tx_buf = &st->tx[1],
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
		},
	};


	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_READ_REG(reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}

	*val = (st->rx[0] << 8) | st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7758_spi_read_reg_24(struct device *dev,
		u8 reg_address,
		u32 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 1,
			.delay_usecs = 4,
		},
		{
			.tx_buf = &st->tx[1],
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 3,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_READ_REG(reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
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

static ssize_t ade7758_read_8bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u8 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7758_spi_read_reg_8(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7758_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7758_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7758_read_24bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u32 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7758_spi_read_reg_24(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val & 0xFFFFFF);
}

static ssize_t ade7758_write_8bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = ade7758_spi_write_reg_8(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7758_write_16bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = ade7758_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int ade7758_reset(struct device *dev)
{
	int ret;
	u8 val;
	ade7758_spi_read_reg_8(dev,
			ADE7758_OPMODE,
			&val);
	val |= 1 << 6; /* Software Chip Reset */
	ret = ade7758_spi_write_reg_8(dev,
			ADE7758_OPMODE,
			val);

	return ret;
}

static ssize_t ade7758_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -1;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return ade7758_reset(dev);
	}
	return len;
}

static IIO_DEV_ATTR_VPEAK(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_VPEAK);
static IIO_DEV_ATTR_IPEAK(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_VPEAK);
static IIO_DEV_ATTR_APHCAL(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_APHCAL);
static IIO_DEV_ATTR_BPHCAL(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_BPHCAL);
static IIO_DEV_ATTR_CPHCAL(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_CPHCAL);
static IIO_DEV_ATTR_WDIV(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_WDIV);
static IIO_DEV_ATTR_VADIV(S_IWUSR | S_IRUGO,
		ade7758_read_8bit,
		ade7758_write_8bit,
		ADE7758_VADIV);
static IIO_DEV_ATTR_AIRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_AIRMS);
static IIO_DEV_ATTR_BIRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_BIRMS);
static IIO_DEV_ATTR_CIRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_CIRMS);
static IIO_DEV_ATTR_AVRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_AVRMS);
static IIO_DEV_ATTR_BVRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_BVRMS);
static IIO_DEV_ATTR_CVRMS(S_IRUGO,
		ade7758_read_24bit,
		NULL,
		ADE7758_CVRMS);
static IIO_DEV_ATTR_AIRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_AIRMSOS);
static IIO_DEV_ATTR_BIRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_BIRMSOS);
static IIO_DEV_ATTR_CIRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_CIRMSOS);
static IIO_DEV_ATTR_AVRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_AVRMSOS);
static IIO_DEV_ATTR_BVRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_BVRMSOS);
static IIO_DEV_ATTR_CVRMSOS(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_CVRMSOS);
static IIO_DEV_ATTR_AIGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_AIGAIN);
static IIO_DEV_ATTR_BIGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_BIGAIN);
static IIO_DEV_ATTR_CIGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_CIGAIN);
static IIO_DEV_ATTR_AVRMSGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_AVRMSGAIN);
static IIO_DEV_ATTR_BVRMSGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_BVRMSGAIN);
static IIO_DEV_ATTR_CVRMSGAIN(S_IWUSR | S_IRUGO,
		ade7758_read_16bit,
		ade7758_write_16bit,
		ADE7758_CVRMSGAIN);

int ade7758_set_irq(struct device *dev, bool enable)
{
	int ret;
	u32 irqen;
	ret = ade7758_spi_read_reg_24(dev, ADE7758_MASK, &irqen);
	if (ret)
		goto error_ret;

	if (enable)
		irqen |= 1 << 16; /* Enables an interrupt when a data is
				     present in the waveform register */
	else
		irqen &= ~(1 << 16);

	ret = ade7758_spi_write_reg_24(dev, ADE7758_MASK, irqen);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
static int ade7758_stop_device(struct device *dev)
{
	int ret;
	u8 val;
	ade7758_spi_read_reg_8(dev,
			ADE7758_OPMODE,
			&val);
	val |= 7 << 3;  /* ADE7758 powered down */
	ret = ade7758_spi_write_reg_8(dev,
			ADE7758_OPMODE,
			val);

	return ret;
}

static int ade7758_initial_setup(struct iio_dev *indio_dev)
{
	struct ade7758_state *st = iio_priv(indio_dev);
	struct device *dev = &indio_dev->dev;
	int ret;

	/* use low spi speed for init */
	st->us->mode = SPI_MODE_1;
	spi_setup(st->us);

	/* Disable IRQ */
	ret = ade7758_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	ade7758_reset(dev);
	msleep(ADE7758_STARTUP_DELAY);

err_ret:
	return ret;
}

static ssize_t ade7758_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret, len = 0;
	u8 t;
	int sps;
	ret = ade7758_spi_read_reg_8(dev,
			ADE7758_WAVMODE,
			&t);
	if (ret)
		return ret;

	t = (t >> 5) & 0x3;
	sps = 26040 / (1 << t);

	len = sprintf(buf, "%d SPS\n", sps);
	return len;
}

static ssize_t ade7758_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	unsigned long val;
	int ret;
	u8 reg, t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	switch (val) {
	case 26040:
		t = 0;
		break;
	case 13020:
		t = 1;
		break;
	case 6510:
		t = 2;
		break;
	case 3255:
		t = 3;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = ade7758_spi_read_reg_8(dev,
			ADE7758_WAVMODE,
			&reg);
	if (ret)
		goto out;

	reg &= ~(5 << 3);
	reg |= t << 5;

	ret = ade7758_spi_write_reg_8(dev,
			ADE7758_WAVMODE,
			reg);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_TEMP_RAW(ade7758_read_8bit);
static IIO_CONST_ATTR(temp_offset, "129 C");
static IIO_CONST_ATTR(temp_scale, "4 C");

static IIO_DEV_ATTR_AWATTHR(ade7758_read_16bit,
		ADE7758_AWATTHR);
static IIO_DEV_ATTR_BWATTHR(ade7758_read_16bit,
		ADE7758_BWATTHR);
static IIO_DEV_ATTR_CWATTHR(ade7758_read_16bit,
		ADE7758_CWATTHR);
static IIO_DEV_ATTR_AVARHR(ade7758_read_16bit,
		ADE7758_AVARHR);
static IIO_DEV_ATTR_BVARHR(ade7758_read_16bit,
		ADE7758_BVARHR);
static IIO_DEV_ATTR_CVARHR(ade7758_read_16bit,
		ADE7758_CVARHR);
static IIO_DEV_ATTR_AVAHR(ade7758_read_16bit,
		ADE7758_AVAHR);
static IIO_DEV_ATTR_BVAHR(ade7758_read_16bit,
		ADE7758_BVAHR);
static IIO_DEV_ATTR_CVAHR(ade7758_read_16bit,
		ADE7758_CVAHR);

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ade7758_read_frequency,
		ade7758_write_frequency);

static IIO_DEV_ATTR_RESET(ade7758_write_reset);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("26040 13020 6510 3255");

static struct attribute *ade7758_attributes[] = {
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_awatthr.dev_attr.attr,
	&iio_dev_attr_bwatthr.dev_attr.attr,
	&iio_dev_attr_cwatthr.dev_attr.attr,
	&iio_dev_attr_avarhr.dev_attr.attr,
	&iio_dev_attr_bvarhr.dev_attr.attr,
	&iio_dev_attr_cvarhr.dev_attr.attr,
	&iio_dev_attr_avahr.dev_attr.attr,
	&iio_dev_attr_bvahr.dev_attr.attr,
	&iio_dev_attr_cvahr.dev_attr.attr,
	&iio_dev_attr_vpeak.dev_attr.attr,
	&iio_dev_attr_ipeak.dev_attr.attr,
	&iio_dev_attr_aphcal.dev_attr.attr,
	&iio_dev_attr_bphcal.dev_attr.attr,
	&iio_dev_attr_cphcal.dev_attr.attr,
	&iio_dev_attr_wdiv.dev_attr.attr,
	&iio_dev_attr_vadiv.dev_attr.attr,
	&iio_dev_attr_airms.dev_attr.attr,
	&iio_dev_attr_birms.dev_attr.attr,
	&iio_dev_attr_cirms.dev_attr.attr,
	&iio_dev_attr_avrms.dev_attr.attr,
	&iio_dev_attr_bvrms.dev_attr.attr,
	&iio_dev_attr_cvrms.dev_attr.attr,
	&iio_dev_attr_aigain.dev_attr.attr,
	&iio_dev_attr_bigain.dev_attr.attr,
	&iio_dev_attr_cigain.dev_attr.attr,
	&iio_dev_attr_avrmsgain.dev_attr.attr,
	&iio_dev_attr_bvrmsgain.dev_attr.attr,
	&iio_dev_attr_cvrmsgain.dev_attr.attr,
	&iio_dev_attr_airmsos.dev_attr.attr,
	&iio_dev_attr_birmsos.dev_attr.attr,
	&iio_dev_attr_cirmsos.dev_attr.attr,
	&iio_dev_attr_avrmsos.dev_attr.attr,
	&iio_dev_attr_bvrmsos.dev_attr.attr,
	&iio_dev_attr_cvrmsos.dev_attr.attr,
	NULL,
};

static const struct attribute_group ade7758_attribute_group = {
	.attrs = ade7758_attributes,
};

static struct iio_chan_spec ade7758_channels[] = {
	IIO_CHAN(IIO_IN, 0, 1, 0, "raw", 0, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_A, AD7758_VOLTAGE),
		0, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_CURRENT, 0, 1, 0, "raw", 0, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_A, AD7758_CURRENT),
		1, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "apparent_raw", 0, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_A, AD7758_APP_PWR),
		2, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "active_raw", 0, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_A, AD7758_ACT_PWR),
		3, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "reactive_raw", 0, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_A, AD7758_REACT_PWR),
		4, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_IN, 0, 1, 0, "raw", 1, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_B, AD7758_VOLTAGE),
		5, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_CURRENT, 0, 1, 0, "raw", 1, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_B, AD7758_CURRENT),
		6, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "apparent_raw", 1, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_B, AD7758_APP_PWR),
		7, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "active_raw", 1, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_B, AD7758_ACT_PWR),
		8, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "reactive_raw", 1, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_B, AD7758_REACT_PWR),
		9, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_IN, 0, 1, 0, "raw", 2, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_C, AD7758_VOLTAGE),
		10, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_CURRENT, 0, 1, 0, "raw", 2, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_C, AD7758_CURRENT),
		11, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "apparent_raw", 2, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_C, AD7758_APP_PWR),
		12, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "active_raw", 2, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_C, AD7758_ACT_PWR),
		13, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN(IIO_POWER, 0, 1, 0, "reactive_raw", 2, 0,
		(1 << IIO_CHAN_INFO_SCALE_SHARED),
		AD7758_WT(AD7758_PHASE_C, AD7758_REACT_PWR),
		14, IIO_ST('s', 24, 32, 0), 0),
	IIO_CHAN_SOFT_TIMESTAMP(15),
};

static const struct iio_info ade7758_info = {
	.attrs = &ade7758_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ade7758_probe(struct spi_device *spi)
{
	int i, ret, regdone = 0;
	struct ade7758_state *st;
	struct iio_dev *indio_dev = iio_allocate_device(sizeof(*st));

	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADE7758_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_dev;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADE7758_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	st->ade7758_ring_channels = &ade7758_channels[0];
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ade7758_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	for (i = 0; i < AD7758_NUM_WAVESRC; i++)
		st->available_scan_masks[i] = 1 << i;

	indio_dev->available_scan_masks = st->available_scan_masks;

	ret = ade7758_configure_ring(indio_dev);
	if (ret)
		goto error_free_tx;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = iio_ring_buffer_register_ex(indio_dev->ring, 0,
					  &ade7758_channels[0],
					  ARRAY_SIZE(ade7758_channels));
	if (ret) {
		dev_err(&spi->dev, "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	/* Get the device into a sane initial state */
	ret = ade7758_initial_setup(indio_dev);
	if (ret)
		goto error_uninitialize_ring;

	if (spi->irq) {
		ret = ade7758_probe_trigger(indio_dev);
		if (ret)
			goto error_remove_trigger;
	}

	return 0;

error_remove_trigger:
	if (indio_dev->modes & INDIO_RING_TRIGGERED)
		ade7758_remove_trigger(indio_dev);
error_uninitialize_ring:
	ade7758_uninitialize_ring(indio_dev->ring);
error_unreg_ring_funcs:
	ade7758_unconfigure_ring(indio_dev);
error_free_tx:
	kfree(st->tx);
error_free_rx:
	kfree(st->rx);
error_free_dev:
	if (regdone)
		iio_device_unregister(indio_dev);
	else
		iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int ade7758_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret;

	ret = ade7758_stop_device(&indio_dev->dev);
	if (ret)
		goto err_ret;

	ade7758_remove_trigger(indio_dev);
	ade7758_uninitialize_ring(indio_dev->ring);
	ade7758_unconfigure_ring(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	iio_device_unregister(indio_dev);

	return 0;
err_ret:
	return ret;
}

static const struct spi_device_id ade7758_id[] = {
	{"ade7758", 0},
	{}
};

static struct spi_driver ade7758_driver = {
	.driver = {
		.name = "ade7758",
		.owner = THIS_MODULE,
	},
	.probe = ade7758_probe,
	.remove = __devexit_p(ade7758_remove),
	.id_table = ade7758_id,
};

static __init int ade7758_init(void)
{
	return spi_register_driver(&ade7758_driver);
}
module_init(ade7758_init);

static __exit void ade7758_exit(void)
{
	spi_unregister_driver(&ade7758_driver);
}
module_exit(ade7758_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7758 Polyphase Multifunction Energy Metering IC Driver");
MODULE_LICENSE("GPL v2");
