/*
 * ADE7759 Active Energy Metering IC with di/dt Sensor Interface Driver
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
#include "meter.h"
#include "ade7759.h"

int ade7759_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7759_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7759_spi_write_reg_16(struct device *dev,
		u8 reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 3,
		}
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7759_WRITE_REG(reg_address);
	st->tx[1] = (value >> 8) & 0xFF;
	st->tx[2] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7759_spi_read_reg_8(struct device *dev,
		u8 reg_address,
		u8 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7759_READ_REG(reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 8 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7759_spi_read_reg_16(struct device *dev,
		u8 reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 3,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7759_READ_REG(reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = (st->rx[1] << 8) | st->rx[2];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7759_spi_read_reg_40(struct device *dev,
		u8 reg_address,
		u64 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 6,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7759_READ_REG(reg_address);
	memset(&st->tx[1], 0 , 5);

	spi_message_init(&msg);
	spi_message_add_tail(xfers, &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 40 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = ((u64)st->rx[1] << 32) | (st->rx[2] << 24) |
		(st->rx[3] << 16) | (st->rx[4] << 8) | st->rx[5];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t ade7759_read_8bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u8 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7759_spi_read_reg_8(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7759_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7759_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val);
}

static ssize_t ade7759_read_40bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u64 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = ade7759_spi_read_reg_40(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%llu\n", val);
}

static ssize_t ade7759_write_8bit(struct device *dev,
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
	ret = ade7759_spi_write_reg_8(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t ade7759_write_16bit(struct device *dev,
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
	ret = ade7759_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int ade7759_reset(struct device *dev)
{
	int ret;
	u16 val;
	ade7759_spi_read_reg_16(dev,
			ADE7759_MODE,
			&val);
	val |= 1 << 6; /* Software Chip Reset */
	ret = ade7759_spi_write_reg_16(dev,
			ADE7759_MODE,
			val);

	return ret;
}

static ssize_t ade7759_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -1;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return ade7759_reset(dev);
	}
	return -1;
}

static IIO_DEV_ATTR_AENERGY(ade7759_read_40bit, ADE7759_AENERGY);
static IIO_DEV_ATTR_CFDEN(S_IWUSR | S_IRUGO,
		ade7759_read_16bit,
		ade7759_write_16bit,
		ADE7759_CFDEN);
static IIO_DEV_ATTR_CFNUM(S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_CFNUM);
static IIO_DEV_ATTR_CHKSUM(ade7759_read_8bit, ADE7759_CHKSUM);
static IIO_DEV_ATTR_PHCAL(S_IWUSR | S_IRUGO,
		ade7759_read_16bit,
		ade7759_write_16bit,
		ADE7759_PHCAL);
static IIO_DEV_ATTR_APOS(S_IWUSR | S_IRUGO,
		ade7759_read_16bit,
		ade7759_write_16bit,
		ADE7759_APOS);
static IIO_DEV_ATTR_SAGCYC(S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_SAGCYC);
static IIO_DEV_ATTR_SAGLVL(S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_SAGLVL);
static IIO_DEV_ATTR_LINECYC(S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_LINECYC);
static IIO_DEV_ATTR_LENERGY(ade7759_read_40bit, ADE7759_LENERGY);
static IIO_DEV_ATTR_PGA_GAIN(S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_GAIN);
static IIO_DEV_ATTR_ACTIVE_POWER_GAIN(S_IWUSR | S_IRUGO,
		ade7759_read_16bit,
		ade7759_write_16bit,
		ADE7759_APGAIN);
static IIO_DEV_ATTR_CH_OFF(1, S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_CH1OS);
static IIO_DEV_ATTR_CH_OFF(2, S_IWUSR | S_IRUGO,
		ade7759_read_8bit,
		ade7759_write_8bit,
		ADE7759_CH2OS);

static int ade7759_set_irq(struct device *dev, bool enable)
{
	int ret;
	u8 irqen;
	ret = ade7759_spi_read_reg_8(dev, ADE7759_IRQEN, &irqen);
	if (ret)
		goto error_ret;

	if (enable)
		irqen |= 1 << 3; /* Enables an interrupt when a data is
				    present in the waveform register */
	else
		irqen &= ~(1 << 3);

	ret = ade7759_spi_write_reg_8(dev, ADE7759_IRQEN, irqen);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
int ade7759_stop_device(struct device *dev)
{
	int ret;
	u16 val;
	ade7759_spi_read_reg_16(dev,
			ADE7759_MODE,
			&val);
	val |= 1 << 4;  /* AD converters can be turned off */
	ret = ade7759_spi_write_reg_16(dev,
			ADE7759_MODE,
			val);

	return ret;
}

static int ade7759_initial_setup(struct ade7759_state *st)
{
	int ret;
	struct device *dev = &st->indio_dev->dev;

	/* use low spi speed for init */
	st->us->mode = SPI_MODE_3;
	spi_setup(st->us);

	/* Disable IRQ */
	ret = ade7759_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	ade7759_reset(dev);
	msleep(ADE7759_STARTUP_DELAY);

err_ret:
	return ret;
}

static ssize_t ade7759_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret, len = 0;
	u16 t;
	int sps;
	ret = ade7759_spi_read_reg_16(dev,
			ADE7759_MODE,
			&t);
	if (ret)
		return ret;

	t = (t >> 3) & 0x3;
	sps = 27900 / (1 + t);

	len = sprintf(buf, "%d SPS\n", sps);
	return len;
}

