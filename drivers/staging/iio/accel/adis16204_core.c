/*
 * ADIS16204 Programmable High-g Digital Impact Sensor and Recorder
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

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "accel.h"
#include "../adc/adc.h"

#include "adis16204.h"

#define DRIVER_NAME		"adis16204"

/**
 * adis16204_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16204_spi_write_reg_8(struct iio_dev *indio_dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16204_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @indio_dev: iio device associated with child of actual device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16204_spi_write_reg_16(struct iio_dev *indio_dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16204_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16204_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @indio_dev: iio device associated with child of actual device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16204_spi_read_reg_16(struct iio_dev *indio_dev,
				     u8 lower_reg_address,
				     u16 *val)
{
	struct spi_message msg;
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 20,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = 20,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_READ_REG(lower_reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
				lower_reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 8) | st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int adis16204_check_status(struct iio_dev *indio_dev)
{
	u16 status;
	int ret;

	ret = adis16204_spi_read_reg_16(indio_dev,
					ADIS16204_DIAG_STAT, &status);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x1F;

	if (status & ADIS16204_DIAG_STAT_SELFTEST_FAIL)
		dev_err(&indio_dev->dev, "Self test failure\n");
	if (status & ADIS16204_DIAG_STAT_SPI_FAIL)
		dev_err(&indio_dev->dev, "SPI failure\n");
	if (status & ADIS16204_DIAG_STAT_FLASH_UPT)
		dev_err(&indio_dev->dev, "Flash update failed\n");
	if (status & ADIS16204_DIAG_STAT_POWER_HIGH)
		dev_err(&indio_dev->dev, "Power supply above 3.625V\n");
	if (status & ADIS16204_DIAG_STAT_POWER_LOW)
		dev_err(&indio_dev->dev, "Power supply below 2.975V\n");

error_ret:
	return ret;
}

static ssize_t adis16204_read_14bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	s16 val = 0;
	ssize_t ret;

	mutex_lock(&indio_dev->mlock);

	ret = adis16204_spi_read_reg_16(indio_dev,
					this_attr->address, (u16 *)&val);
	if (!ret) {
		if (val & ADIS16204_ERROR_ACTIVE)
			adis16204_check_status(indio_dev);

		val = ((s16)(val << 2) >> 2);
		ret = sprintf(buf, "%d\n", val);
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int adis16204_reset(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16204_spi_write_reg_8(indio_dev,
			ADIS16204_GLOB_CMD,
			ADIS16204_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&indio_dev->dev, "problem resetting device");

	return ret;
}

static ssize_t adis16204_write_reset(struct device *dev,
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
		return adis16204_reset(indio_dev);
	}
	return -EINVAL;
}

int adis16204_set_irq(struct iio_dev *indio_dev, bool enable)
{
	int ret = 0;
	u16 msc;

	ret = adis16204_spi_read_reg_16(indio_dev, ADIS16204_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16204_MSC_CTRL_ACTIVE_HIGH;
	msc &= ~ADIS16204_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS16204_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16204_MSC_CTRL_DATA_RDY_EN;

	ret = adis16204_spi_write_reg_16(indio_dev, ADIS16204_MSC_CTRL, msc);

error_ret:
	return ret;
}

static int adis16204_self_test(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16204_spi_write_reg_16(indio_dev,
			ADIS16204_MSC_CTRL,
			ADIS16204_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(&indio_dev->dev, "problem starting self test");
		goto err_ret;
	}

	adis16204_check_status(indio_dev);

err_ret:
	return ret;
}

static int adis16204_initial_setup(struct iio_dev *indio_dev)
{
	int ret;

	/* Disable IRQ */
	ret = adis16204_set_irq(indio_dev, false);
	if (ret) {
		dev_err(&indio_dev->dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16204_self_test(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16204_check_status(indio_dev);
	if (ret) {
		adis16204_reset(indio_dev);
		dev_err(&indio_dev->dev, "device not playing ball -> reset");
		msleep(ADIS16204_STARTUP_DELAY);
		ret = adis16204_check_status(indio_dev);
		if (ret) {
			dev_err(&indio_dev->dev, "giving up");
			goto err_ret;
		}
	}

err_ret:
	return ret;
}
static IIO_DEV_ATTR_ACCEL_XY(adis16204_read_14bit_signed,
		ADIS16204_XY_RSS_OUT);
static IIO_DEV_ATTR_ACCEL_XPEAK(adis16204_read_14bit_signed,
		ADIS16204_X_PEAK_OUT);
static IIO_DEV_ATTR_ACCEL_YPEAK(adis16204_read_14bit_signed,
		ADIS16204_Y_PEAK_OUT);
static IIO_DEV_ATTR_ACCEL_XYPEAK(adis16204_read_14bit_signed,
		ADIS16204_XY_PEAK_OUT);
static IIO_CONST_ATTR(accel_xy_scale, "0.017125");

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16204_write_reset, 0);

enum adis16204_channel {
	in_supply,
	in_aux,
	temp,
	accel_x,
	accel_y,
};

static u8 adis16204_addresses[5][2] = {
	[in_supply] = { ADIS16204_SUPPLY_OUT },
	[in_aux] = { ADIS16204_AUX_ADC },
	[temp] = { ADIS16204_TEMP_OUT },
	[accel_x] = { ADIS16204_XACCL_OUT, ADIS16204_XACCL_NULL },
	[accel_y] = { ADIS16204_XACCL_OUT, ADIS16204_YACCL_NULL },
};
static int adis16204_read_raw(struct iio_dev *indio_dev,
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
		addr = adis16204_addresses[chan->address][0];
		ret = adis16204_spi_read_reg_16(indio_dev, addr, &val16);
		if (ret)
			return ret;

		if (val16 & ADIS16204_ERROR_ACTIVE) {
			ret = adis16204_check_status(indio_dev);
			if (ret)
				return ret;
		}
		val16 = val16 & ((1 << chan->scan_type.realbits) - 1);
		if (chan->scan_type.sign == 's')
			val16 = (s16)(val16 <<
				      (16 - chan->scan_type.realbits)) >>
				(16 - chan->scan_type.realbits);
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_SCALE_SEPARATE):
		switch (chan->type) {
		case IIO_IN:
			*val = 0;
			if (chan->channel == 0)
				*val2 = 1220;
			else
				*val2 = 610;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = -470000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			if (chan->channel == 'x')
				*val2 = 17125;
			else
				*val2 = 8407;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case (1 << IIO_CHAN_INFO_OFFSET_SEPARATE):
		*val = 25;
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE):
		switch (chan->type) {
		case IIO_ACCEL:
			bits = 12;
			break;
		default:
			return -EINVAL;
		};
		mutex_lock(&indio_dev->mlock);
		addr = adis16204_addresses[chan->address][1];
		ret = adis16204_spi_read_reg_16(indio_dev, addr, &val16);
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

static int adis16204_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	int bits;
	s16 val16;
	u8 addr;
	switch (mask) {
	case (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE):
		switch (chan->type) {
		case IIO_ACCEL:
			bits = 12;
			break;
		default:
			return -EINVAL;
		};
		val16 = val & ((1 << bits) - 1);
		addr = adis16204_addresses[chan->address][1];
		return adis16204_spi_write_reg_16(indio_dev, addr, val16);
	}
	return -EINVAL;
}

