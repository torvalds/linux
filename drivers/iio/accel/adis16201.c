// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADIS16201 Dual-Axis Digital Inclinometer and Accelerometer
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/imu/adis.h>

#define ADIS16201_STARTUP_DELAY_MS			220
#define ADIS16201_FLASH_CNT				0x00

/* Data Output Register Information */
#define ADIS16201_SUPPLY_OUT_REG			0x02
#define ADIS16201_XACCL_OUT_REG				0x04
#define ADIS16201_YACCL_OUT_REG				0x06
#define ADIS16201_AUX_ADC_REG				0x08
#define ADIS16201_TEMP_OUT_REG				0x0A
#define ADIS16201_XINCL_OUT_REG				0x0C
#define ADIS16201_YINCL_OUT_REG				0x0E

/* Calibration Register Definition */
#define ADIS16201_XACCL_OFFS_REG			0x10
#define ADIS16201_YACCL_OFFS_REG			0x12
#define ADIS16201_XACCL_SCALE_REG			0x14
#define ADIS16201_YACCL_SCALE_REG			0x16
#define ADIS16201_XINCL_OFFS_REG			0x18
#define ADIS16201_YINCL_OFFS_REG			0x1A
#define ADIS16201_XINCL_SCALE_REG			0x1C
#define ADIS16201_YINCL_SCALE_REG			0x1E

/* Alarm Register Definition */
#define ADIS16201_ALM_MAG1_REG				0x20
#define ADIS16201_ALM_MAG2_REG				0x22
#define ADIS16201_ALM_SMPL1_REG				0x24
#define ADIS16201_ALM_SMPL2_REG				0x26
#define ADIS16201_ALM_CTRL_REG				0x28

#define ADIS16201_AUX_DAC_REG				0x30
#define ADIS16201_GPIO_CTRL_REG				0x32
#define ADIS16201_SMPL_PRD_REG				0x36
/* Operation, filter configuration */
#define ADIS16201_AVG_CNT_REG				0x38
#define ADIS16201_SLP_CNT_REG				0x3A

/* Miscellaneous Control Register Definition */
#define ADIS16201_MSC_CTRL_REG				0x34
#define  ADIS16201_MSC_CTRL_SELF_TEST_EN		BIT(8)
/* Data-ready enable: 1 = enabled, 0 = disabled */
#define  ADIS16201_MSC_CTRL_DATA_RDY_EN			BIT(2)
/* Data-ready polarity: 1 = active high, 0 = active low */
#define  ADIS16201_MSC_CTRL_ACTIVE_DATA_RDY_HIGH	BIT(1)
/* Data-ready line selection: 1 = DIO1, 0 = DIO0 */
#define  ADIS16201_MSC_CTRL_DATA_RDY_DIO1		BIT(0)

/* Diagnostics System Status Register Definition */
#define ADIS16201_DIAG_STAT_REG				0x3C
#define  ADIS16201_DIAG_STAT_ALARM2			BIT(9)
#define  ADIS16201_DIAG_STAT_ALARM1			BIT(8)
#define  ADIS16201_DIAG_STAT_SPI_FAIL_BIT		3
#define  ADIS16201_DIAG_STAT_FLASH_UPT_FAIL_BIT		2
/* Power supply above 3.625 V */
#define  ADIS16201_DIAG_STAT_POWER_HIGH_BIT		1
/* Power supply below 3.15 V */
#define  ADIS16201_DIAG_STAT_POWER_LOW_BIT		0

/* System Command Register Definition */
#define ADIS16201_GLOB_CMD_REG				0x3E
#define  ADIS16201_GLOB_CMD_SW_RESET			BIT(7)
#define  ADIS16201_GLOB_CMD_FACTORY_RESET		BIT(1)

#define ADIS16201_ERROR_ACTIVE				BIT(14)

enum adis16201_scan {
	ADIS16201_SCAN_ACC_X,
	ADIS16201_SCAN_ACC_Y,
	ADIS16201_SCAN_INCLI_X,
	ADIS16201_SCAN_INCLI_Y,
	ADIS16201_SCAN_SUPPLY,
	ADIS16201_SCAN_AUX_ADC,
	ADIS16201_SCAN_TEMP,
};