static ssize_t ade7759_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7759_state *st = iio_dev_get_devdata(indio_dev);
	unsigned long val;
	int ret;
	u16 reg, t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	t = (27900 / val);
	if (t > 0)
		t--;

	if (t > 1)
		st->us->max_speed_hz = ADE7759_SPI_SLOW;
	else
		st->us->max_speed_hz = ADE7759_SPI_FAST;

	ret = ade7759_spi_read_reg_16(dev,
			ADE7759_MODE,
			&reg);
	if (ret)
		goto out;

	reg &= ~(3 << 13);
	reg |= t << 13;

	ret = ade7759_spi_write_reg_16(dev,
			ADE7759_MODE,
			reg);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}
static IIO_DEV_ATTR_TEMP_RAW(ade7759_read_8bit);
static IIO_CONST_ATTR(temp_offset, "70 C");
static IIO_CONST_ATTR(temp_scale, "1 C");

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ade7759_read_frequency,
		ade7759_write_frequency);

static IIO_DEV_ATTR_RESET(ade7759_write_reset);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("27900 14000 7000 3500");

static IIO_CONST_ATTR(name, "ade7759");

static struct attribute *ade7759_event_attributes[] = {
	NULL
};

static struct attribute_group ade7759_event_attribute_group = {
	.attrs = ade7759_event_attributes,
};

static struct attribute *ade7759_attributes[] = {
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	&iio_dev_attr_phcal.dev_attr.attr,
	&iio_dev_attr_cfden.dev_attr.attr,
	&iio_dev_attr_aenergy.dev_attr.attr,
	&iio_dev_attr_cfnum.dev_attr.attr,
	&iio_dev_attr_apos.dev_attr.attr,
	&iio_dev_attr_sagcyc.dev_attr.attr,
	&iio_dev_attr_saglvl.dev_attr.attr,
	&iio_dev_attr_linecyc.dev_attr.attr,
	&iio_dev_attr_lenergy.dev_attr.attr,
	&iio_dev_attr_chksum.dev_attr.attr,
	&iio_dev_attr_pga_gain.dev_attr.attr,
	&iio_dev_attr_active_power_gain.dev_attr.attr,
	&iio_dev_attr_choff_1.dev_attr.attr,
	&iio_dev_attr_choff_2.dev_attr.attr,
	NULL,
};

static const struct attribute_group ade7759_attribute_group = {
	.attrs = ade7759_attributes,
};

static int __devinit ade7759_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct ade7759_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADE7759_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADE7759_MAX_TX, GFP_KERNEL);
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
	st->indio_dev->event_attrs = &ade7759_event_attribute_group;
	st->indio_dev->attrs = &ade7759_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ade7759_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = ade7759_initialize_ring(st->indio_dev->ring);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = iio_register_interrupt_line(spi->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_FALLING,
				"ade7759");
		if (ret)
			goto error_uninitialize_ring;

		ret = ade7759_probe_trigger(st->indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	/* Get the device into a sane initial state */
	ret = ade7759_initial_setup(st);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	if (st->indio_dev->modes & INDIO_RING_TRIGGERED)
		ade7759_remove_trigger(st->indio_dev);
error_unregister_line:
	if (st->indio_dev->modes & INDIO_RING_TRIGGERED)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_uninitialize_ring:
	ade7759_uninitialize_ring(st->indio_dev->ring);
error_unreg_ring_funcs:
	ade7759_unconfigure_ring(st->indio_dev);
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

/* fixme, confirm ordering in this function */
static int ade7759_remove(struct spi_device *spi)
{
	int ret;
	struct ade7759_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	ret = ade7759_stop_device(&(indio_dev->dev));
	if (ret)
		goto err_ret;

	flush_scheduled_work();

	ade7759_remove_trigger(indio_dev);
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(indio_dev, 0);

	ade7759_uninitialize_ring(indio_dev->ring);
	ade7759_unconfigure_ring(indio_dev);
	iio_device_unregister(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;

err_ret:
	return ret;
}

static struct spi_driver ade7759_driver = {
	.driver = {
		.name = "ade7759",
		.owner = THIS_MODULE,
	},
	.probe = ade7759_probe,
	.remove = __devexit_p(ade7759_remove),
};

static __init int ade7759_init(void)
{
	return spi_register_driver(&ade7759_driver);
}
module_init(ade7759_init);

static __exit void ade7759_exit(void)
{
	spi_unregister_driver(&ade7759_driver);
}
module_exit(ade7759_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7759 Active Energy Metering IC Driver");
MODULE_LICENSE("GPL v2");
