/*
 * ADIS16220 Programmable Digital Vibration Sensor driver
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

#include "adis16220.h"

#define DRIVER_NAME		"adis16220"

/**
 * adis16220_spi_write_reg_8() - write single byte to a register
 * @indio_dev: iio device associated with child of actual device
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16220_spi_write_reg_8(struct iio_dev *indio_dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct adis16220_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16220_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @indio_dev:  iio device associated with child of actual device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16220_spi_write_reg_16(struct iio_dev *indio_dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct adis16220_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16220_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16220_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @indio_dev: iio device associated with child of actual device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16220_spi_read_reg_16(struct iio_dev *indio_dev,
				     u8 lower_reg_address,
				     u16 *val)
{
	struct spi_message msg;
	struct adis16220_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_READ_REG(lower_reg_address);
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

static ssize_t adis16220_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	ssize_t ret;
	s16 val = 0;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret = adis16220_spi_read_reg_16(indio_dev, this_attr->address,
					(u16 *)&val);
	mutex_unlock(&indio_dev->mlock);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16220_write_16bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	u16 val;

	ret = kstrtou16(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = adis16220_spi_write_reg_16(indio_dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int adis16220_capture(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16220_spi_write_reg_16(indio_dev,
			ADIS16220_GLOB_CMD,
			0xBF08); /* initiates a manual data capture */
	if (ret)
		dev_err(&indio_dev->dev, "problem beginning capture");

	msleep(10); /* delay for capture to finish */

	return ret;
}

static int adis16220_reset(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16220_spi_write_reg_8(indio_dev,
			ADIS16220_GLOB_CMD,
			ADIS16220_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&indio_dev->dev, "problem resetting device");

	return ret;
}

static ssize_t adis16220_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool val;
	int ret;

	ret = strtobool(buf, &val);
	if (ret)
		return ret;
	if (!val)
		return -EINVAL;

	ret = adis16220_reset(indio_dev);
	if (ret)
		return ret;
	return len;
}

static ssize_t adis16220_write_capture(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool val;
	int ret;

	ret = strtobool(buf, &val);
	if (ret)
		return ret;
	if (!val)
		return -EINVAL;
	ret = adis16220_capture(indio_dev);
	if (ret)
		return ret;

	return len;
}

static int adis16220_check_status(struct iio_dev *indio_dev)
{
	u16 status;
	int ret;

	ret = adis16220_spi_read_reg_16(indio_dev, ADIS16220_DIAG_STAT,
					&status);

	if (ret < 0) {
		dev_err(&indio_dev->dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x7F;

	if (status & ADIS16220_DIAG_STAT_VIOLATION)
		dev_err(&indio_dev->dev,
			"Capture period violation/interruption\n");
	if (status & ADIS16220_DIAG_STAT_SPI_FAIL)
		dev_err(&indio_dev->dev, "SPI failure\n");
	if (status & ADIS16220_DIAG_STAT_FLASH_UPT)
		dev_err(&indio_dev->dev, "Flash update failed\n");
	if (status & ADIS16220_DIAG_STAT_POWER_HIGH)
		dev_err(&indio_dev->dev, "Power supply above 3.625V\n");
	if (status & ADIS16220_DIAG_STAT_POWER_LOW)
		dev_err(&indio_dev->dev, "Power supply below 3.15V\n");

error_ret:
	return ret;
}

static int adis16220_self_test(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16220_spi_write_reg_16(indio_dev,
			ADIS16220_MSC_CTRL,
			ADIS16220_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(&indio_dev->dev, "problem starting self test");
		goto err_ret;
	}

	adis16220_check_status(indio_dev);

err_ret:
	return ret;
}

static int adis16220_initial_setup(struct iio_dev *indio_dev)
{
	int ret;

	/* Do self test */
	ret = adis16220_self_test(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16220_check_status(indio_dev);
	if (ret) {
		adis16220_reset(indio_dev);
		dev_err(&indio_dev->dev, "device not playing ball -> reset");
		msleep(ADIS16220_STARTUP_DELAY);
		ret = adis16220_check_status(indio_dev);
		if (ret) {
			dev_err(&indio_dev->dev, "giving up");
			goto err_ret;
		}
	}

err_ret:
	return ret;
}

static ssize_t adis16220_capture_buffer_read(struct iio_dev *indio_dev,
					char *buf,
					loff_t off,
					size_t count,
					int addr)
{
	struct adis16220_state *st = iio_priv(indio_dev);
	struct spi_message msg;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 25,
		}, {
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.cs_change = 1,
			.delay_usecs = 25,
		},
	};
	int ret;
	int i;

	if (unlikely(!count))
		return count;

	if ((off >= ADIS16220_CAPTURE_SIZE) || (count & 1) || (off & 1))
		return -EINVAL;

	if (off + count > ADIS16220_CAPTURE_SIZE)
		count = ADIS16220_CAPTURE_SIZE - off;

	/* write the begin position of capture buffer */
	ret = adis16220_spi_write_reg_16(indio_dev,
					ADIS16220_CAPT_PNTR,
					off > 1);
	if (ret)
		return -EIO;

	/* read count/2 values from capture buffer */
	mutex_lock(&st->buf_lock);

	for (i = 0; i < count; i += 2) {
		st->tx[i] = ADIS16220_READ_REG(addr);
		st->tx[i + 1] = 0;
	}
	xfers[1].len = count;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {

		mutex_unlock(&st->buf_lock);
		return -EIO;
	}

	memcpy(buf, st->rx, count);

	mutex_unlock(&st->buf_lock);
	return count;
}

static ssize_t adis16220_accel_bin_read(struct file *filp, struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf,
					loff_t off,
					size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);

	return adis16220_capture_buffer_read(indio_dev, buf,
					off, count,
					ADIS16220_CAPT_BUFA);
}

