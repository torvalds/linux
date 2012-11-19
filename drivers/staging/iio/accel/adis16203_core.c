/*
 * ADIS16203 Programmable Digital Vibration Sensor driver
 *
 * Copyright 2030 Analog Devices Inc.
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
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include "adis16203.h"

#define DRIVER_NAME		"adis16203"

static const u8 adis16203_addresses[] = {
	[ADIS16203_SCAN_INCLI_X] = ADIS16203_INCL_NULL,
};

static int adis16203_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct adis *st = iio_priv(indio_dev);
	/* currently only one writable parameter which keeps this simple */
	u8 addr = adis16203_addresses[chan->scan_index];
	return adis_write_reg_16(st, addr, val & 0x3FFF);
}

static int adis16203_read_raw(struct iio_dev *indio_dev,
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
				ADIS16203_ERROR_ACTIVE, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->channel == 0) {
				*val = 1;
				*val2 = 220000; /* 1.22 mV */
			} else {
				*val = 0;
				*val2 = 610000; /* 0.61 mV */
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = -470; /* -0.47 C */
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_INCLI:
			*val = 0;
			*val2 = 25000; /* 0.025 degree */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		*val = 25000 / -470 - 1278; /* 25 C = 1278 */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		bits = 14;
		mutex_lock(&indio_dev->mlock);
		addr = adis16203_addresses[chan->scan_index];
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
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec adis16203_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16203_SUPPLY_OUT, ADIS16203_SCAN_SUPPLY, 12),
	ADIS_AUX_ADC_CHAN(ADIS16203_AUX_ADC, ADIS16203_SCAN_AUX_ADC, 12),
	ADIS_INCLI_CHAN(X, ADIS16203_XINCL_OUT, ADIS16203_SCAN_INCLI_X,
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT, 14),
	/* Fixme: Not what it appears to be - see data sheet */
	ADIS_INCLI_CHAN(Y, ADIS16203_YINCL_OUT, ADIS16203_SCAN_INCLI_Y, 0, 14),
	ADIS_TEMP_CHAN(ADIS16203_TEMP_OUT, ADIS16203_SCAN_TEMP, 12),
	IIO_CHAN_SOFT_TIMESTAMP(5),
};

static const struct iio_info adis16203_info = {
	.read_raw = &adis16203_read_raw,
	.write_raw = &adis16203_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static const char * const adis16203_status_error_msgs[] = {
	[ADIS16203_DIAG_STAT_SELFTEST_FAIL_BIT] = "Self test failure",
	[ADIS16203_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16203_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16203_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16203_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 3.15V",
};

static const struct adis_data adis16203_data = {
	.read_delay = 20,
	.msc_ctrl_reg = ADIS16203_MSC_CTRL,
	.glob_cmd_reg = ADIS16203_GLOB_CMD,
	.diag_stat_reg = ADIS16203_DIAG_STAT,

	.self_test_mask = ADIS16203_MSC_CTRL_SELF_TEST_EN,
	.startup_delay = ADIS16203_STARTUP_DELAY,

	.status_error_msgs = adis16203_status_error_msgs,
	.status_error_mask = BIT(ADIS16203_DIAG_STAT_SELFTEST_FAIL_BIT) |
		BIT(ADIS16203_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16203_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16203_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16203_DIAG_STAT_POWER_LOW_BIT),
};

static int adis16203_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev;
	struct adis *st;

	/* setup the industrialio driver allocated elements */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->channels = adis16203_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16203_channels);
	indio_dev->info = &adis16203_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16203_data);
	if (ret)
		goto error_free_dev;

	ret = adis_setup_buffer_and_trigger(st, indio_dev, NULL);
	if (ret)
		goto error_free_dev;

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
error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int __devexit adis16203_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis_cleanup_buffer_and_trigger(st, indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct spi_driver adis16203_driver = {
	.driver = {
		.name = "adis16203",
		.owner = THIS_MODULE,
	},
	.probe = adis16203_probe,
	.remove = __devexit_p(adis16203_remove),
};
module_spi_driver(adis16203_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16203 Programmable Digital Vibration Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16203");
