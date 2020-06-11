// SPDX-License-Identifier: GPL-2.0-only
/*
 * adis16400.c	support Analog Devices ADIS16400/5
 *		3d 2g Linear Accelerometers,
 *		3d Gyroscopes,
 *		3d Magnetometers via SPI
 *
 * Copyright (c) 2009 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2007 Jonathan Cameron <jic23@kernel.org>
 * Copyright (c) 2011 Analog Devices Inc.
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
#include <linux/debugfs.h>
#include <linux/bitops.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/imu/adis.h>

#define ADIS16400_STARTUP_DELAY	290 /* ms */
#define ADIS16400_MTEST_DELAY 90 /* ms */

#define ADIS16400_FLASH_CNT  0x00 /* Flash memory write count */
#define ADIS16400_SUPPLY_OUT 0x02 /* Power supply measurement */
#define ADIS16400_XGYRO_OUT 0x04 /* X-axis gyroscope output */
#define ADIS16400_YGYRO_OUT 0x06 /* Y-axis gyroscope output */
#define ADIS16400_ZGYRO_OUT 0x08 /* Z-axis gyroscope output */
#define ADIS16400_XACCL_OUT 0x0A /* X-axis accelerometer output */
#define ADIS16400_YACCL_OUT 0x0C /* Y-axis accelerometer output */
#define ADIS16400_ZACCL_OUT 0x0E /* Z-axis accelerometer output */
#define ADIS16400_XMAGN_OUT 0x10 /* X-axis magnetometer measurement */
#define ADIS16400_YMAGN_OUT 0x12 /* Y-axis magnetometer measurement */
#define ADIS16400_ZMAGN_OUT 0x14 /* Z-axis magnetometer measurement */
#define ADIS16400_TEMP_OUT  0x16 /* Temperature output */
#define ADIS16400_AUX_ADC   0x18 /* Auxiliary ADC measurement */

#define ADIS16350_XTEMP_OUT 0x10 /* X-axis gyroscope temperature measurement */
#define ADIS16350_YTEMP_OUT 0x12 /* Y-axis gyroscope temperature measurement */
#define ADIS16350_ZTEMP_OUT 0x14 /* Z-axis gyroscope temperature measurement */

#define ADIS16300_PITCH_OUT 0x12 /* X axis inclinometer output measurement */
#define ADIS16300_ROLL_OUT  0x14 /* Y axis inclinometer output measurement */
#define ADIS16300_AUX_ADC   0x16 /* Auxiliary ADC measurement */

#define ADIS16448_BARO_OUT	0x16 /* Barometric pressure output */
#define ADIS16448_TEMP_OUT  0x18 /* Temperature output */

/* Calibration parameters */
#define ADIS16400_XGYRO_OFF 0x1A /* X-axis gyroscope bias offset factor */
#define ADIS16400_YGYRO_OFF 0x1C /* Y-axis gyroscope bias offset factor */
#define ADIS16400_ZGYRO_OFF 0x1E /* Z-axis gyroscope bias offset factor */
#define ADIS16400_XACCL_OFF 0x20 /* X-axis acceleration bias offset factor */
#define ADIS16400_YACCL_OFF 0x22 /* Y-axis acceleration bias offset factor */
#define ADIS16400_ZACCL_OFF 0x24 /* Z-axis acceleration bias offset factor */
#define ADIS16400_XMAGN_HIF 0x26 /* X-axis magnetometer, hard-iron factor */
#define ADIS16400_YMAGN_HIF 0x28 /* Y-axis magnetometer, hard-iron factor */
#define ADIS16400_ZMAGN_HIF 0x2A /* Z-axis magnetometer, hard-iron factor */
#define ADIS16400_XMAGN_SIF 0x2C /* X-axis magnetometer, soft-iron factor */
#define ADIS16400_YMAGN_SIF 0x2E /* Y-axis magnetometer, soft-iron factor */
#define ADIS16400_ZMAGN_SIF 0x30 /* Z-axis magnetometer, soft-iron factor */

#define ADIS16400_GPIO_CTRL 0x32 /* Auxiliary digital input/output control */
#define ADIS16400_MSC_CTRL  0x34 /* Miscellaneous control */
#define ADIS16400_SMPL_PRD  0x36 /* Internal sample period (rate) control */
#define ADIS16400_SENS_AVG  0x38 /* Dynamic range and digital filter control */
#define ADIS16400_SLP_CNT   0x3A /* Sleep mode control */
#define ADIS16400_DIAG_STAT 0x3C /* System status */

