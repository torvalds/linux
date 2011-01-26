/*
 * ADIS16260/ADIS16265 Programmable Digital Gyroscope Sensor Driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "../adc/adc.h"
#include "gyro.h"

#include "adis16260.h"

#define DRIVER_NAME		"adis16260"

static int adis16260_check_status(struct device *dev);

/**
 * adis16260_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16260_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16260_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16260_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16260_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16260_spi_write_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16260_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 20,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 20,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16260_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16260_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16260_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16260_spi_read_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16260_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 30,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 30,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16260_READ_REG(lower_reg_address);
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

static ssize_t adis16260_spi_read_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf,
		unsigned bits)
{
	int ret;
	s16 val = 0;
	unsigned shift = 16 - bits;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16260_spi_read_reg_16(dev, this_attr->address, (u16 *)&val);
	if (ret)
		return ret;

	if (val & ADIS16260_ERROR_ACTIVE)
		adis16260_check_status(dev);
	val = ((s16)(val << shift) >> shift);
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16260_read_12bit_unsigned(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16260_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	if (val & ADIS16260_ERROR_ACTIVE)
		adis16260_check_status(dev);

	return sprintf(buf, "%u\n", val & 0x0FFF);
}

static ssize_t adis16260_read_12bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16260_spi_read_signed(dev, attr, buf, 12);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16260_read_14bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16260_spi_read_signed(dev, attr, buf, 14);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16260_write_16bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = adis16260_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t adis16260_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret, len = 0;
	u16 t;
	int sps;
	ret = adis16260_spi_read_reg_16(dev,
			ADIS16260_SMPL_PRD,
			&t);
	if (ret)
		return ret;
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16260_state *st = iio_dev_get_devdata(indio_dev);
	long val;
	int ret;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	t = (2048 / val);
	if (t > 0)
		t--;
	t &= ADIS16260_SMPL_PRD_DIV_MASK;
	if ((t & ADIS16260_SMPL_PRD_DIV_MASK) >= 0x0A)
		st->us->max_speed_hz = ADIS16260_SPI_SLOW;
	else
		st->us->max_speed_hz = ADIS16260_SPI_FAST;

	ret = adis16260_spi_write_reg_8(dev,
			ADIS16260_SMPL_PRD,
			t);

	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static ssize_t adis16260_read_gyro_scale(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16260_state *st = iio_dev_get_devdata(indio_dev);
	ssize_t ret = 0;

	if (st->negate)
		ret = sprintf(buf, "-");
	/* Take the iio_dev status lock */
	ret += sprintf(buf + ret, "%s\n", "0.00127862821");

	return ret;
}

static int adis16260_reset(struct device *dev)
{
	int ret;
	ret = adis16260_spi_write_reg_8(dev,
			ADIS16260_GLOB_CMD,
			ADIS16260_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(dev, "problem resetting device");

	return ret;
}

static ssize_t adis16260_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -EINVAL;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16260_reset(dev);
	}
	return -EINVAL;
}

