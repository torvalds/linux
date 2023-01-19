// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADIS16209 Dual-Axis Digital Inclinometer and Accelerometer
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/imu/adis.h>

#define ADIS16209_STARTUP_DELAY_MS	220
#define ADIS16209_FLASH_CNT_REG		0x00

/* Data Output Register Definitions */
#define ADIS16209_SUPPLY_OUT_REG	0x02
#define ADIS16209_XACCL_OUT_REG		0x04
#define ADIS16209_YACCL_OUT_REG		0x06
/* Output, auxiliary ADC input */
#define ADIS16209_AUX_ADC_REG		0x08
/* Output, temperature */
#define ADIS16209_TEMP_OUT_REG		0x0A
/* Output, +/- 90 degrees X-axis inclination */
#define ADIS16209_XINCL_OUT_REG		0x0C
#define ADIS16209_YINCL_OUT_REG		0x0E
/* Output, +/-180 vertical rotational position */
#define ADIS16209_ROT_OUT_REG		0x10

/*
 * Calibration Register Definitions.
 * Acceleration, inclination or rotation offset null.
 */
#define ADIS16209_XACCL_NULL_REG	0x12
#define ADIS16209_YACCL_NULL_REG	0x14
#define ADIS16209_XINCL_NULL_REG	0x16
#define ADIS16209_YINCL_NULL_REG	0x18
#define ADIS16209_ROT_NULL_REG		0x1A

/* Alarm Register Definitions */
#define ADIS16209_ALM_MAG1_REG		0x20
#define ADIS16209_ALM_MAG2_REG		0x22
#define ADIS16209_ALM_SMPL1_REG		0x24
#define ADIS16209_ALM_SMPL2_REG		0x26
#define ADIS16209_ALM_CTRL_REG		0x28

#define ADIS16209_AUX_DAC_REG		0x30
#define ADIS16209_GPIO_CTRL_REG		0x32
#define ADIS16209_SMPL_PRD_REG		0x36
#define ADIS16209_AVG_CNT_REG		0x38
#define ADIS16209_SLP_CNT_REG		0x3A

#define ADIS16209_MSC_CTRL_REG			0x34
#define  ADIS16209_MSC_CTRL_PWRUP_SELF_TEST	BIT(10)
#define  ADIS16209_MSC_CTRL_SELF_TEST_EN	BIT(8)
#define  ADIS16209_MSC_CTRL_DATA_RDY_EN		BIT(2)
/* Data-ready polarity: 1 = active high, 0 = active low */
#define  ADIS16209_MSC_CTRL_ACTIVE_HIGH		BIT(1)
#define  ADIS16209_MSC_CTRL_DATA_RDY_DIO2	BIT(0)

#define ADIS16209_STAT_REG			0x3C
#define  ADIS16209_STAT_ALARM2			BIT(9)
#define  ADIS16209_STAT_ALARM1			BIT(8)
#define  ADIS16209_STAT_SELFTEST_FAIL_BIT	5
#define  ADIS16209_STAT_SPI_FAIL_BIT		3
#define  ADIS16209_STAT_FLASH_UPT_FAIL_BIT	2
/* Power supply above 3.625 V */
#define  ADIS16209_STAT_POWER_HIGH_BIT		1
/* Power supply below 2.975 V */
#define  ADIS16209_STAT_POWER_LOW_BIT		0

#define ADIS16209_CMD_REG			0x3E
#define  ADIS16209_CMD_SW_RESET			BIT(7)
#define  ADIS16209_CMD_CLEAR_STAT		BIT(4)
#define  ADIS16209_CMD_FACTORY_CAL		BIT(1)

#define ADIS16209_ERROR_ACTIVE			BIT(14)

enum adis16209_scan {
	ADIS16209_SCAN_SUPPLY,
	ADIS16209_SCAN_ACC_X,
	ADIS16209_SCAN_ACC_Y,
	ADIS16209_SCAN_AUX_ADC,
	ADIS16209_SCAN_TEMP,
	ADIS16209_SCAN_INCLI_X,
	ADIS16209_SCAN_INCLI_Y,
	ADIS16209_SCAN_ROT,
};

