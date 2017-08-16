/*
 * ADE7754 Polyphase Multifunction Energy Metering IC Driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "meter.h"

#define ADE7754_AENERGY   0x01
#define ADE7754_RAENERGY  0x02
#define ADE7754_LAENERGY  0x03
#define ADE7754_VAENERGY  0x04
#define ADE7754_RVAENERGY 0x05
#define ADE7754_LVAENERGY 0x06
#define ADE7754_PERIOD    0x07
#define ADE7754_TEMP      0x08
#define ADE7754_WFORM     0x09
#define ADE7754_OPMODE    0x0A
#define ADE7754_MMODE     0x0B
#define ADE7754_WAVMODE   0x0C
#define ADE7754_WATMODE   0x0D
#define ADE7754_VAMODE    0x0E
#define ADE7754_IRQEN     0x0F
#define ADE7754_STATUS    0x10
#define ADE7754_RSTATUS   0x11
#define ADE7754_ZXTOUT    0x12
#define ADE7754_LINCYC    0x13
#define ADE7754_SAGCYC    0x14
#define ADE7754_SAGLVL    0x15
#define ADE7754_VPEAK     0x16
#define ADE7754_IPEAK     0x17
#define ADE7754_GAIN      0x18
#define ADE7754_AWG       0x19
#define ADE7754_BWG       0x1A
#define ADE7754_CWG       0x1B
#define ADE7754_AVAG      0x1C
#define ADE7754_BVAG      0x1D
#define ADE7754_CVAG      0x1E
#define ADE7754_APHCAL    0x1F
#define ADE7754_BPHCAL    0x20
#define ADE7754_CPHCAL    0x21
#define ADE7754_AAPOS     0x22
#define ADE7754_BAPOS     0x23
#define ADE7754_CAPOS     0x24
#define ADE7754_CFNUM     0x25
#define ADE7754_CFDEN     0x26
#define ADE7754_WDIV      0x27
#define ADE7754_VADIV     0x28
#define ADE7754_AIRMS     0x29
#define ADE7754_BIRMS     0x2A
#define ADE7754_CIRMS     0x2B
#define ADE7754_AVRMS     0x2C
#define ADE7754_BVRMS     0x2D
#define ADE7754_CVRMS     0x2E
#define ADE7754_AIRMSOS   0x2F
#define ADE7754_BIRMSOS   0x30
#define ADE7754_CIRMSOS   0x31
#define ADE7754_AVRMSOS   0x32
#define ADE7754_BVRMSOS   0x33
#define ADE7754_CVRMSOS   0x34
#define ADE7754_AAPGAIN   0x35
#define ADE7754_BAPGAIN   0x36
#define ADE7754_CAPGAIN   0x37
#define ADE7754_AVGAIN    0x38
#define ADE7754_BVGAIN    0x39
#define ADE7754_CVGAIN    0x3A
#define ADE7754_CHKSUM    0x3E
#define ADE7754_VERSION   0x3F

#define ADE7754_READ_REG(a)    a
#define ADE7754_WRITE_REG(a) ((a) | 0x80)

#define ADE7754_MAX_TX    4
#define ADE7754_MAX_RX    4
#define ADE7754_STARTUP_DELAY 1000

#define ADE7754_SPI_SLOW	(u32)(300 * 1000)
#define ADE7754_SPI_BURST	(u32)(1000 * 1000)
#define ADE7754_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct ade7754_state - device instance specific data
 * @us:			actual spi_device
 * @buf_lock:		mutex to protect tx, rx and write frequency
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct ade7754_state {
	struct spi_device	*us;
	struct mutex		buf_lock;
	u8			tx[ADE7754_MAX_TX] ____cacheline_aligned;
	u8			rx[ADE7754_MAX_RX];
};

/* Unlocked version of ade7754_spi_write_reg_8 function */
static int __ade7754_spi_write_reg_8(struct device *dev, u8 reg_address, u8 val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);

	st->tx[0] = ADE7754_WRITE_REG(reg_address);
	st->tx[1] = val;
	return spi_write(st->us, st->tx, 2);
}

static int ade7754_spi_write_reg_8(struct device *dev, u8 reg_address, u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	ret = __ade7754_spi_write_reg_8(dev, reg_address, val);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7754_spi_write_reg_16(struct device *dev,
				    u8 reg_address, u16 value)
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

static int ade7754_spi_read_reg_8(struct device *dev, u8 reg_address, u8 *val)
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
				   u8 reg_address, u16 *val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7754_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_w8r16be(st->us, ADE7754_READ_REG(reg_address));
	if (ret < 0) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
			reg_address);
		return ret;
	}

	*val = ret;

	return 0;
}

