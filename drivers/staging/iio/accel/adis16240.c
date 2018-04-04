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

#define ADIS16240_STARTUP_DELAY	220 /* ms */

/* Flash memory write count */
#define ADIS16240_FLASH_CNT      0x00

/* Output, power supply */
#define ADIS16240_SUPPLY_OUT     0x02

/* Output, x-axis accelerometer */
#define ADIS16240_XACCL_OUT      0x04

/* Output, y-axis accelerometer */
#define ADIS16240_YACCL_OUT      0x06

/* Output, z-axis accelerometer */
#define ADIS16240_ZACCL_OUT      0x08

/* Output, auxiliary ADC input */
#define ADIS16240_AUX_ADC        0x0A

/* Output, temperature */
#define ADIS16240_TEMP_OUT       0x0C

/* Output, x-axis acceleration peak */
#define ADIS16240_XPEAK_OUT      0x0E

/* Output, y-axis acceleration peak */
#define ADIS16240_YPEAK_OUT      0x10

/* Output, z-axis acceleration peak */
#define ADIS16240_ZPEAK_OUT      0x12

/* Output, sum-of-squares acceleration peak */
#define ADIS16240_XYZPEAK_OUT    0x14

/* Output, Capture Buffer 1, X and Y acceleration */
#define ADIS16240_CAPT_BUF1      0x16

/* Output, Capture Buffer 2, Z acceleration */
#define ADIS16240_CAPT_BUF2      0x18

/* Diagnostic, error flags */
#define ADIS16240_DIAG_STAT      0x1A

/* Diagnostic, event counter */
#define ADIS16240_EVNT_CNTR      0x1C

/* Diagnostic, check sum value from firmware test */
#define ADIS16240_CHK_SUM        0x1E

/* Calibration, x-axis acceleration offset adjustment */
#define ADIS16240_XACCL_OFF      0x20

/* Calibration, y-axis acceleration offset adjustment */
#define ADIS16240_YACCL_OFF      0x22

/* Calibration, z-axis acceleration offset adjustment */
#define ADIS16240_ZACCL_OFF      0x24

/* Clock, hour and minute */
#define ADIS16240_CLK_TIME       0x2E

/* Clock, month and day */
#define ADIS16240_CLK_DATE       0x30

/* Clock, year */
#define ADIS16240_CLK_YEAR       0x32

/* Wake-up setting, hour and minute */
#define ADIS16240_WAKE_TIME      0x34

/* Wake-up setting, month and day */
#define ADIS16240_WAKE_DATE      0x36

/* Alarm 1 amplitude threshold */
#define ADIS16240_ALM_MAG1       0x38

/* Alarm 2 amplitude threshold */
#define ADIS16240_ALM_MAG2       0x3A

/* Alarm control */
#define ADIS16240_ALM_CTRL       0x3C

/* Capture, external trigger control */
#define ADIS16240_XTRIG_CTRL     0x3E

/* Capture, address pointer */
#define ADIS16240_CAPT_PNTR      0x40

/* Capture, configuration and control */
#define ADIS16240_CAPT_CTRL      0x42

/* General-purpose digital input/output control */
#define ADIS16240_GPIO_CTRL      0x44

/* Miscellaneous control */
#define ADIS16240_MSC_CTRL       0x46

/* Internal sample period (rate) control */
#define ADIS16240_SMPL_PRD       0x48

/* System command */
#define ADIS16240_GLOB_CMD       0x4A

/* MSC_CTRL */

/* Enables sum-of-squares output (XYZPEAK_OUT) */
#define ADIS16240_MSC_CTRL_XYZPEAK_OUT_EN	BIT(15)

/* Enables peak tracking output (XPEAK_OUT, YPEAK_OUT, and ZPEAK_OUT) */
#define ADIS16240_MSC_CTRL_X_Y_ZPEAK_OUT_EN	BIT(14)

/* Self-test enable: 1 = apply electrostatic force, 0 = disabled */
#define ADIS16240_MSC_CTRL_SELF_TEST_EN	        BIT(8)

/* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16240_MSC_CTRL_DATA_RDY_EN	        BIT(2)

/* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16240_MSC_CTRL_ACTIVE_HIGH	        BIT(1)

/* Data-ready line selection: 1 = DIO2, 0 = DIO1 */
#define ADIS16240_MSC_CTRL_DATA_RDY_DIO2	BIT(0)

/* DIAG_STAT */

/* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16240_DIAG_STAT_ALARM2      BIT(9)

/* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16240_DIAG_STAT_ALARM1      BIT(8)

/* Capture buffer full: 1 = capture buffer is full */
#define ADIS16240_DIAG_STAT_CPT_BUF_FUL BIT(7)

/* Flash test, checksum flag: 1 = mismatch, 0 = match */
#define ADIS16240_DIAG_STAT_CHKSUM      BIT(6)

/* Power-on, self-test flag: 1 = failure, 0 = pass */
#define ADIS16240_DIAG_STAT_PWRON_FAIL_BIT  5

/* Power-on self-test: 1 = in-progress, 0 = complete */
#define ADIS16240_DIAG_STAT_PWRON_BUSY  BIT(4)

/* SPI communications failure */
#define ADIS16240_DIAG_STAT_SPI_FAIL_BIT	3

/* Flash update failure */
#define ADIS16240_DIAG_STAT_FLASH_UPT_BIT	2

/* Power supply above 3.625 V */
#define ADIS16240_DIAG_STAT_POWER_HIGH_BIT	1

 /* Power supply below 3.15 V */
#define ADIS16240_DIAG_STAT_POWER_LOW_BIT	0

/* GLOB_CMD */

#define ADIS16240_GLOB_CMD_RESUME	BIT(8)
#define ADIS16240_GLOB_CMD_SW_RESET	BIT(7)
#define ADIS16240_GLOB_CMD_STANDBY	BIT(2)

#define ADIS16240_ERROR_ACTIVE          BIT(14)

/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum adis16240_scan {
	ADIS16240_SCAN_ACC_X,
	ADIS16240_SCAN_ACC_Y,
	ADIS16240_SCAN_ACC_Z,
	ADIS16240_SCAN_SUPPLY,
	ADIS16240_SCAN_AUX_ADC,
	ADIS16240_SCAN_TEMP,
};

static ssize_t adis16240_spi_read_signed(struct device *dev,
					 struct device_attribute *attr,
					 char *buf,
					 unsigned int bits)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis *st = iio_priv(indio_dev);
	int ret;
	s16 val = 0;
	unsigned int shift = 16 - bits;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis_read_reg_16(st,
			       this_attr->address, (u16 *)&val);
	if (ret)
		return ret;

	if (val & ADIS16240_ERROR_ACTIVE)
		adis_check_status(st);

	val = (s16)(val << shift) >> shift;
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16240_read_12bit_signed(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return adis16240_spi_read_signed(dev, attr, buf, 12);
}

static IIO_DEVICE_ATTR(in_accel_xyz_squared_peak_raw, 0444,
		       adis16240_read_12bit_signed, NULL,
		       ADIS16240_XYZPEAK_OUT);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("4096");

static const u8 adis16240_addresses[][2] = {
	[ADIS16240_SCAN_ACC_X] = { ADIS16240_XACCL_OFF, ADIS16240_XPEAK_OUT },
	[ADIS16240_SCAN_ACC_Y] = { ADIS16240_YACCL_OFF, ADIS16240_YPEAK_OUT },
	[ADIS16240_SCAN_ACC_Z] = { ADIS16240_ZACCL_OFF, ADIS16240_ZPEAK_OUT },
};

