/*
 * ADIS16240 Programmable Impact Sensor and Recorder driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
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
#include <linux/module.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../buffer.h"

#include "adis16240.h"

#define DRIVER_NAME		"adis16240"

static int adis16240_check_status(struct iio_dev *indio_dev);

/**
 * adis16240_spi_write_reg_8() - write single byte to a register
 * @indio_dev: iio_dev associated with device
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16240_spi_write_reg_8(struct iio_dev *indio_dev,
				     u8 reg_address,
				     u8 val)
{
	int ret;
	struct adis16240_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16240_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16240_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @indio_dev: iio_dev for this device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16240_spi_write_reg_16(struct iio_dev *indio_dev,
				      u8 lower_reg_address,
				      u16 value)
{
	int ret;
	struct spi_message msg;
	struct adis16240_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16240_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16240_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16240_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @indio_dev: iio_dev for this device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16240_spi_read_reg_16(struct iio_dev *indio_dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct adis16240_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16240_READ_REG(lower_reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev,
			"problem when reading 16 bit register 0x%02X",
			lower_reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 8) | st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t adis16240_spi_read_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf,
		unsigned bits)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	int ret;
	s16 val = 0;
	unsigned shift = 16 - bits;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16240_spi_read_reg_16(indio_dev,
					this_attr->address, (u16 *)&val);
	if (ret)
		return ret;

	if (val & ADIS16240_ERROR_ACTIVE)
		adis16240_check_status(indio_dev);

	val = ((s16)(val << shift) >> shift);
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16240_read_12bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	ssize_t ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16240_spi_read_signed(dev, attr, buf, 12);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int adis16240_reset(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16240_spi_write_reg_8(indio_dev,
			ADIS16240_GLOB_CMD,
			ADIS16240_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&indio_dev->dev, "problem resetting device");

	return ret;
}

static ssize_t adis16240_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	if (len < 1)
		return -EINVAL;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16240_reset(indio_dev);
	}
	return -EINVAL;
}

int adis16240_set_irq(struct iio_dev *indio_dev, bool enable)
{
	int ret = 0;
	u16 msc;

	ret = adis16240_spi_read_reg_16(indio_dev,
					ADIS16240_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16240_MSC_CTRL_ACTIVE_HIGH;
	msc &= ~ADIS16240_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS16240_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16240_MSC_CTRL_DATA_RDY_EN;

	ret = adis16240_spi_write_reg_16(indio_dev,
					 ADIS16240_MSC_CTRL, msc);

error_ret:
	return ret;
}

static int adis16240_self_test(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16240_spi_write_reg_16(indio_dev,
			ADIS16240_MSC_CTRL,
			ADIS16240_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(&indio_dev->dev, "problem starting self test");
		goto err_ret;
	}

	msleep(ADIS16240_STARTUP_DELAY);

	adis16240_check_status(indio_dev);

err_ret:
	return ret;
}

static int adis16240_check_status(struct iio_dev *indio_dev)
{
	u16 status;
	int ret;
	struct device *dev = &indio_dev->dev;

	ret = adis16240_spi_read_reg_16(indio_dev,
					ADIS16240_DIAG_STAT, &status);

	if (ret < 0) {
		dev_err(dev, "Reading status failed\n");
		goto error_ret;
	}

	ret = status & 0x2F;
	if (status & ADIS16240_DIAG_STAT_PWRON_FAIL)
		dev_err(dev, "Power-on, self-test fail\n");
	if (status & ADIS16240_DIAG_STAT_SPI_FAIL)
		dev_err(dev, "SPI failure\n");
	if (status & ADIS16240_DIAG_STAT_FLASH_UPT)
		dev_err(dev, "Flash update failed\n");
	if (status & ADIS16240_DIAG_STAT_POWER_HIGH)
		dev_err(dev, "Power supply above 3.625V\n");
	if (status & ADIS16240_DIAG_STAT_POWER_LOW)
		dev_err(dev, "Power supply below 2.225V\n");

error_ret:
	return ret;
}

static int adis16240_initial_setup(struct iio_dev *indio_dev)
{
	int ret;
	struct device *dev = &indio_dev->dev;

	/* Disable IRQ */
	ret = adis16240_set_irq(indio_dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16240_self_test(indio_dev);
	if (ret) {
		dev_err(dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16240_check_status(indio_dev);
	if (ret) {
		adis16240_reset(indio_dev);
		dev_err(dev, "device not playing ball -> reset");
		msleep(ADIS16240_STARTUP_DELAY);
		ret = adis16240_check_status(indio_dev);
		if (ret) {
			dev_err(dev, "giving up");
			goto err_ret;
		}
	}

err_ret:
	return ret;
}

static IIO_DEVICE_ATTR(in_accel_xyz_squared_peak_raw, S_IRUGO,
		       adis16240_read_12bit_signed, NULL,
		       ADIS16240_XYZPEAK_OUT);

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16240_write_reset, 0);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("4096");

enum adis16240_chan {
	in_supply,
	in_aux,
	accel_x,
	accel_y,
	accel_z,
	temp,
};

static const u8 adis16240_addresses[6][3] = {
	[in_supply] = { ADIS16240_SUPPLY_OUT },
	[in_aux] = { ADIS16240_AUX_ADC },
	[accel_x] = { ADIS16240_XACCL_OUT, ADIS16240_XACCL_OFF,
		      ADIS16240_XPEAK_OUT },
	[accel_y] = { ADIS16240_YACCL_OUT, ADIS16240_YACCL_OFF,
		      ADIS16240_YPEAK_OUT },
	[accel_z] = { ADIS16240_ZACCL_OUT, ADIS16240_ZACCL_OFF,
		      ADIS16240_ZPEAK_OUT },
	[temp] = { ADIS16240_TEMP_OUT },
};

static int adis16240_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	int ret;
	int bits;
	u8 addr;
	s16 val16;

	switch (mask) {
	case 0:
		mutex_lock(&indio_dev->mlock);
		addr = adis16240_addresses[chan->address][0];
		ret = adis16240_spi_read_reg_16(indio_dev, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		if (val16 & ADIS16240_ERROR_ACTIVE) {
			ret = adis16240_check_status(indio_dev);
			if (ret) {
				mutex_unlock(&indio_dev->mlock);
				return ret;
			}
		}
		val16 = val16 & ((1 << chan->scan_type.realbits) - 1);
		if (chan->scan_type.sign == 's')
			val16 = (s16)(val16 <<
				      (16 - chan->scan_type.realbits)) >>
				(16 - chan->scan_type.realbits);
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = 0;
			if (chan->channel == 0)
				*val2 = 4880;
			else
				return -EINVAL;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = 244000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = 504062;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_PEAK_SCALE:
		*val = 6;
		*val2 = 629295;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = 25;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		bits = 10;
		mutex_lock(&indio_dev->mlock);
		addr = adis16240_addresses[chan->address][1];
		ret = adis16240_spi_read_reg_16(indio_dev, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		val16 &= (1 << bits) - 1;
		val16 = (s16)(val16 << (16 - bits)) >> (16 - bits);
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PEAK:
		bits = 10;
		mutex_lock(&indio_dev->mlock);
		addr = adis16240_addresses[chan->address][2];
		ret = adis16240_spi_read_reg_16(indio_dev, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		val16 &= (1 << bits) - 1;
		val16 = (s16)(val16 << (16 - bits)) >> (16 - bits);
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static int adis16240_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	int bits = 10;
	s16 val16;
	u8 addr;
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		val16 = val & ((1 << bits) - 1);
		addr = adis16240_addresses[chan->address][1];
		return adis16240_spi_write_reg_16(indio_dev, addr, val16);
	}
	return -EINVAL;
}

static struct iio_chan_spec adis16240_channels[] = {
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, "supply", 0, 0,
		 IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		 in_supply, ADIS16240_SCAN_SUPPLY,
		 IIO_ST('u', 10, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 1, 0,
		 0,
		 in_aux, ADIS16240_SCAN_AUX_ADC,
		 IIO_ST('u', 10, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_X,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT |
		 IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT,
		 accel_x, ADIS16240_SCAN_ACC_X,
		 IIO_ST('s', 10, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_Y,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT |
		 IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT,
		 accel_y, ADIS16240_SCAN_ACC_Y,
		 IIO_ST('s', 10, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_Z,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT |
		 IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT,
		 accel_z, ADIS16240_SCAN_ACC_Z,
		 IIO_ST('s', 10, 16, 0), 0),
	IIO_CHAN(IIO_TEMP, 0, 1, 0, NULL, 0, 0,
		 IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		 temp, ADIS16240_SCAN_TEMP,
		 IIO_ST('u', 10, 16, 0), 0),
	IIO_CHAN_SOFT_TIMESTAMP(6)
};

static struct attribute *adis16240_attributes[] = {
	&iio_dev_attr_in_accel_xyz_squared_peak_raw.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16240_attribute_group = {
	.attrs = adis16240_attributes,
};

static const struct iio_info adis16240_info = {
	.attrs = &adis16240_attribute_group,
	.read_raw = &adis16240_read_raw,
	.write_raw = &adis16240_write_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit adis16240_probe(struct spi_device *spi)
{
	int ret;
	struct adis16240_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	st->us = spi;
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16240_info;
	indio_dev->channels = adis16240_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16240_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16240_configure_ring(indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_buffer_register(indio_dev,
				  adis16240_channels,
				  ARRAY_SIZE(adis16240_channels));
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = adis16240_probe_trigger(indio_dev);
		if (ret)
			goto error_uninitialize_ring;
	}

	/* Get the device into a sane initial state */
	ret = adis16240_initial_setup(indio_dev);
	if (ret)
		goto error_remove_trigger;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	adis16240_remove_trigger(indio_dev);
error_uninitialize_ring:
	iio_buffer_unregister(indio_dev);
error_unreg_ring_funcs:
	adis16240_unconfigure_ring(indio_dev);
error_free_dev:
	iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int adis16240_remove(struct spi_device *spi)
{

	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	flush_scheduled_work();

	iio_device_unregister(indio_dev);
	adis16240_remove_trigger(indio_dev);
	iio_buffer_unregister(indio_dev);
	adis16240_unconfigure_ring(indio_dev);
	iio_free_device(indio_dev);

	return 0;
}

static struct spi_driver adis16240_driver = {
	.driver = {
		.name = "adis16240",
		.owner = THIS_MODULE,
	},
	.probe = adis16240_probe,
	.remove = __devexit_p(adis16240_remove),
};
module_spi_driver(adis16240_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices Programmable Impact Sensor and Recorder");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16240");
