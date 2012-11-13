/*
 * ADIS16260/ADIS16265 Programmable Digital Gyroscope Sensor Driver
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

#include "adis16260.h"

static ssize_t adis16260_read_frequency_available(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16260_state *st = iio_priv(indio_dev);
	if (spi_get_device_id(st->adis.spi)->driver_data)
		return sprintf(buf, "%s\n", "0.129 ~ 256");
	else
		return sprintf(buf, "%s\n", "256 2048");
}

static ssize_t adis16260_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16260_state *st = iio_priv(indio_dev);
	int ret, len = 0;
	u16 t;
	int sps;
	ret = adis_read_reg_16(&st->adis, ADIS16260_SMPL_PRD, &t);
	if (ret)
		return ret;

	if (spi_get_device_id(st->adis.spi)->driver_data) /* If an adis16251 */
		sps =  (t & ADIS16260_SMPL_PRD_TIME_BASE) ? 8 : 256;
	else
		sps =  (t & ADIS16260_SMPL_PRD_TIME_BASE) ? 66 : 2048;
	sps /= (t & ADIS16260_SMPL_PRD_DIV_MASK) + 1;
	len = sprintf(buf, "%d SPS\n", sps);
	return len;
}

static ssize_t adis16260_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16260_state *st = iio_priv(indio_dev);
	long val;
	int ret;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;
	if (val == 0)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);
	if (spi_get_device_id(st->adis.spi)->driver_data) {
		t = (256 / val);
		if (t > 0)
			t--;
		t &= ADIS16260_SMPL_PRD_DIV_MASK;
	} else {
		t = (2048 / val);
		if (t > 0)
			t--;
		t &= ADIS16260_SMPL_PRD_DIV_MASK;
	}
	if ((t & ADIS16260_SMPL_PRD_DIV_MASK) >= 0x0A)
		st->adis.spi->max_speed_hz = ADIS16260_SPI_SLOW;
	else
		st->adis.spi->max_speed_hz = ADIS16260_SPI_FAST;
	ret = adis_write_reg_8(&st->adis,
			ADIS16260_SMPL_PRD,
			t);

	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

/* Power down the device */
static int adis16260_stop_device(struct iio_dev *indio_dev)
{
	struct adis16260_state *st = iio_priv(indio_dev);
	int ret;
	u16 val = ADIS16260_SLP_CNT_POWER_OFF;

	ret = adis_write_reg_16(&st->adis, ADIS16260_SLP_CNT, val);
	if (ret)
		dev_err(&indio_dev->dev, "problem with turning device off: SLP_CNT");

	return ret;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		adis16260_read_frequency,
		adis16260_write_frequency);

static IIO_DEVICE_ATTR(sampling_frequency_available,
		       S_IRUGO, adis16260_read_frequency_available, NULL, 0);

#define ADIS16260_GYRO_CHANNEL_SET(axis, mod)				\
struct iio_chan_spec adis16260_channels_##axis[] = {		\
	ADIS_GYRO_CHAN(mod, ADIS16260_GYRO_OUT, ADIS16260_SCAN_GYRO, \
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT | \
		IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT, 14), \
	ADIS_INCLI_CHAN(mod, ADIS16260_ANGL_OUT, ADIS16260_SCAN_ANGL, 0, 14), \
	ADIS_TEMP_CHAN(ADIS16260_TEMP_OUT, ADIS16260_SCAN_TEMP, 12), \
	ADIS_SUPPLY_CHAN(ADIS16260_SUPPLY_OUT, ADIS16260_SCAN_SUPPLY, 12), \
	ADIS_AUX_ADC_CHAN(ADIS16260_AUX_ADC, ADIS16260_SCAN_AUX_ADC, 12), \
	IIO_CHAN_SOFT_TIMESTAMP(5),				\
}

static const ADIS16260_GYRO_CHANNEL_SET(x, X);
static const ADIS16260_GYRO_CHANNEL_SET(y, Y);
static const ADIS16260_GYRO_CHANNEL_SET(z, Z);

static const u8 adis16260_addresses[][2] = {
	[ADIS16260_SCAN_GYRO] = { ADIS16260_GYRO_OFF, ADIS16260_GYRO_SCALE },
};

static int adis16260_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct adis16260_state *st = iio_priv(indio_dev);
	int ret;
	int bits;
	u8 addr;
	s16 val16;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan,
				ADIS16260_ERROR_ACTIVE, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = 0;
			if (spi_get_device_id(st->adis.spi)->driver_data) {
				/* 0.01832 degree / sec */
				*val2 = IIO_DEGREE_TO_RAD(18320);
			} else {
				/* 0.07326 degree / sec */
				*val2 = IIO_DEGREE_TO_RAD(73260);
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_VOLTAGE:
			if (chan->channel == 0) {
				*val = 1;
				*val2 = 831500; /* 1.8315 mV */
			} else {
				*val = 0;
				*val2 = 610500; /* 610.5 uV */
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 145;
			*val2 = 300000; /* 0.1453 C */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = 250000 / 1453; /* 25 C = 0x00 */
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			bits = 12;
			break;
		default:
			return -EINVAL;
		}
		mutex_lock(&indio_dev->mlock);
		addr = adis16260_addresses[chan->scan_index][0];
		ret = adis_read_reg_16(&st->adis, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		val16 &= (1 << bits) - 1;
		val16 = (s16)(val16 << (16 - bits)) >> (16 - bits);
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			bits = 12;
			break;
		default:
			return -EINVAL;
		}
		mutex_lock(&indio_dev->mlock);
		addr = adis16260_addresses[chan->scan_index][1];
		ret = adis_read_reg_16(&st->adis, addr, &val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		*val = (1 << bits) - 1;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static int adis16260_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct adis16260_state *st = iio_priv(indio_dev);
	int bits = 12;
	s16 val16;
	u8 addr;
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		val16 = val & ((1 << bits) - 1);
		addr = adis16260_addresses[chan->scan_index][0];
		return adis_write_reg_16(&st->adis, addr, val16);
	case IIO_CHAN_INFO_CALIBSCALE:
		val16 = val & ((1 << bits) - 1);
		addr = adis16260_addresses[chan->scan_index][1];
		return adis_write_reg_16(&st->adis, addr, val16);
	}
	return -EINVAL;
}