static const u8 adis16209_addresses[8][1] = {
	[ADIS16209_SCAN_SUPPLY] = { },
	[ADIS16209_SCAN_AUX_ADC] = { },
	[ADIS16209_SCAN_ACC_X] = { ADIS16209_XACCL_NULL_REG },
	[ADIS16209_SCAN_ACC_Y] = { ADIS16209_YACCL_NULL_REG },
	[ADIS16209_SCAN_INCLI_X] = { ADIS16209_XINCL_NULL_REG },
	[ADIS16209_SCAN_INCLI_Y] = { ADIS16209_YINCL_NULL_REG },
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
	int m;

	if (mask != IIO_CHAN_INFO_CALIBBIAS)
		return -EINVAL;

	switch (chan->type) {
	case IIO_ACCEL:
	case IIO_INCLI:
		m = GENMASK(13, 0);
		break;
	default:
		return -EINVAL;
	}

	return adis_write_reg_16(st, adis16209_addresses[chan->scan_index][0],
				 val & m);
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
			switch (chan->channel) {
			case 0:
				*val2 = 305180; /* 0.30518 mV */
				break;
			case 1:
				*val2 = 610500; /* 0.6105 mV */
				break;
			default:
				return -EINVAL;
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
			*val2 = IIO_G_TO_M_S_2(244140);
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_INCLI:
		case IIO_ROT:
			/*
			 * IIO base units for rotation are degrees.
			 * 1 LSB represents 0.025 milli degrees.
			 */
			*val = 0;
			*val2 = 25000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * The raw ADC value is 0x4FE when the temperature
		 * is 45 degrees and the scale factor per milli
		 * degree celcius is -470.
		 */
		*val = 25000 / -470 - 0x4FE;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ACCEL:
			bits = 14;
			break;
		default:
			return -EINVAL;
		}
		addr = adis16209_addresses[chan->scan_index][0];
		ret = adis_read_reg_16(st, addr, &val16);
		if (ret)
			return ret;

		*val = sign_extend32(val16, bits - 1);
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static const struct iio_chan_spec adis16209_channels[] = {
	ADIS_SUPPLY_CHAN(ADIS16209_SUPPLY_OUT_REG, ADIS16209_SCAN_SUPPLY,
			 0, 14),
	ADIS_TEMP_CHAN(ADIS16209_TEMP_OUT_REG, ADIS16209_SCAN_TEMP, 0, 12),
	ADIS_ACCEL_CHAN(X, ADIS16209_XACCL_OUT_REG, ADIS16209_SCAN_ACC_X,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_ACCEL_CHAN(Y, ADIS16209_YACCL_OUT_REG, ADIS16209_SCAN_ACC_Y,
			BIT(IIO_CHAN_INFO_CALIBBIAS), 0, 14),
	ADIS_AUX_ADC_CHAN(ADIS16209_AUX_ADC_REG, ADIS16209_SCAN_AUX_ADC, 0, 12),
	ADIS_INCLI_CHAN(X, ADIS16209_XINCL_OUT_REG, ADIS16209_SCAN_INCLI_X,
			0, 0, 14),
	ADIS_INCLI_CHAN(Y, ADIS16209_YINCL_OUT_REG, ADIS16209_SCAN_INCLI_Y,
			0, 0, 14),
	ADIS_ROT_CHAN(X, ADIS16209_ROT_OUT_REG, ADIS16209_SCAN_ROT, 0, 0, 14),
	IIO_CHAN_SOFT_TIMESTAMP(8)
};

static const struct iio_info adis16209_info = {
	.read_raw = adis16209_read_raw,
	.write_raw = adis16209_write_raw,
	.update_scan_mode = adis_update_scan_mode,
};

static const char * const adis16209_status_error_msgs[] = {
	[ADIS16209_STAT_SELFTEST_FAIL_BIT] = "Self test failure",
	[ADIS16209_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16209_STAT_FLASH_UPT_FAIL_BIT] = "Flash update failed",
	[ADIS16209_STAT_POWER_HIGH_BIT] = "Power supply above 3.625V",
	[ADIS16209_STAT_POWER_LOW_BIT] = "Power supply below 2.975V",
};

static const struct adis_timeout adis16209_timeouts = {
	.reset_ms = ADIS16209_STARTUP_DELAY_MS,
	.self_test_ms = ADIS16209_STARTUP_DELAY_MS,
	.sw_reset_ms = ADIS16209_STARTUP_DELAY_MS,
};

static const struct adis_data adis16209_data = {
	.read_delay = 30,
	.msc_ctrl_reg = ADIS16209_MSC_CTRL_REG,
	.glob_cmd_reg = ADIS16209_CMD_REG,
	.diag_stat_reg = ADIS16209_STAT_REG,

	.self_test_mask = ADIS16209_MSC_CTRL_SELF_TEST_EN,
	.self_test_reg = ADIS16209_MSC_CTRL_REG,
	.self_test_no_autoclear = true,
	.timeouts = &adis16209_timeouts,

	.status_error_msgs = adis16209_status_error_msgs,
	.status_error_mask = BIT(ADIS16209_STAT_SELFTEST_FAIL_BIT) |
		BIT(ADIS16209_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16209_STAT_FLASH_UPT_FAIL_BIT) |
		BIT(ADIS16209_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16209_STAT_POWER_LOW_BIT),
};

static int adis16209_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adis *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->info = &adis16209_info;
	indio_dev->channels = adis16209_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16209_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(st, indio_dev, spi, &adis16209_data);
	if (ret)
		return ret;

	ret = devm_adis_setup_buffer_and_trigger(st, indio_dev, NULL);
	if (ret)
		return ret;

	ret = __adis_initial_startup(st);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static struct spi_driver adis16209_driver = {
	.driver = {
		.name = "adis16209",
	},
	.probe = adis16209_probe,
};
module_spi_driver(adis16209_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16209 Dual-Axis Digital Inclinometer and Accelerometer");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16209");
MODULE_IMPORT_NS(IIO_ADISLIB);
