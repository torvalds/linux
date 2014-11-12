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
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include "adis16209.h"

static const u8 adis16209_addresses[8][1] = {
	[ADIS16209_SCAN_SUPPLY] = { },
	[ADIS16209_SCAN_AUX_ADC] = { },
	[ADIS16209_SCAN_ACC_X] = { ADIS16209_XACCL_NULL },
	[ADIS16209_SCAN_ACC_Y] = { ADIS16209_YACCL_NULL },
	[ADIS16209_SCAN_INCLI_X] = { ADIS16209_XINCL_NULL },
	[ADIS16209_SCAN_INCLI_Y] = { ADIS16209_YINCL_NULL },
	[ADIS16209_SCAN_ROT] = { },
	[ADIS16209_SCAN_TEMP] = { },
};

static int adis16209_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct adis *st = iio_priv(indio_dev);
	int bits;
	s16 val16;
	u8 addr;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_INCLI:
			bits = 14;
			break;
		default:
			return -EINVAL;
		}
		val16 = val & ((1 << bits) - 1);
		addr = adis16209_addresses[chan->scan_index][0];
		return adis_write_reg_16(st, addr, val16);
	}
	return -EINVAL;
}

static int adis16209_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct adis *st = iio_priv(indio_dev);
	int ret;
	int bits;
	u8 addr;
	s16 val16;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan,
			ADIS16209_ERROR_ACTIVE, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = 0;
			if (chan->channel == 0)
				*val2 = 305180; /* 0.30518 mV */
			else
				*val2 = 610500; /* 0.6105 mV */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = -470; /* -0.47 C */
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = IIO_G_TO_M_S_2(244140); /* 0.244140 mg */
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_INCLI:
		case IIO_ROT:
			*val = 0;
			*val2 = 25000; /* 0.025 degree */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = 25000 / -470 - 0x4FE; /* 25 C = 0x4FE */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ACCEL:
			bits = 14;
			break;
		default:
			return -EINVAL;
		}
		mutex_lock(&indio_dev->mlock);
		addr = adis16209_addresses[chan->scan_index][0];
		ret = adis_read_reg_16(st, addr, &val16);
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

static const struct iio_chan_spec adis16209_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16209_SUPPLY_OUT, ADIS16209_SCAN_SUPPLY, 0, 14),
	ADIS_TEMP_CHAN(ADIS16209_TEMP_OUT, ADIS16209_SCAN_TEMP, 0, 12),
	ADIS_ACCEL_CHAN(X, ADIS16209_XACCL_OUT, ADIS16209_SCAN_ACC_X,
		BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_ACCEL_CHAN(Y, ADIS16209_YACCL_OUT, ADIS16209_SCAN_ACC_Y,
		BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_AUX_ADC_CHAN(ADIS16209_AUX_ADC, ADIS16209_SCAN_AUX_ADC, 0, 12),
	ADIS_INCLI_CHAN(X, ADIS16209_XINCL_OUT, ADIS16209_SCAN_INCLI_X,
		0, 0, 14),
	ADIS_INCLI_CHAN(Y, ADIS16209_YINCL_OUT, ADIS16209_SCAN_INCLI_Y,
		0, 0, 14),
	ADIS_ROT_CHAN(X, ADIS16209_ROT_OUT, ADIS16209_SCAN_ROT, 0, 0, 14),
	IIO_CHAN_SOFT_TIMESTAMP(8)
};

static const struct iio_info adis16209_info = {
	.read_raw = &adis16209_read_raw,
	.write_raw = &adis16209_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static const char * const adis16209_status_error_msgs[] = {
	[ADIS16209_DIAG_STAT_SELFTEST_FAIL_BIT] = "Self test failure",
	[ADIS16209_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16209_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16209_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16209_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 3.15V",
};

static const struct adis_data adis16209_data = {
	.read_delay = 30,
	.msc_ctrl_reg = ADIS16209_MSC_CTRL,
	.glob_cmd_reg = ADIS16209_GLOB_CMD,
	.diag_stat_reg = ADIS16209_DIAG_STAT,

	.self_test_mask = ADIS16209_MSC_CTRL_SELF_TEST_EN,
	.startup_delay = ADIS16209_STARTUP_DELAY,

	.status_error_msgs = adis16209_status_error_msgs,
	.status_error_mask = BIT(ADIS16209_DIAG_STAT_SELFTEST_FAIL_BIT) |
		BIT(ADIS16209_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16209_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16209_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16209_DIAG_STAT_POWER_LOW_BIT),
};


static int adis16209_probe(struct spi_device *spi)
{
	int ret;
	struct adis *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16209_info;
	indio_dev->channels = adis16209_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16209_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16209_data);
	if (ret)
		return ret;
	ret = adis_setup_buffer_and_trigger(st, indio_dev, NULL);
	if (ret)
		return ret;

	/* Get the device into a sane initial state */
	ret = adis_initial_startup(st);
	if (ret)
		goto error_cleanup_buffer_trigger;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer_trigger;

	return 0;

error_cleanup_buffer_trigger:
	adis_cleanup_buffer_and_trigger(st, indio_dev);
	return ret;
}

static int adis16209_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis_cleanup_buffer_and_trigger(st, indio_dev);

	return 0;
}

static struct spi_driver adis16209_driver = {
	.driver = {
		.name = "adis16209",
		.owner = THIS_MODULE,
	},
	.probe = adis16209_probe,
	.remove = adis16209_remove,
};
module_spi_driver(adis16209_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16209 Digital Vibration Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16209");