static struct iio_chan_spec adis16204_channels[] = {
	IIO_CHAN(IIO_IN, 0, 0, 0, "supply", 0, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
		 in_supply, ADIS16204_SCAN_SUPPLY,
		 IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 1, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
		 in_aux, ADIS16204_SCAN_AUX_ADC,
		 IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_TEMP, 0, 1, 0, NULL, 0, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE) |
		 (1 << IIO_CHAN_INFO_OFFSET_SEPARATE),
		 temp, ADIS16204_SCAN_TEMP,
		 IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_X,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE) |
		 (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
		 accel_x, ADIS16204_SCAN_ACC_X,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_Y,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE) |
		 (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
		 accel_y, ADIS16204_SCAN_ACC_Y,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN_SOFT_TIMESTAMP(5),
};
static struct attribute *adis16204_attributes[] = {
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_accel_xy.dev_attr.attr,
	&iio_dev_attr_accel_xpeak.dev_attr.attr,
	&iio_dev_attr_accel_ypeak.dev_attr.attr,
	&iio_dev_attr_accel_xypeak.dev_attr.attr,
	&iio_const_attr_accel_xy_scale.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16204_attribute_group = {
	.attrs = adis16204_attributes,
};

static const struct iio_info adis16204_info = {
	.attrs = &adis16204_attribute_group,
	.read_raw = &adis16204_read_raw,
	.write_raw = &adis16204_write_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit adis16204_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16204_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16204_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16204_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device(0);
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}

	st->indio_dev->name = spi->dev.driver->name;
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->info = &adis16204_info;
	st->indio_dev->channels = adis16204_channels;
	st->indio_dev->num_channels = ARRAY_SIZE(adis16204_channels);
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16204_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = iio_ring_buffer_register_ex(st->indio_dev->ring, 0,
					  adis16204_channels,
					  ARRAY_SIZE(adis16204_channels));
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = adis16204_probe_trigger(st->indio_dev);
		if (ret)
			goto error_uninitialize_ring;
	}

	/* Get the device into a sane initial state */
	ret = adis16204_initial_setup(st->indio_dev);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	adis16204_remove_trigger(st->indio_dev);
error_uninitialize_ring:
	iio_ring_buffer_unregister(st->indio_dev->ring);
error_unreg_ring_funcs:
	adis16204_unconfigure_ring(st->indio_dev);
error_free_dev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_tx:
	kfree(st->tx);
error_free_rx:
	kfree(st->rx);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

static int adis16204_remove(struct spi_device *spi)
{
	struct adis16204_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	adis16204_remove_trigger(indio_dev);
	iio_ring_buffer_unregister(st->indio_dev->ring);
	iio_device_unregister(indio_dev);
	adis16204_unconfigure_ring(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

static struct spi_driver adis16204_driver = {
	.driver = {
		.name = "adis16204",
		.owner = THIS_MODULE,
	},
	.probe = adis16204_probe,
	.remove = __devexit_p(adis16204_remove),
};

static __init int adis16204_init(void)
{
	return spi_register_driver(&adis16204_driver);
}
module_init(adis16204_init);

static __exit void adis16204_exit(void)
{
	spi_unregister_driver(&adis16204_driver);
}
module_exit(adis16204_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("ADIS16204 High-g Digital Impact Sensor and Recorder");
MODULE_LICENSE("GPL v2");