/* Alarm functions */
#define ADIS16400_GLOB_CMD  0x3E /* System command */
#define ADIS16400_ALM_MAG1  0x40 /* Alarm 1 amplitude threshold */
#define ADIS16400_ALM_MAG2  0x42 /* Alarm 2 amplitude threshold */
#define ADIS16400_ALM_SMPL1 0x44 /* Alarm 1 sample size */
#define ADIS16400_ALM_SMPL2 0x46 /* Alarm 2 sample size */
#define ADIS16400_ALM_CTRL  0x48 /* Alarm control */
#define ADIS16400_AUX_DAC   0x4A /* Auxiliary DAC data */

#define ADIS16334_LOT_ID1   0x52 /* Lot identification code 1 */
#define ADIS16334_LOT_ID2   0x54 /* Lot identification code 2 */
#define ADIS16400_PRODUCT_ID 0x56 /* Product identifier */
#define ADIS16334_SERIAL_NUMBER 0x58 /* Serial number, lot specific */

#define ADIS16400_ERROR_ACTIVE			(1<<14)
#define ADIS16400_NEW_DATA			(1<<14)

/* MSC_CTRL */
#define ADIS16400_MSC_CTRL_MEM_TEST		(1<<11)
#define ADIS16400_MSC_CTRL_INT_SELF_TEST	(1<<10)
#define ADIS16400_MSC_CTRL_NEG_SELF_TEST	(1<<9)
#define ADIS16400_MSC_CTRL_POS_SELF_TEST	(1<<8)
#define ADIS16400_MSC_CTRL_GYRO_BIAS		(1<<7)
#define ADIS16400_MSC_CTRL_ACCL_ALIGN		(1<<6)
#define ADIS16400_MSC_CTRL_DATA_RDY_EN		(1<<2)
#define ADIS16400_MSC_CTRL_DATA_RDY_POL_HIGH	(1<<1)
#define ADIS16400_MSC_CTRL_DATA_RDY_DIO2	(1<<0)

/* SMPL_PRD */
#define ADIS16400_SMPL_PRD_TIME_BASE	(1<<7)
#define ADIS16400_SMPL_PRD_DIV_MASK	0x7F

/* DIAG_STAT */
#define ADIS16400_DIAG_STAT_ZACCL_FAIL	15
#define ADIS16400_DIAG_STAT_YACCL_FAIL	14
#define ADIS16400_DIAG_STAT_XACCL_FAIL	13
#define ADIS16400_DIAG_STAT_XGYRO_FAIL	12
#define ADIS16400_DIAG_STAT_YGYRO_FAIL	11
#define ADIS16400_DIAG_STAT_ZGYRO_FAIL	10
#define ADIS16400_DIAG_STAT_ALARM2	9
#define ADIS16400_DIAG_STAT_ALARM1	8
#define ADIS16400_DIAG_STAT_FLASH_CHK	6
#define ADIS16400_DIAG_STAT_SELF_TEST	5
#define ADIS16400_DIAG_STAT_OVERFLOW	4
#define ADIS16400_DIAG_STAT_SPI_FAIL	3
#define ADIS16400_DIAG_STAT_FLASH_UPT	2
#define ADIS16400_DIAG_STAT_POWER_HIGH	1
#define ADIS16400_DIAG_STAT_POWER_LOW	0

/* GLOB_CMD */
#define ADIS16400_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16400_GLOB_CMD_P_AUTO_NULL	(1<<4)
#define ADIS16400_GLOB_CMD_FLASH_UPD	(1<<3)
#define ADIS16400_GLOB_CMD_DAC_LATCH	(1<<2)
#define ADIS16400_GLOB_CMD_FAC_CALIB	(1<<1)
#define ADIS16400_GLOB_CMD_AUTO_NULL	(1<<0)

/* SLP_CNT */
#define ADIS16400_SLP_CNT_POWER_OFF	(1<<8)

#define ADIS16334_RATE_DIV_SHIFT 8
#define ADIS16334_RATE_INT_CLK BIT(0)

#define ADIS16400_SPI_SLOW	(u32)(300 * 1000)
#define ADIS16400_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16400_SPI_FAST	(u32)(2000 * 1000)

#define ADIS16400_HAS_PROD_ID		BIT(0)
#define ADIS16400_NO_BURST		BIT(1)
#define ADIS16400_HAS_SLOW_MODE		BIT(2)
#define ADIS16400_HAS_SERIAL_NUMBER	BIT(3)
#define ADIS16400_BURST_DIAG_STAT	BIT(4)

struct adis16400_state;