static const u8 adis16201_addresses[] = {
	[ADIS16201_SCAN_ACC_X] = ADIS16201_XACCL_OFFS_REG,
	[ADIS16201_SCAN_ACC_Y] = ADIS16201_YACCL_OFFS_REG,
	[ADIS16201_SCAN_INCLI_X] = ADIS16201_XINCL_OFFS_REG,
	[ADIS16201_SCAN_INCLI_Y] = ADIS16201_YINCL_OFFS_REG,
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
			/* Voltage base units are mV hence 1.22 mV */
				*val = 1;
				*val2 = 220000;
			} else {
			/* Voltage base units are mV hence 0.61 mV */
				*val = 0;
				*val2 = 610000;
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = -470;
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			/*
			 * IIO base unit for sensitivity of accelerometer
			 * is milli g.
			 * 1 LSB represents 0.244 mg.
			 */
			*val = 0;
			*val2 = IIO_G_TO_M_S_2(462400);
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_INCLI:
			*val = 0;
			*val2 = 100000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * The raw ADC value is 1278 when the temperature
		 * is 25 degrees and the scale factor per milli
		 * degree celcius is -470.
		 */
		*val = 25000 / -470 - 1278;
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
		addr = adis16201_addresses[chan->scan_index];
		ret = adis_read_reg_16(st, addr, &val16);
		if (ret)
			return ret;

		*val = sign_extend32(val16, bits - 1);
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
	int m;

	if (mask != IIO_CHAN_INFO_CALIBBIAS)
		return -EINVAL;

	switch (chan->type) {
	case IIO_ACCEL:
		m = GENMASK(11, 0);
		break;
	case IIO_INCLI:
		m = GENMASK(8, 0);
		break;
	default:
		return -EINVAL;
	}

	return adis_write_reg_16(st, adis16201_addresses[chan->scan_index],
				 val & m);
}

static const struct iio_chan_spec adis16201_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16201_SUPPLY_OUT_REG, ADIS16201_SCAN_SUPPLY, 0,
			 12),
	ADIS_TEMP_CHAN(ADIS16201_TEMP_OUT_REG, ADIS16201_SCAN_TEMP, 0, 12),
	ADIS_ACCEL_CHAN(X, ADIS16201_XACCL_OUT_REG, ADIS16201_SCAN_ACC_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_ACCEL_CHAN(Y, ADIS16201_YACCL_OUT_REG, ADIS16201_SCAN_ACC_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_AUX_ADC_CHAN(ADIS16201_AUX_ADC_REG, ADIS16201_SCAN_AUX_ADC, 0, 12),
	ADIS_INCLI_CHAN(X, ADIS16201_XINCL_OUT_REG, ADIS16201_SCAN_INCLI_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_INCLI_CHAN(X, ADIS16201_YINCL_OUT_REG, ADIS16201_SCAN_INCLI_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	IIO_CHAN_SOFT_TIMESTAMP(7)
};

static const struct iio_info adis16201_info = {
	.read_raw = adis16201_read_raw,
	.write_raw = adis16201_write_raw,
	.update_scan_mode = adis_update_scan_mode,
};

static const char * const adis16201_status_error_msgs[] = {
	[ADIS16201_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16201_DIAG_STAT_FLASH_UPT_FAIL_BIT] = "Flash update failed",
	[ADIS16201_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16201_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 3.15V",
};

static const struct adis_data adis16201_data = {
	.read_delay = 20,
	.msc_ctrl_reg = ADIS16201_MSC_CTRL_REG,
	.glob_cmd_reg = ADIS16201_GLOB_CMD_REG,
	.diag_stat_reg = ADIS16201_DIAG_STAT_REG,

	.self_test_mask = ADIS16201_MSC_CTRL_SELF_TEST_EN,
	.self_test_no_autoclear = true,
	.startup_delay = ADIS16201_STARTUP_DELAY_MS,

	.status_error_msgs = adis16201_status_error_msgs,
	.status_error_mask = BIT(ADIS16201_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16201_DIAG_STAT_FLASH_UPT_FAIL_BIT) |
		BIT(ADIS16201_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16201_DIAG_STAT_POWER_LOW_BIT),
};

static int adis16201_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adis *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
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
