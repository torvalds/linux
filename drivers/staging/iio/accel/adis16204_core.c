/*
 * ADIS16204 Programmable High-g Digital Impact Sensor and Recorder
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
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include "adis16204.h"

/* Unique to this driver currently */

static const u8 adis16204_addresses[][2] = {
	[ADIS16204_SCAN_ACC_X] = { ADIS16204_XACCL_NULL, ADIS16204_X_PEAK_OUT },
	[ADIS16204_SCAN_ACC_Y] = { ADIS16204_YACCL_NULL, ADIS16204_Y_PEAK_OUT },
	[ADIS16204_SCAN_ACC_XY] = { 0, ADIS16204_XY_PEAK_OUT },
};

static int adis16204_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct adis *st = iio_priv(indio_dev);
	int ret;
	int bits;
	u8 addr;
	s16 val16;
	int addrind;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan,
				ADIS16204_ERROR_ACTIVE, val);
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
			*val = -470; /* 0.47 C */
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			switch (chan->channel2) {
			case IIO_MOD_X:
			case IIO_MOD_ROOT_SUM_SQUARED_X_Y:
				*val2 = IIO_G_TO_M_S_2(17125); /* 17.125 mg */
				break;
			case IIO_MOD_Y:
			case IIO_MOD_Z:
				*val2 = IIO_G_TO_M_S_2(8407); /* 8.407 mg */
				break;
			}
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = 25000 / -470 - 1278; /* 25 C = 1278 */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
	case IIO_CHAN_INFO_PEAK:
		if (mask == IIO_CHAN_INFO_CALIBBIAS) {
			bits = 12;
			addrind = 0;
		} else { /* PEAK_SEPARATE */
			bits = 14;
			addrind = 1;
		}
		mutex_lock(&indio_dev->mlock);
		addr = adis16204_addresses[chan->scan_index][addrind];
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

static int adis16204_write_raw(struct iio_dev *indio_dev,
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
		default:
			return -EINVAL;
		}
		val16 = val & ((1 << bits) - 1);
		addr = adis16204_addresses[chan->scan_index][1];
		return adis_write_reg_16(st, addr, val16);
	}
	return -EINVAL;
}

static const struct iio_chan_spec adis16204_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16204_SUPPLY_OUT, ADIS16204_SCAN_SUPPLY, 12),
	ADIS_AUX_ADC_CHAN(ADIS16204_AUX_ADC, ADIS16204_SCAN_AUX_ADC, 12),
	ADIS_TEMP_CHAN(ADIS16204_TEMP_OUT, ADIS16204_SCAN_TEMP, 12),
	ADIS_ACCEL_CHAN(X, ADIS16204_XACCL_OUT, ADIS16204_SCAN_ACC_X,
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_PEAK_SEPARATE_BIT, 14),
	ADIS_ACCEL_CHAN(Y, ADIS16204_YACCL_OUT, ADIS16204_SCAN_ACC_Y,
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_PEAK_SEPARATE_BIT, 14),
	ADIS_ACCEL_CHAN(ROOT_SUM_SQUARED_X_Y, ADIS16204_XY_RSS_OUT,
		ADIS16204_SCAN_ACC_XY, IIO_CHAN_INFO_PEAK_SEPARATE_BIT, 14),
	IIO_CHAN_SOFT_TIMESTAMP(5),
};

static const struct iio_info adis16204_info = {
	.read_raw = &adis16204_read_raw,
	.write_raw = &adis16204_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static const char * const adis16204_status_error_msgs[] = {
	[ADIS16204_DIAG_STAT_SELFTEST_FAIL_BIT] = "Self test failure",
	[ADIS16204_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16204_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16204_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16204_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 2.975V",
};

static const struct adis_data adis16204_data = {
	.read_delay = 20,
	.msc_ctrl_reg = ADIS16204_MSC_CTRL,
	.glob_cmd_reg = ADIS16204_GLOB_CMD,
	.diag_stat_reg = ADIS16204_DIAG_STAT,

	.self_test_mask = ADIS16204_MSC_CTRL_SELF_TEST_EN,
	.startup_delay = ADIS16204_STARTUP_DELAY,

	.status_error_msgs = adis16204_status_error_msgs,
	.status_error_mask = BIT(ADIS16204_DIAG_STAT_SELFTEST_FAIL_BIT) |
		BIT(ADIS16204_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16204_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16204_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16204_DIAG_STAT_POWER_LOW_BIT),
};

static int adis16204_probe(struct spi_device *spi)
{
	int ret;
	struct adis *st;
	struct iio_dev *indio_dev;

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
	indio_dev->info = &adis16204_info;
	indio_dev->channels = adis16204_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16204_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16204_data);
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

static int adis16204_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis_cleanup_buffer_and_trigger(st, indio_dev);
	iio_device_free(indio_dev);

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
module_spi_driver(adis16204_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("ADIS16204 High-g Digital Impact Sensor and Recorder");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16204");