static struct bin_attribute accel_bin = {
	.attr = {
		.name = "accel_bin",
		.mode = S_IRUGO,
	},
	.read = adis16220_accel_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static ssize_t adis16220_adc1_bin_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off,
				size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);

	return adis16220_capture_buffer_read(indio_dev, buf,
					off, count,
					ADIS16220_CAPT_BUF1);
}

static struct bin_attribute adc1_bin = {
	.attr = {
		.name = "in0_bin",
		.mode = S_IRUGO,
	},
	.read =  adis16220_adc1_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static ssize_t adis16220_adc2_bin_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off,
				size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);

	return adis16220_capture_buffer_read(indio_dev, buf,
					off, count,
					ADIS16220_CAPT_BUF2);
}


static struct bin_attribute adc2_bin = {
	.attr = {
		.name = "in1_bin",
		.mode = S_IRUGO,
	},
	.read =  adis16220_adc2_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL,
		adis16220_write_reset, 0);

#define IIO_DEV_ATTR_CAPTURE(_store)				\
	IIO_DEVICE_ATTR(capture, S_IWUSR, NULL, _store, 0)

static IIO_DEV_ATTR_CAPTURE(adis16220_write_capture);

#define IIO_DEV_ATTR_CAPTURE_COUNT(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(capture_count, _mode, _show, _store, _addr)

static IIO_DEV_ATTR_CAPTURE_COUNT(S_IWUSR | S_IRUGO,
		adis16220_read_16bit,
		adis16220_write_16bit,
		ADIS16220_CAPT_PNTR);

enum adis16220_channel {
	in_supply, in_1, in_2, accel, temp
};

struct adis16220_address_spec {
	u8 addr;
	u8 bits;
	bool sign;
};

/* Address / bits / signed */
static const struct adis16220_address_spec adis16220_addresses[][3] = {
	[in_supply] =	{ { ADIS16220_CAPT_SUPPLY,	12, 0 }, },
	[in_1] =	{ { ADIS16220_CAPT_BUF1,	16, 1 },
			  { ADIS16220_AIN1_NULL,	16, 1 },
			  { ADIS16220_CAPT_PEAK1,	16, 1 }, },
	[in_2] =	{ { ADIS16220_CAPT_BUF2,	16, 1 },
			  { ADIS16220_AIN2_NULL,	16, 1 },
			  { ADIS16220_CAPT_PEAK2,	16, 1 }, },
	[accel] =	{ { ADIS16220_CAPT_BUFA,	16, 1 },
			  { ADIS16220_ACCL_NULL,	16, 1 },
			  { ADIS16220_CAPT_PEAKA,	16, 1 }, },
	[temp] =	{ { ADIS16220_CAPT_TEMP,	12, 0 }, }
};