struct adis16400_chip_info {
	const struct iio_chan_spec *channels;
	const struct adis_data adis_data;
	const int num_channels;
	const long flags;
	unsigned int gyro_scale_micro;
	unsigned int accel_scale_micro;
	int temp_scale_nano;
	int temp_offset;
	/* set_freq() & get_freq() need to avoid using ADIS lib's state lock */
	int (*set_freq)(struct adis16400_state *st, unsigned int freq);
	int (*get_freq)(struct adis16400_state *st);
};

/**
 * struct adis16400_state - device instance specific data
 * @variant:	chip variant info
 * @filt_int:	integer part of requested filter frequency
 * @adis:	adis device
 **/
struct adis16400_state {
	struct adis16400_chip_info	*variant;
	int				filt_int;

	struct adis adis;
	unsigned long avail_scan_mask[2];
};

/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum {
	ADIS16400_SCAN_SUPPLY,
	ADIS16400_SCAN_GYRO_X,
	ADIS16400_SCAN_GYRO_Y,
	ADIS16400_SCAN_GYRO_Z,
	ADIS16400_SCAN_ACC_X,
	ADIS16400_SCAN_ACC_Y,
	ADIS16400_SCAN_ACC_Z,
	ADIS16400_SCAN_MAGN_X,
	ADIS16400_SCAN_MAGN_Y,
	ADIS16400_SCAN_MAGN_Z,
	ADIS16400_SCAN_BARO,
	ADIS16350_SCAN_TEMP_X,
	ADIS16350_SCAN_TEMP_Y,
	ADIS16350_SCAN_TEMP_Z,
	ADIS16300_SCAN_INCLI_X,
	ADIS16300_SCAN_INCLI_Y,
	ADIS16400_SCAN_ADC,
	ADIS16400_SCAN_TIMESTAMP,
};

#ifdef CONFIG_DEBUG_FS

static ssize_t adis16400_show_serial_number(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct adis16400_state *st = file->private_data;
	u16 lot1, lot2, serial_number;
	char buf[16];
	size_t len;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_LOT_ID1, &lot1);
	if (ret)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_LOT_ID2, &lot2);
	if (ret)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_SERIAL_NUMBER,
			&serial_number);
	if (ret)
		return ret;

	len = snprintf(buf, sizeof(buf), "%.4x-%.4x-%.4x\n", lot1, lot2,
			serial_number);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16400_serial_number_fops = {
	.open = simple_open,
	.read = adis16400_show_serial_number,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static int adis16400_show_product_id(void *arg, u64 *val)
{
	struct adis16400_state *st = arg;
	uint16_t prod_id;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16400_PRODUCT_ID, &prod_id);
	if (ret)
		return ret;

	*val = prod_id;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16400_product_id_fops,
	adis16400_show_product_id, NULL, "%lld\n");

static int adis16400_show_flash_count(void *arg, u64 *val)
{
	struct adis16400_state *st = arg;
	uint16_t flash_count;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16400_FLASH_CNT, &flash_count);
	if (ret)
		return ret;

	*val = flash_count;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16400_flash_count_fops,
	adis16400_show_flash_count, NULL, "%lld\n");

static int adis16400_debugfs_init(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	struct dentry *d = iio_get_debugfs_dentry(indio_dev);

	if (st->variant->flags & ADIS16400_HAS_SERIAL_NUMBER)
		debugfs_create_file_unsafe("serial_number", 0400,
				d, st, &adis16400_serial_number_fops);
	if (st->variant->flags & ADIS16400_HAS_PROD_ID)
		debugfs_create_file_unsafe("product_id", 0400,
				d, st, &adis16400_product_id_fops);
	debugfs_create_file_unsafe("flash_count", 0400,
			d, st, &adis16400_flash_count_fops);

	return 0;
}

#else

static int adis16400_debugfs_init(struct iio_dev *indio_dev)
{
	return 0;
}

#endif

enum adis16400_chip_variant {
	ADIS16300,
	ADIS16334,
	ADIS16350,
	ADIS16360,
	ADIS16362,
	ADIS16364,
	ADIS16367,
	ADIS16400,
	ADIS16445,
	ADIS16448,
};

static struct adis_burst adis16400_burst = {
	.en = true,
	.reg_cmd = ADIS16400_GLOB_CMD,
};

static int adis16334_get_freq(struct adis16400_state *st)
{
	int ret;
	uint16_t t;

	ret = __adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &t);
	if (ret)
		return ret;

	t >>= ADIS16334_RATE_DIV_SHIFT;

	return 819200 >> t;
}