static int adis16240_read_raw(struct iio_dev *indio_dev,
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
				ADIS16240_ERROR_ACTIVE, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->channel == 0) {
				*val = 4;
				*val2 = 880000; /* 4.88 mV */
				return IIO_VAL_INT_PLUS_MICRO;
			}
			return -EINVAL;
		case IIO_TEMP:
			*val = 244; /* 0.244 C */
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = IIO_G_TO_M_S_2(51400); /* 51.4 mg */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_PEAK_SCALE:
		*val = 0;
		*val2 = IIO_G_TO_M_S_2(51400); /* 51.4 mg */
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = 25000 / 244 - 0x133; /* 25 C = 0x133 */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		bits = 10;
		addr = adis16240_addresses[chan->scan_index][0];
		ret = adis_read_reg_16(st, addr, &val16);
		if (ret)
			return ret;
		val16 &= (1 << bits) - 1;
		val16 = (s16)(val16 << (16 - bits)) >> (16 - bits);
		*val = val16;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PEAK:
		bits = 10;
		addr = adis16240_addresses[chan->scan_index][1];
		ret = adis_read_reg_16(st, addr, &val16);
		if (ret)
			return ret;
		val16 &= (1 << bits) - 1;
		val16 = (s16)(val16 << (16 - bits)) >> (16 - bits);
		*val = val16;
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
	struct adis *st = iio_priv(indio_dev);
	int bits = 10;
	s16 val16;
	u8 addr;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		val16 = val & ((1 << bits) - 1);
		addr = adis16240_addresses[chan->scan_index][0];
		return adis_write_reg_16(st, addr, val16);
	}
	return -EINVAL;
}

static const struct iio_chan_spec adis16240_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16240_SUPPLY_OUT, ADIS16240_SCAN_SUPPLY, 0, 10),
	ADIS_AUX_ADC_CHAN(ADIS16240_AUX_ADC, ADIS16240_SCAN_AUX_ADC, 0, 10),
	ADIS_ACCEL_CHAN(X, ADIS16240_XACCL_OUT, ADIS16240_SCAN_ACC_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS) | BIT(IIO_CHAN_INFO_PEAK),
			0, 10),
	ADIS_ACCEL_CHAN(Y, ADIS16240_YACCL_OUT, ADIS16240_SCAN_ACC_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS) | BIT(IIO_CHAN_INFO_PEAK),
			0, 10),
	ADIS_ACCEL_CHAN(Z, ADIS16240_ZACCL_OUT, ADIS16240_SCAN_ACC_Z,
			BIT(IIO_CHAN_INFO_CALIBBIAS) | BIT(IIO_CHAN_INFO_PEAK),
			0, 10),
	ADIS_TEMP_CHAN(ADIS16240_TEMP_OUT, ADIS16240_SCAN_TEMP, 0, 10),
	IIO_CHAN_SOFT_TIMESTAMP(6)
};

static struct attribute *adis16240_attributes[] = {
	&iio_dev_attr_in_accel_xyz_squared_peak_raw.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16240_attribute_group = {
	.attrs = adis16240_attributes,
};

static const struct iio_info adis16240_info = {
	.attrs = &adis16240_attribute_group,
	.read_raw = adis16240_read_raw,
	.write_raw = adis16240_write_raw,
	.update_scan_mode = adis_update_scan_mode,
};

static const char * const adis16240_status_error_msgs[] = {
	[ADIS16240_DIAG_STAT_PWRON_FAIL_BIT] = "Power on, self-test failed",
	[ADIS16240_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16240_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16240_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16240_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 2.225V",
};

static const struct adis_data adis16240_data = {
	.write_delay = 35,
	.read_delay = 35,
	.msc_ctrl_reg = ADIS16240_MSC_CTRL,
	.glob_cmd_reg = ADIS16240_GLOB_CMD,
	.diag_stat_reg = ADIS16240_DIAG_STAT,

	.self_test_mask = ADIS16240_MSC_CTRL_SELF_TEST_EN,
	.self_test_no_autoclear = true,
	.startup_delay = ADIS16240_STARTUP_DELAY,

	.status_error_msgs = adis16240_status_error_msgs,
	.status_error_mask = BIT(ADIS16240_DIAG_STAT_PWRON_FAIL_BIT) |
		BIT(ADIS16240_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16240_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16240_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16240_DIAG_STAT_POWER_LOW_BIT),
};

static int adis16240_probe(struct spi_device *spi)
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
	indio_dev->info = &adis16240_info;
	indio_dev->channels = adis16240_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16240_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16240_data);
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

static int adis16240_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis_cleanup_buffer_and_trigger(st, indio_dev);

	return 0;
}

static struct spi_driver adis16240_driver = {
	.driver = {
		.name = "adis16240",
	},
	.probe = adis16240_probe,
	.remove = adis16240_remove,
};
module_spi_driver(adis16240_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices Programmable Impact Sensor and Recorder");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16240");