static int adis16220_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	int ret = -EINVAL;
	int addrind = 0;
	u16 uval;
	s16 sval;
	u8 bits;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		addrind = 0;
		break;
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = 25;
			return IIO_VAL_INT;
		}
		addrind = 1;
		break;
	case IIO_CHAN_INFO_PEAK:
		addrind = 2;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		switch (chan->type) {
		case IIO_TEMP:
			*val2 = -470000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val2 = 1887042;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_VOLTAGE:
			if (chan->channel == 0)
				*val2 = 0012221;
			else /* Should really be dependent on VDD */
				*val2 = 305;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
	if (adis16220_addresses[chan->address][addrind].sign) {
		ret = adis16220_spi_read_reg_16(indio_dev,
						adis16220_addresses[chan
								    ->address]
						[addrind].addr,
						&sval);
		if (ret)
			return ret;
		bits = adis16220_addresses[chan->address][addrind].bits;
		sval &= (1 << bits) - 1;
		sval = (s16)(sval << (16 - bits)) >> (16 - bits);
		*val = sval;
		return IIO_VAL_INT;
	} else {
		ret = adis16220_spi_read_reg_16(indio_dev,
						adis16220_addresses[chan
								    ->address]
						[addrind].addr,
						&uval);
		if (ret)
			return ret;
		bits = adis16220_addresses[chan->address][addrind].bits;
		uval &= (1 << bits) - 1;
		*val = uval;
		return IIO_VAL_INT;
	}
}

static const struct iio_chan_spec adis16220_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "supply",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			     IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in_supply,
	}, {
		.type = IIO_ACCEL,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			     IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
			     IIO_CHAN_INFO_SCALE_SEPARATE_BIT |
			     IIO_CHAN_INFO_PEAK_SEPARATE_BIT,
		.address = accel,
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			     IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
			     IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = temp,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			     IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
			     IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in_1,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 2,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = in_2,
	}
};

static struct attribute *adis16220_attributes[] = {
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_capture.dev_attr.attr,
	&iio_dev_attr_capture_count.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16220_attribute_group = {
	.attrs = adis16220_attributes,
};

static const struct iio_info adis16220_info = {
	.attrs = &adis16220_attribute_group,
	.driver_module = THIS_MODULE,
	.read_raw = &adis16220_read_raw,
};

static int __devinit adis16220_probe(struct spi_device *spi)
{
	int ret;
	struct adis16220_state *st;
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

	st->us = spi;
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16220_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adis16220_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16220_channels);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	ret = sysfs_create_bin_file(&indio_dev->dev.kobj, &accel_bin);
	if (ret)
		goto error_unregister_dev;

	ret = sysfs_create_bin_file(&indio_dev->dev.kobj, &adc1_bin);
	if (ret)
		goto error_rm_accel_bin;

	ret = sysfs_create_bin_file(&indio_dev->dev.kobj, &adc2_bin);
	if (ret)
		goto error_rm_adc1_bin;

	/* Get the device into a sane initial state */
	ret = adis16220_initial_setup(indio_dev);
	if (ret)
		goto error_rm_adc2_bin;
	return 0;

error_rm_adc2_bin:
	sysfs_remove_bin_file(&indio_dev->dev.kobj, &adc2_bin);
error_rm_adc1_bin:
	sysfs_remove_bin_file(&indio_dev->dev.kobj, &adc1_bin);
error_rm_accel_bin:
	sysfs_remove_bin_file(&indio_dev->dev.kobj, &accel_bin);
error_unregister_dev:
	iio_device_unregister(indio_dev);
error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int adis16220_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	flush_scheduled_work();

	sysfs_remove_bin_file(&indio_dev->dev.kobj, &adc2_bin);
	sysfs_remove_bin_file(&indio_dev->dev.kobj, &adc1_bin);
	sysfs_remove_bin_file(&indio_dev->dev.kobj, &accel_bin);
	iio_device_unregister(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct spi_driver adis16220_driver = {
	.driver = {
		.name = "adis16220",
		.owner = THIS_MODULE,
	},
	.probe = adis16220_probe,
	.remove = __devexit_p(adis16220_remove),
};
module_spi_driver(adis16220_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16220 Digital Vibration Sensor");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16220");