static int adis16334_set_freq(struct adis16400_state *st, unsigned int freq)
{
	unsigned int t;

	if (freq < 819200)
		t = ilog2(819200 / freq);
	else
		t = 0;

	if (t > 0x31)
		t = 0x31;

	t <<= ADIS16334_RATE_DIV_SHIFT;
	t |= ADIS16334_RATE_INT_CLK;

	return __adis_write_reg_16(&st->adis, ADIS16400_SMPL_PRD, t);
}

static int adis16400_get_freq(struct adis16400_state *st)
{
	int sps, ret;
	uint16_t t;

	ret = __adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &t);
	if (ret)
		return ret;

	sps = (t & ADIS16400_SMPL_PRD_TIME_BASE) ? 52851 : 1638404;
	sps /= (t & ADIS16400_SMPL_PRD_DIV_MASK) + 1;

	return sps;
}

static int adis16400_set_freq(struct adis16400_state *st, unsigned int freq)
{
	unsigned int t;
	uint8_t val = 0;

	t = 1638404 / freq;
	if (t >= 128) {
		val |= ADIS16400_SMPL_PRD_TIME_BASE;
		t = 52851 / freq;
		if (t >= 128)
			t = 127;
	} else if (t != 0) {
		t--;
	}

	val |= t;

	if (t >= 0x0A || (val & ADIS16400_SMPL_PRD_TIME_BASE))
		st->adis.spi->max_speed_hz = ADIS16400_SPI_SLOW;
	else
		st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;

	return __adis_write_reg_8(&st->adis, ADIS16400_SMPL_PRD, val);
}

static const unsigned int adis16400_3db_divisors[] = {
	[0] = 2, /* Special case */
	[1] = 6,
	[2] = 12,
	[3] = 25,
	[4] = 50,
	[5] = 100,
	[6] = 200,
	[7] = 200, /* Not a valid setting */
};

static int __adis16400_set_filter(struct iio_dev *indio_dev, int sps, int val)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	uint16_t val16;
	int i, ret;

	for (i = ARRAY_SIZE(adis16400_3db_divisors) - 1; i >= 1; i--) {
		if (sps / adis16400_3db_divisors[i] >= val)
			break;
	}

	ret = __adis_read_reg_16(&st->adis, ADIS16400_SENS_AVG, &val16);
	if (ret)
		return ret;

	ret = __adis_write_reg_16(&st->adis, ADIS16400_SENS_AVG,
					 (val16 & ~0x07) | i);
	return ret;
}

/* Power down the device */
static int adis16400_stop_device(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int ret;

	ret = adis_write_reg_16(&st->adis, ADIS16400_SLP_CNT,
			ADIS16400_SLP_CNT_POWER_OFF);
	if (ret)
		dev_err(&indio_dev->dev,
			"problem with turning device off: SLP_CNT");

	return ret;
}

static int adis16400_initial_setup(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	uint16_t prod_id, smp_prd;
	unsigned int device_id;
	int ret;

	/* use low spi speed for init if the device has a slow mode */
	if (st->variant->flags & ADIS16400_HAS_SLOW_MODE)
		st->adis.spi->max_speed_hz = ADIS16400_SPI_SLOW;
	else
		st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;
	st->adis.spi->mode = SPI_MODE_3;
	spi_setup(st->adis.spi);

	ret = adis_initial_startup(&st->adis);
	if (ret)
		return ret;

	if (st->variant->flags & ADIS16400_HAS_PROD_ID) {
		ret = adis_read_reg_16(&st->adis,
						ADIS16400_PRODUCT_ID, &prod_id);
		if (ret)
			goto err_ret;

		ret = sscanf(indio_dev->name, "adis%u\n", &device_id);
		if (ret != 1) {
			ret = -EINVAL;
			goto err_ret;
		}

		if (prod_id != device_id)
			dev_warn(&indio_dev->dev, "Device ID(%u) and product ID(%u) do not match.",
					device_id, prod_id);

		dev_info(&indio_dev->dev, "%s: prod_id 0x%04x at CS%d (irq %d)\n",
			indio_dev->name, prod_id,
			st->adis.spi->chip_select, st->adis.spi->irq);
	}
	/* use high spi speed if possible */
	if (st->variant->flags & ADIS16400_HAS_SLOW_MODE) {
		ret = adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &smp_prd);
		if (ret)
			goto err_ret;

		if ((smp_prd & ADIS16400_SMPL_PRD_DIV_MASK) < 0x0A) {
			st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;
			spi_setup(st->adis.spi);
		}
	}