static struct attribute *adis16260_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16260_attribute_group = {
	.attrs = adis16260_attributes,
};

static const struct iio_info adis16260_info = {
	.attrs = &adis16260_attribute_group,
	.read_raw = &adis16260_read_raw,
	.write_raw = &adis16260_write_raw,
	.driver_module = THIS_MODULE,
};

static const char * const adis1620_status_error_msgs[] = {
	[ADIS16260_DIAG_STAT_FLASH_CHK_BIT] = "Flash checksum error",
	[ADIS16260_DIAG_STAT_SELF_TEST_BIT] = "Self test error",
	[ADIS16260_DIAG_STAT_OVERFLOW_BIT] = "Sensor overrange",
	[ADIS16260_DIAG_STAT_SPI_FAIL_BIT] = "SPI failure",
	[ADIS16260_DIAG_STAT_FLASH_UPT_BIT] = "Flash update failed",
	[ADIS16260_DIAG_STAT_POWER_HIGH_BIT] = "Power supply above 5.25",
	[ADIS16260_DIAG_STAT_POWER_LOW_BIT] = "Power supply below 4.75",
};

static const struct adis_data adis16260_data = {
	.write_delay = 30,
	.read_delay = 30,
	.msc_ctrl_reg = ADIS16260_MSC_CTRL,
	.glob_cmd_reg = ADIS16260_GLOB_CMD,
	.diag_stat_reg = ADIS16260_DIAG_STAT,

	.self_test_mask = ADIS16260_MSC_CTRL_MEM_TEST,
	.startup_delay = ADIS16260_STARTUP_DELAY,

	.status_error_msgs = adis1620_status_error_msgs,
	.status_error_mask = BIT(ADIS16260_DIAG_STAT_FLASH_CHK_BIT) |
		BIT(ADIS16260_DIAG_STAT_SELF_TEST_BIT) |
		BIT(ADIS16260_DIAG_STAT_OVERFLOW_BIT) |
		BIT(ADIS16260_DIAG_STAT_SPI_FAIL_BIT) |
		BIT(ADIS16260_DIAG_STAT_FLASH_UPT_BIT) |
		BIT(ADIS16260_DIAG_STAT_POWER_HIGH_BIT) |
		BIT(ADIS16260_DIAG_STAT_POWER_LOW_BIT),
};

static int __devinit adis16260_probe(struct spi_device *spi)
{
	int ret;
	struct adis16260_platform_data *pd = spi->dev.platform_data;
	struct adis16260_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	if (pd)
		st->negate = pd->negate;
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16260_info;
	indio_dev->num_channels
		= ARRAY_SIZE(adis16260_channels_x);
	if (pd && pd->direction)
		switch (pd->direction) {
		case 'x':
			indio_dev->channels = adis16260_channels_x;
			break;
		case 'y':
			indio_dev->channels = adis16260_channels_y;
			break;
		case 'z':
			indio_dev->channels = adis16260_channels_z;
			break;
		default:
			return -EINVAL;
		}
	else
		indio_dev->channels = adis16260_channels_x;
	indio_dev->num_channels = ARRAY_SIZE(adis16260_channels_x);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(&st->adis, indio_dev, spi, &adis16260_data);
	if (ret)
		goto error_free_dev;

	ret = adis_setup_buffer_and_trigger(&st->adis, indio_dev, NULL);
	if (ret)
		goto error_free_dev;

	if (indio_dev->buffer) {
		/* Set default scan mode */
		iio_scan_mask_set(indio_dev, indio_dev->buffer,
				  ADIS16260_SCAN_SUPPLY);
		iio_scan_mask_set(indio_dev, indio_dev->buffer,
				  ADIS16260_SCAN_GYRO);
		iio_scan_mask_set(indio_dev, indio_dev->buffer,
				  ADIS16260_SCAN_AUX_ADC);
		iio_scan_mask_set(indio_dev, indio_dev->buffer,
				  ADIS16260_SCAN_TEMP);
		iio_scan_mask_set(indio_dev, indio_dev->buffer,
				  ADIS16260_SCAN_ANGL);
	}

	/* Get the device into a sane initial state */
	ret = adis_initial_startup(&st->adis);
	if (ret)
		goto error_cleanup_buffer_trigger;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer_trigger;

	return 0;

error_cleanup_buffer_trigger:
	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int __devexit adis16260_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis16260_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis16260_stop_device(indio_dev);
	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

/*
 * These parts do not need to be differentiated until someone adds
 * support for the on chip filtering.
 */
static const struct spi_device_id adis16260_id[] = {
	{"adis16260", 0},
	{"adis16265", 0},
	{"adis16250", 0},
	{"adis16255", 0},
	{"adis16251", 1},
	{}
};
MODULE_DEVICE_TABLE(spi, adis16260_id);

static struct spi_driver adis16260_driver = {
	.driver = {
		.name = "adis16260",
		.owner = THIS_MODULE,
	},
	.probe = adis16260_probe,
	.remove = __devexit_p(adis16260_remove),
	.id_table = adis16260_id,
};
module_spi_driver(adis16260_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16260/5 Digital Gyroscope Sensor");
MODULE_LICENSE("GPL v2");
