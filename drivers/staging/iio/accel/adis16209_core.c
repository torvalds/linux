/*
 * ADIS16209 Programmable Digital Vibration Sensor driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

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
#include "inclinometer.h"
#include "../adc/adc.h"

#include "adis16209.h"

#define DRIVER_NAME		"adis16209"

/**
 * adis16209_spi_write_reg_8() - write single byte to a register
 * @indio_dev: iio device associated with actual device
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16209_spi_write_reg_8(struct iio_dev *indio_dev,
				     u8 reg_address,
				     u8 val)
{
	int ret;
	struct adis16209_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16209_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16209_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @indio_dev: iio device associated actual device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16209_spi_write_reg_16(struct iio_dev *indio_dev,
				      u8 lower_reg_address,
				      u16 value)
{
	int ret;
	struct spi_message msg;
	struct adis16209_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 30,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = 30,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16209_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16209_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16209_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @indio_dev: iio device associated with device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16209_spi_read_reg_16(struct iio_dev *indio_dev,
				     u8 lower_reg_address,
				     u16 *val)
{
	struct spi_message msg;
	struct adis16209_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 30,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = 30,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16209_READ_REG(lower_reg_address);
	st->tx[1] = 0;

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

static int adis16209_reset(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16209_spi_write_reg_8(indio_dev,
			ADIS16209_GLOB_CMD,
			ADIS16209_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&indio_dev->dev, "problem resetting device");

	return ret;
}

static ssize_t adis16209_write_reset(struct device *dev,
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
		return adis16209_reset(indio_dev);
	}
	return -EINVAL;
}

int adis16209_set_irq(struct iio_dev *indio_dev, bool enable)
{
	int ret = 0;
	u16 msc;

	ret = adis16209_spi_read_reg_16(indio_dev, ADIS16209_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16209_MSC_CTRL_ACTIVE_HIGH;
	msc &= ~ADIS16209_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS16209_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16209_MSC_CTRL_DATA_RDY_EN;

	ret = adis16209_spi_write_reg_16(indio_dev, ADIS16209_MSC_CTRL, msc);

error_ret:
	return ret;
}

static int adis16209_check_status(struct iio_dev *indio_dev)
{
	u16 status;
	int ret;

	ret = adis16209_spi_read_reg_16(indio_dev,
					ADIS16209_DIAG_STAT, &status);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x1F;

	if (status & ADIS16209_DIAG_STAT_SELFTEST_FAIL)
		dev_err(&indio_dev->dev, "Self test failure\n");
	if (status & ADIS16209_DIAG_STAT_SPI_FAIL)
		dev_err(&indio_dev->dev, "SPI failure\n");
	if (status & ADIS16209_DIAG_STAT_FLASH_UPT)
		dev_err(&indio_dev->dev, "Flash update failed\n");
	if (status & ADIS16209_DIAG_STAT_POWER_HIGH)
		dev_err(&indio_dev->dev, "Power supply above 3.625V\n");
	if (status & ADIS16209_DIAG_STAT_POWER_LOW)
		dev_err(&indio_dev->dev, "Power supply below 3.15V\n");

error_ret:
	return ret;
}

static int adis16209_self_test(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16209_spi_write_reg_16(indio_dev,
			ADIS16209_MSC_CTRL,
			ADIS16209_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(&indio_dev->dev, "problem starting self test");
		goto err_ret;
	}

	adis16209_check_status(indio_dev);

err_ret:
	return ret;
}

static int adis16209_initial_setup(struct iio_dev *indio_dev)
{
	int ret;

	/* Disable IRQ */
	ret = adis16209_set_irq(indio_dev, false);
	if (ret) {
		dev_err(&indio_dev->dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16209_self_test(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16209_check_status(indio_dev);
	if (ret) {
		adis16209_reset(indio_dev);
		dev_err(&indio_dev->dev, "device not playing ball -> reset");
		msleep(ADIS16209_STARTUP_DELAY);
		ret = adis16209_check_status(indio_dev);
		if (ret) {
			dev_err(&indio_dev->dev, "giving up");
			goto err_ret;
		}
	}

err_ret:
	return ret;
}

enum adis16209_chan {
	in_supply,
	temp,
	accel_x,
	accel_y,
	incli_x,
	incli_y,
	in_aux,
	rot,
};

static const u8 adis16209_addresses[8][2] = {
	[in_supply] = { ADIS16209_SUPPLY_OUT },
	[in_aux] = { ADIS16209_AUX_ADC },
	[accel_x] = { ADIS16209_XACCL_OUT, ADIS16209_XACCL_NULL },
	[accel_y] = { ADIS16209_YACCL_OUT, ADIS16209_YACCL_NULL },
	[incli_x] = { ADIS16209_XINCL_OUT, ADIS16209_XINCL_NULL },
	[incli_y] = { ADIS16209_YINCL_OUT, ADIS16209_YINCL_NULL },
	[rot] = { ADIS16209_ROT_OUT },
	[temp] = { ADIS16209_TEMP_OUT },
};

static int adis16209_write_raw(struct iio_dev *indio_dev,
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
		case IIO_INCLI:
			bits = 14;
			break;
		default:
			return -EINVAL;
		};
		val16 = val & ((1 << bits) - 1);
		addr = adis16209_addresses[chan->address][1];
		return adis16209_spi_write_reg_16(indio_dev, addr, val16);
	}
	return -EINVAL;
}