err_ret:
	return ret;
}

static const uint8_t adis16400_addresses[] = {
	[ADIS16400_SCAN_GYRO_X] = ADIS16400_XGYRO_OFF,
	[ADIS16400_SCAN_GYRO_Y] = ADIS16400_YGYRO_OFF,
	[ADIS16400_SCAN_GYRO_Z] = ADIS16400_ZGYRO_OFF,
	[ADIS16400_SCAN_ACC_X] = ADIS16400_XACCL_OFF,
	[ADIS16400_SCAN_ACC_Y] = ADIS16400_YACCL_OFF,
	[ADIS16400_SCAN_ACC_Z] = ADIS16400_ZACCL_OFF,
};

static int adis16400_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long info)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	struct mutex *slock = &st->adis.state_lock;
	int ret, sps;

	switch (info) {
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = adis_write_reg_16(&st->adis,
				adis16400_addresses[chan->scan_index], val);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		/*
		 * Need to cache values so we can update if the frequency
		 * changes.
		 */
		mutex_lock(slock);
		st->filt_int = val;
		/* Work out update to current value */
		sps = st->variant->get_freq(st);
		if (sps < 0) {
			mutex_unlock(slock);
			return sps;
		}

		ret = __adis16400_set_filter(indio_dev, sps,
			val * 1000 + val2 / 1000);
		mutex_unlock(slock);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		sps = val * 1000 + val2 / 1000;

		if (sps <= 0)
			return -EINVAL;

		mutex_lock(slock);
		ret = st->variant->set_freq(st, sps);
		mutex_unlock(slock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int adis16400_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	struct mutex *slock = &st->adis.state_lock;
	int16_t val16;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan, 0, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = st->variant->gyro_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_VOLTAGE:
			*val = 0;
			if (chan->channel == 0) {
				*val = 2;
				*val2 = 418000; /* 2.418 mV */
			} else {
				*val = 0;
				*val2 = 805800; /* 805.8 uV */
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = st->variant->accel_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MAGN:
			*val = 0;
			*val2 = 500; /* 0.5 mgauss */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = st->variant->temp_scale_nano / 1000000;
			*val2 = (st->variant->temp_scale_nano % 1000000);
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_PRESSURE:
			/* 20 uBar = 0.002kPascal */
			*val = 0;
			*val2 = 2000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = adis_read_reg_16(&st->adis,
				adis16400_addresses[chan->scan_index], &val16);
		if (ret)
			return ret;
		val16 = sign_extend32(val16, 11);
		*val = val16;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		/* currently only temperature */
		*val = st->variant->temp_offset;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		mutex_lock(slock);
		/* Need both the number of taps and the sampling frequency */
		ret = __adis_read_reg_16(&st->adis,
						ADIS16400_SENS_AVG,
						&val16);
		if (ret) {
			mutex_unlock(slock);
			return ret;
		}
		ret = st->variant->get_freq(st);
		mutex_unlock(slock);
		if (ret)
			return ret;
		ret /= adis16400_3db_divisors[val16 & 0x07];
		*val = ret / 1000;
		*val2 = (ret % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(slock);
		ret = st->variant->get_freq(st);
		mutex_unlock(slock);
		if (ret)
			return ret;
		*val = ret / 1000;
		*val2 = (ret % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

#if IS_ENABLED(CONFIG_IIO_BUFFER)
static irqreturn_t adis16400_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adis16400_state *st = iio_priv(indio_dev);
	struct adis *adis = &st->adis;
	u32 old_speed_hz = st->adis.spi->max_speed_hz;
	void *buffer;
	int ret;

	if (!adis->buffer)
		return -ENOMEM;

	if (!(st->variant->flags & ADIS16400_NO_BURST) &&
		st->adis.spi->max_speed_hz > ADIS16400_SPI_BURST) {
		st->adis.spi->max_speed_hz = ADIS16400_SPI_BURST;
		spi_setup(st->adis.spi);
	}

	ret = spi_sync(adis->spi, &adis->msg);
	if (ret)
		dev_err(&adis->spi->dev, "Failed to read data: %d\n", ret);

	if (!(st->variant->flags & ADIS16400_NO_BURST)) {
		st->adis.spi->max_speed_hz = old_speed_hz;
		spi_setup(st->adis.spi);
	}

	if (st->variant->flags & ADIS16400_BURST_DIAG_STAT)
		buffer = adis->buffer + sizeof(u16);
	else
		buffer = adis->buffer;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
		pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
#else
#define adis16400_trigger_handler	NULL
#endif /* IS_ENABLED(CONFIG_IIO_BUFFER) */

#define ADIS16400_VOLTAGE_CHAN(addr, bits, name, si, chn) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = chn, \
	.extend_name = name, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_SUPPLY_CHAN(addr, bits) \
	ADIS16400_VOLTAGE_CHAN(addr, bits, "supply", ADIS16400_SCAN_SUPPLY, 0)

#define ADIS16400_AUX_ADC_CHAN(addr, bits) \
	ADIS16400_VOLTAGE_CHAN(addr, bits, NULL, ADIS16400_SCAN_ADC, 1)

#define ADIS16400_GYRO_CHAN(mod, addr, bits) { \
	.type = IIO_ANGL_VEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS),		  \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = addr, \
	.scan_index = ADIS16400_SCAN_GYRO_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_ACCEL_CHAN(mod, addr, bits) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16400_SCAN_ACC_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_MAGN_CHAN(mod, addr, bits) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16400_SCAN_MAGN_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_MOD_TEMP_NAME_X "x"
#define ADIS16400_MOD_TEMP_NAME_Y "y"
#define ADIS16400_MOD_TEMP_NAME_Z "z"

#define ADIS16400_MOD_TEMP_CHAN(mod, addr, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.extend_name = ADIS16400_MOD_TEMP_NAME_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_OFFSET) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_type = \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16350_SCAN_TEMP_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_TEMP_CHAN(addr, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_OFFSET) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16350_SCAN_TEMP_X, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_INCLI_CHAN(mod, addr, bits) { \
	.type = IIO_INCLI, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16300_SCAN_INCLI_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

static const struct iio_chan_spec adis16400_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 14),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16400_TEMP_OUT, 12),
	ADIS16400_AUX_ADC_CHAN(ADIS16400_AUX_ADC, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16445_channels[] = {
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 16),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 16),
	ADIS16400_TEMP_CHAN(ADIS16448_TEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16448_channels[] = {
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 16),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 16),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 16),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 16),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 16),
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = ADIS16448_BARO_OUT,
		.scan_index = ADIS16400_SCAN_BARO,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	ADIS16400_TEMP_CHAN(ADIS16448_TEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16350_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 12),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 14),
	ADIS16400_AUX_ADC_CHAN(ADIS16300_AUX_ADC, 12),
	ADIS16400_MOD_TEMP_CHAN(X, ADIS16350_XTEMP_OUT, 12),
	ADIS16400_MOD_TEMP_CHAN(Y, ADIS16350_YTEMP_OUT, 12),
	ADIS16400_MOD_TEMP_CHAN(Z, ADIS16350_ZTEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16300_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 12),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16350_XTEMP_OUT, 12),
	ADIS16400_AUX_ADC_CHAN(ADIS16300_AUX_ADC, 12),
	ADIS16400_INCLI_CHAN(X, ADIS16300_PITCH_OUT, 13),
	ADIS16400_INCLI_CHAN(Y, ADIS16300_ROLL_OUT, 13),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16334_channels[] = {
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16350_XTEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const char * const adis16400_status_error_msgs[] = {
	[ADIS16400_DIAG_STAT_ZACCL_FAIL] = "Z-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_YACCL_FAIL] = "Y-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_XACCL_FAIL] = "X-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_XGYRO_FAIL] = "X-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_YGYRO_FAIL] = "Y-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_ZGYRO_FAIL] = "Z-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_ALARM2] = "Alarm 2 active",
	[ADIS16400_DIAG_STAT_ALARM1] = "Alarm 1 active",
	[ADIS16400_DIAG_STAT_FLASH_CHK] = "Flash checksum error",
	[ADIS16400_DIAG_STAT_SELF_TEST] = "Self test error",
	[ADIS16400_DIAG_STAT_OVERFLOW] = "Sensor overrange",
	[ADIS16400_DIAG_STAT_SPI_FAIL] = "SPI failure",
	[ADIS16400_DIAG_STAT_FLASH_UPT] = "Flash update failed",
	[ADIS16400_DIAG_STAT_POWER_HIGH] = "Power supply above 5.25V",
	[ADIS16400_DIAG_STAT_POWER_LOW] = "Power supply below 4.75V",
};