static int ade7754_spi_read_reg_24(struct device *dev,
				   u8 reg_address, u32 *val)
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
	int ret;
	u8 val;

	ret = ade7754_spi_read_reg_8(dev, ADE7754_OPMODE, &val);
	if (ret < 0)
		return ret;

	val |= BIT(6); /* Software Chip Reset */
	return ade7754_spi_write_reg_8(dev, ADE7754_OPMODE, val);
}

static IIO_DEV_ATTR_AENERGY(ade7754_read_24bit, ADE7754_AENERGY);
static IIO_DEV_ATTR_LAENERGY(ade7754_read_24bit, ADE7754_LAENERGY);
static IIO_DEV_ATTR_VAENERGY(ade7754_read_24bit, ADE7754_VAENERGY);
static IIO_DEV_ATTR_LVAENERGY(ade7754_read_24bit, ADE7754_LVAENERGY);
static IIO_DEV_ATTR_VPEAK(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VPEAK);
static IIO_DEV_ATTR_IPEAK(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VPEAK);
static IIO_DEV_ATTR_APHCAL(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_APHCAL);
static IIO_DEV_ATTR_BPHCAL(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_BPHCAL);
static IIO_DEV_ATTR_CPHCAL(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_CPHCAL);
static IIO_DEV_ATTR_AAPOS(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AAPOS);
static IIO_DEV_ATTR_BAPOS(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BAPOS);
static IIO_DEV_ATTR_CAPOS(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CAPOS);
static IIO_DEV_ATTR_WDIV(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_WDIV);
static IIO_DEV_ATTR_VADIV(0644,
		ade7754_read_8bit,
		ade7754_write_8bit,
		ADE7754_VADIV);
static IIO_DEV_ATTR_CFNUM(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CFNUM);
static IIO_DEV_ATTR_CFDEN(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CFDEN);
static IIO_DEV_ATTR_ACTIVE_POWER_A_GAIN(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AAPGAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_B_GAIN(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BAPGAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_C_GAIN(0644,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CAPGAIN);
static IIO_DEV_ATTR_AIRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_AIRMS);
static IIO_DEV_ATTR_BIRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_BIRMS);
static IIO_DEV_ATTR_CIRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_CIRMS);
static IIO_DEV_ATTR_AVRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_AVRMS);
static IIO_DEV_ATTR_BVRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_BVRMS);
static IIO_DEV_ATTR_CVRMS(0444,
		ade7754_read_24bit,
		NULL,
		ADE7754_CVRMS);
static IIO_DEV_ATTR_AIRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AIRMSOS);
static IIO_DEV_ATTR_BIRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BIRMSOS);
static IIO_DEV_ATTR_CIRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CIRMSOS);
static IIO_DEV_ATTR_AVRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_AVRMSOS);
static IIO_DEV_ATTR_BVRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_BVRMSOS);
static IIO_DEV_ATTR_CVRMSOS(0444,
		ade7754_read_16bit,
		ade7754_write_16bit,
		ADE7754_CVRMSOS);

static int ade7754_set_irq(struct device *dev, bool enable)
{
	int ret;
	u16 irqen;

	ret = ade7754_spi_read_reg_16(dev, ADE7754_IRQEN, &irqen);
	if (ret)
		return ret;

	if (enable)
		irqen |= BIT(14); /* Enables an interrupt when a data is
				   * present in the waveform register
				   */
	else
		irqen &= ~BIT(14);

	return ade7754_spi_write_reg_16(dev, ADE7754_IRQEN, irqen);
}

/* Power down the device */
static int ade7754_stop_device(struct device *dev)
{
	int ret;
	u8 val;

	ret = ade7754_spi_read_reg_8(dev, ADE7754_OPMODE, &val);
	if (ret < 0) {
		dev_err(dev, "unable to power down the device, error: %d",
			ret);
		return ret;
	}

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
	usleep_range(ADE7754_STARTUP_DELAY, ADE7754_STARTUP_DELAY + 100);

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

	ret = ade7754_spi_read_reg_8(dev, ADE7754_WAVMODE, &t);
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
	if (!val)
		return -EINVAL;

	mutex_lock(&st->buf_lock);

	t = 26000 / val;
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

	ret = __ade7754_spi_write_reg_8(dev, ADE7754_WAVMODE, reg);

out:
	mutex_unlock(&st->buf_lock);

	return ret ? ret : len;
}
static IIO_DEV_ATTR_TEMP_RAW(ade7754_read_8bit);
static IIO_CONST_ATTR(in_temp_offset, "129 C");
static IIO_CONST_ATTR(in_temp_scale, "4 C");

static IIO_DEV_ATTR_SAMP_FREQ(0644,
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
		goto powerdown_on_error;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto powerdown_on_error;
	return ret;

powerdown_on_error:
	ade7754_stop_device(&indio_dev->dev);
	return ret;
}

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
	},
	.probe = ade7754_probe,
	.remove = ade7754_remove,
};
module_spi_driver(ade7754_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7754 Polyphase Multifunction Energy Metering IC Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad7754");