static int adis16209_read_raw(struct iio_dev *indio_dev,
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
		addr = adis16209_addresses[chan->address][0];
		ret = adis16209_spi_read_reg_16(indio_dev, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		if (val16 & ADIS16209_ERROR_ACTIVE) {
			ret = adis16209_check_status(indio_dev);
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
	case (1 << IIO_CHAN_INFO_SCALE_SEPARATE):
	case (1 << IIO_CHAN_INFO_SCALE_SHARED):
		switch (chan->type) {
		case IIO_IN:
			*val = 0;
			if (chan->channel == 0)
				*val2 = 305180;
			else
				*val2 = 610500;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = -470000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = 2394;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_INCLI:
			*val = 0;
			*val2 = 436;
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
			bits = 14;
			break;
		default:
			return -EINVAL;
		};
		mutex_lock(&indio_dev->mlock);
		addr = adis16209_addresses[chan->address][1];
		ret = adis16209_spi_read_reg_16(indio_dev, addr, &val16);
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

static struct iio_chan_spec adis16209_channels[] = {
	IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
		 in_supply, ADIS16209_SCAN_SUPPLY,
		 IIO_ST('u', 14, 16, 0), 0),
	IIO_CHAN(IIO_TEMP, 0, 1, 0, NULL, 0, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE) |
		 (1 << IIO_CHAN_INFO_OFFSET_SEPARATE),
		 temp, ADIS16209_SCAN_TEMP,
		 IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_X,
		 (1 << IIO_CHAN_INFO_SCALE_SHARED) |
		 (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
		 accel_x, ADIS16209_SCAN_ACC_X,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN(IIO_ACCEL, 1, 0, 0, NULL, 0, IIO_MOD_Y,
		 (1 << IIO_CHAN_INFO_SCALE_SHARED) |
		 (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
		 accel_y, ADIS16209_SCAN_ACC_Y,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 1, 0,
		 (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
		 in_aux, ADIS16209_SCAN_AUX_ADC,
		 IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_INCLI, 0, 1, 0, NULL, 0, IIO_MOD_X,
		 (1 << IIO_CHAN_INFO_SCALE_SHARED),
		 incli_x, ADIS16209_SCAN_INCLI_X,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN(IIO_INCLI, 0, 1, 0, NULL, 0, IIO_MOD_Y,
		 (1 << IIO_CHAN_INFO_SCALE_SHARED),
		 incli_y, ADIS16209_SCAN_INCLI_Y,
		 IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN(IIO_ROT, 0, 1, 0, NULL, 0, IIO_MOD_X,
		    0,
		    rot, ADIS16209_SCAN_ROT,
		    IIO_ST('s', 14, 16, 0), 0),
	IIO_CHAN_SOFT_TIMESTAMP(8)
};

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16209_write_reset, 0);

static struct attribute *adis16209_attributes[] = {
	&iio_dev_attr_reset.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16209_attribute_group = {
	.attrs = adis16209_attributes,
};

static const struct iio_info adis16209_info = {
	.attrs = &adis16209_attribute_group,
	.read_raw = &adis16209_read_raw,
	.write_raw = &adis16209_write_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit adis16209_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16209_state *st;
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
	indio_dev->info = &adis16209_info;
	indio_dev->channels = adis16209_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16209_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16209_configure_ring(indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = iio_ring_buffer_register_ex(indio_dev->ring, 0,
					  adis16209_channels,
					  ARRAY_SIZE(adis16209_channels));
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = adis16209_probe_trigger(indio_dev);
		if (ret)
			goto error_uninitialize_ring;
	}

	/* Get the device into a sane initial state */
	ret = adis16209_initial_setup(indio_dev);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	adis16209_remove_trigger(indio_dev);
error_uninitialize_ring:
	iio_ring_buffer_unregister(indio_dev->ring);
error_unreg_ring_funcs:
	adis16209_unconfigure_ring(indio_dev);
error_free_dev:
	if (regdone)
		iio_device_unregister(indio_dev);
	else
		iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int adis16209_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	flush_scheduled_work();

	adis16209_remove_trigger(indio_dev);
	iio_ring_buffer_unregister(indio_dev->ring);
	iio_device_unregister(indio_dev);
	adis16209_unconfigure_ring(indio_dev);

	return 0;
}

static struct spi_driver adis16209_driver = {
	.driver = {
		.name = "adis16209",
		.owner = THIS_MODULE,
	},
	.probe = adis16209_probe,
	.remove = __devexit_p(adis16209_remove),
};

static __init int adis16209_init(void)
{
	return spi_register_driver(&adis16209_driver);
}
module_init(adis16209_init);

static __exit void adis16209_exit(void)
{
	spi_unregister_driver(&adis16209_driver);
}
module_exit(adis16209_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16209 Digital Vibration Sensor driver");
MODULE_LICENSE("GPL v2");