#define ADIS16400_DATA(_timeouts)					\
{									\
	.msc_ctrl_reg = ADIS16400_MSC_CTRL,				\
	.glob_cmd_reg = ADIS16400_GLOB_CMD,				\
	.diag_stat_reg = ADIS16400_DIAG_STAT,				\
	.read_delay = 50,						\
	.write_delay = 50,						\
	.self_test_mask = ADIS16400_MSC_CTRL_MEM_TEST,			\
	.self_test_reg = ADIS16400_MSC_CTRL,				\
	.status_error_msgs = adis16400_status_error_msgs,		\
	.status_error_mask = BIT(ADIS16400_DIAG_STAT_ZACCL_FAIL) |	\
		BIT(ADIS16400_DIAG_STAT_YACCL_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_XACCL_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_XGYRO_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_YGYRO_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_ZGYRO_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_ALARM2) |			\
		BIT(ADIS16400_DIAG_STAT_ALARM1) |			\
		BIT(ADIS16400_DIAG_STAT_FLASH_CHK) |			\
		BIT(ADIS16400_DIAG_STAT_SELF_TEST) |			\
		BIT(ADIS16400_DIAG_STAT_OVERFLOW) |			\
		BIT(ADIS16400_DIAG_STAT_SPI_FAIL) |			\
		BIT(ADIS16400_DIAG_STAT_FLASH_UPT) |			\
		BIT(ADIS16400_DIAG_STAT_POWER_HIGH) |			\
		BIT(ADIS16400_DIAG_STAT_POWER_LOW),			\
	.timeouts = (_timeouts),					\
}

