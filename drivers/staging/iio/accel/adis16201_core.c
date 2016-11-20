/*
 * ADIS16201 Dual-Axis Digital Inclinometer and Accelerometer
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
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include "adis16201.h"

static const u8 adis16201_addresses[] = {
	[ADIS16201_SCAN_ACC_X] = ADIS16201_XACCL_OFFS,
	[ADIS16201_SCAN_ACC_Y] = ADIS16201_YACCL_OFFS,
	[ADIS16201_SCAN_INCLI_X] = ADIS16201_XINCL_OFFS,
	[ADIS16201_SCAN_INCLI_Y] = ADIS16201_YINCL_OFFS,
};

static int adis16201_read_raw(struct iio_dev *indio_dev,
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
				ADIS16201_ERROR_ACTIVE, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->channel == 0) {
				*val = 1;
				*val2 = 220000; /* 1.22 mV */
			} else {
				*val = 0;
				*val2 = 610000; /* 0.610 mV */
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = -470; /* 0.47 C */
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = IIO_G_TO_M_S_2(462400); /* 0.4624 mg */
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_INCLI:
			*val = 0;
			*val2 = 100000; /* 0.1 degree */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = 25000 / -470 - 1278; /* 25 C = 1278 */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ACCEL:
			bits = 12;
			break;
		case IIO_INCLI:
			bits = 9;
			break;
		default:
			return -EINVAL;
		}
		mutex_lock(&indio_dev->mlock);
		addr = adis16201_addresses[chan->scan_index];
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

static int adis16201_write_raw(struct iio_dev *indio_dev,
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
			bits = 12;
			break;
		case IIO_INCLI:
			bits = 9;
			break;
		default:
			return -EINVAL;
		}
		val16 = val & ((1 << bits) - 1);
		addr = adis16201_addresses[chan->scan_index];
		return adis_write_reg_16(st, addr, val16);
	}
	return -EINVAL;
}

static const struct iio_chan_spec adis16201_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16201_SUPPLY_OUT, ADIS16201_SCAN_SUPPLY, 0, 12),
	ADIS_TEMP_CHAN(ADIS16201_TEMP_OUT, ADIS16201_SCAN_TEMP, 0, 12),
	ADIS_ACCEL_CHAN(X, ADIS16201_XACCL_OUT, ADIS16201_SCAN_ACC_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_ACCEL_CHAN(Y, ADIS16201_YACCL_OUT, ADIS16201_SCAN_ACC_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_AUX_ADC_CHAN(ADIS16201_AUX_ADC, ADIS16201_SCAN_AUX_ADC, 0, 12),
	ADIS_INCLI_CHAN(X, ADIS16201_XINCL_OUT, ADIS16201_SCAN_INCLI_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_INCLI_CHAN(X, ADIS16201_YINCL_OUT, ADIS16201_SCAN_INCLI_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	IIO_CHAN_SOFT_TIMESTAMP(7)
};

static const struct iio_info adis16201_info = {
	.read_raw = &adis16201_read_raw,
	.write_raw = &adis16201_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static const char * const adis16201_status_error_msgs[] = {
	[ADIS16201_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16201_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16201_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16201_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 3.15V",
};

static const struct adis_data adis16201_data = {
	.read_delay = 20,
	.msc_ctrl_reg = ADIS16201_MSC_CTRL,
	.glob_cmd_reg = ADIS16201_GLOB_CMD,
	.diag_stat_reg = ADIS16201_DIAG_STAT,

	.self_test_mask = ADIS16201_MSC_CTRL_SELF_TEST_EN,
	.self_test_no_autoclear = true,
	.startup_delay = ADIS16201_STARTUP_DELAY,

	.status_error_msgs = adis16201_status_error_msgs,
	.status_error_mask = BIT(ADIS16201_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16201_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16201_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16201_DIAG_STAT_POWER_LOW_BIT),
};

static int adis16201_probe(struct spi_device *spi)
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
	indio_dev->info = &adis16201_info;

	indio_dev->channels = adis16201_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16201_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16201_data);
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
	if (ret < 0)
		goto error_cleanup_buffer_trigger;
	return 0;

error_cleanup_buffer_trigger:
	adis_cleanup_buffer_and_trigger(st, indio_dev);
	return ret;
}

static int adis16201_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis_cleanup_buffer_and_trigger(st, indio_dev);

	return 0;
}

static struct spi_driver adis16201_driver = {
	.driver = {
		.name = "adis16201",
	},
	.probe = adis16201_probe,
	.remove = adis16201_remove,
};
module_spi_driver(adis16201_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16201 Dual-Axis Digital Inclinometer and Accelerometer");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16201");