int adis16260_set_irq(struct device *dev, bool enable)
{
	int ret;
	u16 msc;
	ret = adis16260_spi_read_reg_16(dev, ADIS16260_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16260_MSC_CTRL_DATA_RDY_POL_HIGH;
	if (enable)
		msc |= ADIS16260_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16260_MSC_CTRL_DATA_RDY_EN;

	ret = adis16260_spi_write_reg_16(dev, ADIS16260_MSC_CTRL, msc);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
static int adis16260_stop_device(struct device *dev)
{
	int ret;
	u16 val = ADIS16260_SLP_CNT_POWER_OFF;

	ret = adis16260_spi_write_reg_16(dev, ADIS16260_SLP_CNT, val);
	if (ret)
		dev_err(dev, "problem with turning device off: SLP_CNT");

	return ret;
}

static int adis16260_self_test(struct device *dev)
{
	int ret;
	ret = adis16260_spi_write_reg_16(dev,
			ADIS16260_MSC_CTRL,
			ADIS16260_MSC_CTRL_MEM_TEST);
	if (ret) {
		dev_err(dev, "problem starting self test");
		goto err_ret;
	}

	adis16260_check_status(dev);

err_ret:
	return ret;
}

static int adis16260_check_status(struct device *dev)
{
	u16 status;
	int ret;

	ret = adis16260_spi_read_reg_16(dev, ADIS16260_DIAG_STAT, &status);

	if (ret < 0) {
		dev_err(dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x7F;
	if (status & ADIS16260_DIAG_STAT_FLASH_CHK)
		dev_err(dev, "Flash checksum error\n");
	if (status & ADIS16260_DIAG_STAT_SELF_TEST)
		dev_err(dev, "Self test error\n");
	if (status & ADIS16260_DIAG_STAT_OVERFLOW)
		dev_err(dev, "Sensor overrange\n");
	if (status & ADIS16260_DIAG_STAT_SPI_FAIL)
		dev_err(dev, "SPI failure\n");
	if (status & ADIS16260_DIAG_STAT_FLASH_UPT)
		dev_err(dev, "Flash update failed\n");
	if (status & ADIS16260_DIAG_STAT_POWER_HIGH)
		dev_err(dev, "Power supply above 5.25V\n");
	if (status & ADIS16260_DIAG_STAT_POWER_LOW)
		dev_err(dev, "Power supply below 4.75V\n");

error_ret:
	return ret;
}

static int adis16260_initial_setup(struct adis16260_state *st)
{
	int ret;
	struct device *dev = &st->indio_dev->dev;

	/* Disable IRQ */
	ret = adis16260_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16260_self_test(dev);
	if (ret) {
		dev_err(dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16260_check_status(dev);
	if (ret) {
		adis16260_reset(dev);
		dev_err(dev, "device not playing ball -> reset");
		msleep(ADIS16260_STARTUP_DELAY);
		ret = adis16260_check_status(dev);
		if (ret) {
			dev_err(dev, "giving up");
			goto err_ret;
		}
	}

	printk(KERN_INFO DRIVER_NAME ": at CS%d (irq %d)\n",
			st->us->chip_select, st->us->irq);

err_ret:
	return ret;
}

static IIO_DEV_ATTR_IN_NAMED_RAW(0, supply,
				adis16260_read_12bit_unsigned,
				ADIS16260_SUPPLY_OUT);
static IIO_CONST_ATTR_IN_NAMED_SCALE(0, supply, "0.0018315");

static IIO_DEV_ATTR_TEMP_RAW(adis16260_read_12bit_unsigned);
static IIO_CONST_ATTR_TEMP_OFFSET("25");
static IIO_CONST_ATTR_TEMP_SCALE("0.1453");

static IIO_DEV_ATTR_IN_RAW(1, adis16260_read_12bit_unsigned,
		ADIS16260_AUX_ADC);
static IIO_CONST_ATTR(in1_scale, "0.0006105");

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		adis16260_read_frequency,
		adis16260_write_frequency);

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16260_write_reset, 0);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("256 2048");

static IIO_CONST_ATTR_NAME("adis16260");

static struct attribute *adis16260_event_attributes[] = {
	NULL
};

static struct attribute_group adis16260_event_attribute_group = {
	.attrs = adis16260_event_attributes,
};

#define ADIS16260_GYRO_ATTR_SET(axis)					\
	IIO_DEV_ATTR_GYRO##axis(adis16260_read_14bit_signed,		\
				ADIS16260_GYRO_OUT);			\
	static IIO_DEV_ATTR_GYRO##axis##_SCALE(S_IRUGO,			\
					adis16260_read_gyro_scale,	\
					NULL,				\
					0);				\
	static IIO_DEV_ATTR_GYRO##axis##_CALIBSCALE(S_IRUGO | S_IWUSR,	\
					adis16260_read_12bit_unsigned,	\
					adis16260_write_16bit,		\
					ADIS16260_GYRO_SCALE);		\
	static IIO_DEV_ATTR_GYRO##axis##_CALIBBIAS(S_IWUSR | S_IRUGO,	\
					adis16260_read_12bit_signed,	\
					adis16260_write_16bit,		\
					ADIS16260_GYRO_OFF);		\
	static IIO_DEV_ATTR_ANGL##axis(adis16260_read_14bit_signed,	\
				       ADIS16260_ANGL_OUT);

static ADIS16260_GYRO_ATTR_SET();
static ADIS16260_GYRO_ATTR_SET(_X);
static ADIS16260_GYRO_ATTR_SET(_Y);
static ADIS16260_GYRO_ATTR_SET(_Z);