static const struct adis_timeout adis16300_timeouts = {
	.reset_ms = ADIS16400_STARTUP_DELAY,
	.sw_reset_ms = ADIS16400_STARTUP_DELAY,
	.self_test_ms = ADIS16400_STARTUP_DELAY,
};

static const struct adis_timeout adis16334_timeouts = {
	.reset_ms = 60,
	.sw_reset_ms = 60,
	.self_test_ms = 14,
};

static const struct adis_timeout adis16362_timeouts = {
	.reset_ms = 130,
	.sw_reset_ms = 130,
	.self_test_ms = 12,
};

static const struct adis_timeout adis16400_timeouts = {
	.reset_ms = 170,
	.sw_reset_ms = 170,
	.self_test_ms = 12,
};

static const struct adis_timeout adis16445_timeouts = {
	.reset_ms = 55,
	.sw_reset_ms = 55,
	.self_test_ms = 16,
};

static const struct adis_timeout adis16448_timeouts = {
	.reset_ms = 90,
	.sw_reset_ms = 90,
	.self_test_ms = 45,
};

static struct adis16400_chip_info adis16400_chips[] = {
	[ADIS16300] = {
		.channels = adis16300_channels,
		.num_channels = ARRAY_SIZE(adis16300_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = 5884,
		.temp_scale_nano = 140000000, /* 0.14 C */
		.temp_offset = 25000000 / 140000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16300_timeouts),
	},
	[ADIS16334] = {
		.channels = adis16334_channels,
		.num_channels = ARRAY_SIZE(adis16334_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_NO_BURST |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(1000), /* 1 mg */
		.temp_scale_nano = 67850000, /* 0.06785 C */
		.temp_offset = 25000000 / 67850, /* 25 C = 0x00 */
		.set_freq = adis16334_set_freq,
		.get_freq = adis16334_get_freq,
		.adis_data = ADIS16400_DATA(&adis16334_timeouts),
	},
	[ADIS16350] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(73260), /* 0.07326 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(2522), /* 0.002522 g */
		.temp_scale_nano = 145300000, /* 0.1453 C */
		.temp_offset = 25000000 / 145300, /* 25 C = 0x00 */
		.flags = ADIS16400_NO_BURST | ADIS16400_HAS_SLOW_MODE,
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16300_timeouts),
	},
	[ADIS16360] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(3333), /* 3.333 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16300_timeouts),
	},
	[ADIS16362] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(333), /* 0.333 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16362_timeouts),
	},
	[ADIS16364] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(1000), /* 1 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16362_timeouts),
	},
	[ADIS16367] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(2000), /* 0.2 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(3333), /* 3.333 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16300_timeouts),
	},
	[ADIS16400] = {
		.channels = adis16400_channels,
		.num_channels = ARRAY_SIZE(adis16400_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(3333), /* 3.333 mg */
		.temp_scale_nano = 140000000, /* 0.14 C */
		.temp_offset = 25000000 / 140000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
		.adis_data = ADIS16400_DATA(&adis16400_timeouts),
	},
	[ADIS16445] = {
		.channels = adis16445_channels,
		.num_channels = ARRAY_SIZE(adis16445_channels),
		.flags = ADIS16400_HAS_PROD_ID |
				ADIS16400_HAS_SERIAL_NUMBER |
				ADIS16400_BURST_DIAG_STAT,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(10000), /* 0.01 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(250), /* 1/4000 g */
		.temp_scale_nano = 73860000, /* 0.07386 C */
		.temp_offset = 31000000 / 73860, /* 31 C = 0x00 */
		.set_freq = adis16334_set_freq,
		.get_freq = adis16334_get_freq,
		.adis_data = ADIS16400_DATA(&adis16445_timeouts),
	},
	[ADIS16448] = {
		.channels = adis16448_channels,
		.num_channels = ARRAY_SIZE(adis16448_channels),
		.flags = ADIS16400_HAS_PROD_ID |
				ADIS16400_HAS_SERIAL_NUMBER |
				ADIS16400_BURST_DIAG_STAT,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(40000), /* 0.04 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(833), /* 1/1200 g */
		.temp_scale_nano = 73860000, /* 0.07386 C */
		.temp_offset = 31000000 / 73860, /* 31 C = 0x00 */
		.set_freq = adis16334_set_freq,
		.get_freq = adis16334_get_freq,
		.adis_data = ADIS16400_DATA(&adis16448_timeouts),
	}
};

static const struct iio_info adis16400_info = {
	.read_raw = &adis16400_read_raw,
	.write_raw = &adis16400_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.debugfs_reg_access = adis_debugfs_reg_access,
};

static void adis16400_setup_chan_mask(struct adis16400_state *st)
{
	const struct adis16400_chip_info *chip_info = st->variant;
	unsigned int i;

	for (i = 0; i < chip_info->num_channels; i++) {
		const struct iio_chan_spec *ch = &chip_info->channels[i];

		if (ch->scan_index >= 0 &&
		    ch->scan_index != ADIS16400_SCAN_TIMESTAMP)
			st->avail_scan_mask[0] |= BIT(ch->scan_index);
	}
}
static int adis16400_probe(struct spi_device *spi)
{
	struct adis16400_state *st;
	struct iio_dev *indio_dev;
	int ret;
	const struct adis_data *adis16400_data;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	/* setup the industrialio driver allocated elements */
	st->variant = &adis16400_chips[spi_get_device_id(spi)->driver_data];
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = st->variant->channels;
	indio_dev->num_channels = st->variant->num_channels;
	indio_dev->info = &adis16400_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (!(st->variant->flags & ADIS16400_NO_BURST)) {
		adis16400_setup_chan_mask(st);
		indio_dev->available_scan_masks = st->avail_scan_mask;
		st->adis.burst = &adis16400_burst;
		if (st->variant->flags & ADIS16400_BURST_DIAG_STAT)
			st->adis.burst_extra_len = sizeof(u16);
	}

	adis16400_data = &st->variant->adis_data;

	ret = adis_init(&st->adis, indio_dev, spi, adis16400_data);
	if (ret)
		return ret;

	ret = adis_setup_buffer_and_trigger(&st->adis, indio_dev,
			adis16400_trigger_handler);
	if (ret)
		return ret;

	/* Get the device into a sane initial state */
	ret = adis16400_initial_setup(indio_dev);
	if (ret)
		goto error_cleanup_buffer;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer;

	adis16400_debugfs_init(indio_dev);
	return 0;

error_cleanup_buffer:
	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
	return ret;
}

static int adis16400_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis16400_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis16400_stop_device(indio_dev);

	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);

	return 0;
}

static const struct spi_device_id adis16400_id[] = {
	{"adis16300", ADIS16300},
	{"adis16305", ADIS16300},
	{"adis16334", ADIS16334},
	{"adis16350", ADIS16350},
	{"adis16354", ADIS16350},
	{"adis16355", ADIS16350},
	{"adis16360", ADIS16360},
	{"adis16362", ADIS16362},
	{"adis16364", ADIS16364},
	{"adis16365", ADIS16360},
	{"adis16367", ADIS16367},
	{"adis16400", ADIS16400},
	{"adis16405", ADIS16400},
	{"adis16445", ADIS16445},
	{"adis16448", ADIS16448},
	{}
};
MODULE_DEVICE_TABLE(spi, adis16400_id);

static struct spi_driver adis16400_driver = {
	.driver = {
		.name = "adis16400",
	},
	.id_table = adis16400_id,
	.probe = adis16400_probe,
	.remove = adis16400_remove,
};
module_spi_driver(adis16400_driver);

MODULE_AUTHOR("Manuel Stahl <manuel.stahl@iis.fraunhofer.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16400/5 IMU SPI driver");
MODULE_LICENSE("GPL v2");