#define ADIS16260_ATTR_GROUP(axis)					\
	struct attribute *adis16260_attributes##axis[] = {		\
		&iio_dev_attr_in0_supply_raw.dev_attr.attr,		\
		&iio_const_attr_in0_supply_scale.dev_attr.attr,		\
		&iio_dev_attr_gyro##axis##_raw.dev_attr.attr,		\
		&iio_dev_attr_gyro##axis##_scale.dev_attr.attr,		\
		&iio_dev_attr_gyro##axis##_calibscale.dev_attr.attr,	\
		&iio_dev_attr_gyro##axis##_calibbias.dev_attr.attr,	\
		&iio_dev_attr_angl##axis##_raw.dev_attr.attr,		\
		&iio_dev_attr_temp_raw.dev_attr.attr,			\
		&iio_const_attr_temp_offset.dev_attr.attr,		\
		&iio_const_attr_temp_scale.dev_attr.attr,		\
		&iio_dev_attr_in1_raw.dev_attr.attr,			\
		&iio_const_attr_in1_scale.dev_attr.attr,		\
		&iio_dev_attr_sampling_frequency.dev_attr.attr,		\
		&iio_const_attr_sampling_frequency_available.dev_attr.attr, \
		&iio_dev_attr_reset.dev_attr.attr,			\
		&iio_const_attr_name.dev_attr.attr,			\
		NULL							\
	};								\
	static const struct attribute_group adis16260_attribute_group##axis \
	= {								\
		.attrs = adis16260_attributes##axis,			\
	};

static ADIS16260_ATTR_GROUP();
static ADIS16260_ATTR_GROUP(_x);
static ADIS16260_ATTR_GROUP(_y);
static ADIS16260_ATTR_GROUP(_z);

static int __devinit adis16260_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16260_platform_data *pd = spi->dev.platform_data;
	struct adis16260_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	if (pd)
		st->negate = pd->negate;
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16260_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16260_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->num_interrupt_lines = 1;
	st->indio_dev->event_attrs = &adis16260_event_attribute_group;
	if (pd && pd->direction)
		switch (pd->direction) {
		case 'x':
			st->indio_dev->attrs = &adis16260_attribute_group_x;
			break;
		case 'y':
			st->indio_dev->attrs = &adis16260_attribute_group_y;
			break;
		case 'z':
			st->indio_dev->attrs = &adis16260_attribute_group_z;
			break;
		default:
			st->indio_dev->attrs = &adis16260_attribute_group;
			break;
		}
	else
		st->indio_dev->attrs = &adis16260_attribute_group;

	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16260_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;
	ret = iio_ring_buffer_register(st->indio_dev->ring, 0);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = iio_register_interrupt_line(spi->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_RISING,
				"adis16260");
		if (ret)
			goto error_uninitialize_ring;

		ret = adis16260_probe_trigger(st->indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	/* Get the device into a sane initial state */
	ret = adis16260_initial_setup(st);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	adis16260_remove_trigger(st->indio_dev);
error_unregister_line:
	if (spi->irq)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_uninitialize_ring:
	iio_ring_buffer_unregister(st->indio_dev->ring);
error_unreg_ring_funcs:
	adis16260_unconfigure_ring(st->indio_dev);
error_free_dev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_tx:
	kfree(st->tx);
error_free_rx:
	kfree(st->rx);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

static int adis16260_remove(struct spi_device *spi)
{
	int ret;
	struct adis16260_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	ret = adis16260_stop_device(&(indio_dev->dev));
	if (ret)
		goto err_ret;

	flush_scheduled_work();

	adis16260_remove_trigger(indio_dev);
	if (spi->irq)
		iio_unregister_interrupt_line(indio_dev, 0);

	iio_ring_buffer_unregister(st->indio_dev->ring);
	iio_device_unregister(indio_dev);
	adis16260_unconfigure_ring(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

err_ret:
	return ret;
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
	{}
};

static struct spi_driver adis16260_driver = {
	.driver = {
		.name = "adis16260",
		.owner = THIS_MODULE,
	},
	.probe = adis16260_probe,
	.remove = __devexit_p(adis16260_remove),
	.id_table = adis16260_id,
};

static __init int adis16260_init(void)
{
	return spi_register_driver(&adis16260_driver);
}
module_init(adis16260_init);

static __exit void adis16260_exit(void)
{
	spi_unregister_driver(&adis16260_driver);
}
module_exit(adis16260_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16260/5 Digital Gyroscope Sensor");
MODULE_LICENSE("GPL v2");
