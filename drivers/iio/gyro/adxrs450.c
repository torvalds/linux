/*
 * ADXRS450/ADXRS453 Digital Output Gyroscope Driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
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

#define ADXRS450_STARTUP_DELAY	50 /* ms */

/* The MSB for the spi commands */
#define ADXRS450_SENSOR_DATA    (0x20 << 24)
#define ADXRS450_WRITE_DATA	(0x40 << 24)
#define ADXRS450_READ_DATA	(0x80 << 24)

#define ADXRS450_RATE1	0x00	/* Rate Registers */
#define ADXRS450_TEMP1	0x02	/* Temperature Registers */
#define ADXRS450_LOCST1	0x04	/* Low CST Memory Registers */
#define ADXRS450_HICST1	0x06	/* High CST Memory Registers */
#define ADXRS450_QUAD1	0x08	/* Quad Memory Registers */
#define ADXRS450_FAULT1	0x0A	/* Fault Registers */
#define ADXRS450_PID1	0x0C	/* Part ID Register 1 */
#define ADXRS450_SNH	0x0E	/* Serial Number Registers, 4 bytes */
#define ADXRS450_SNL	0x10
#define ADXRS450_DNC1	0x12	/* Dynamic Null Correction Registers */
/* Check bits */
#define ADXRS450_P	0x01
#define ADXRS450_CHK	0x02
#define ADXRS450_CST	0x04
#define ADXRS450_PWR	0x08
#define ADXRS450_POR	0x10
#define ADXRS450_NVM	0x20
#define ADXRS450_Q	0x40
#define ADXRS450_PLL	0x80
#define ADXRS450_UV	0x100
#define ADXRS450_OV	0x200
#define ADXRS450_AMP	0x400
#define ADXRS450_FAIL	0x800

#define ADXRS450_WRERR_MASK	(0x7 << 29)

#define ADXRS450_MAX_RX 4
#define ADXRS450_MAX_TX 4

#define ADXRS450_GET_ST(a)	((a >> 26) & 0x3)

enum {
	ID_ADXRS450,
	ID_ADXRS453,
};

/**
 * struct adxrs450_state - device instance specific data
 * @us:			actual spi_device
 * @buf_lock:		mutex to protect tx and rx
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct adxrs450_state {
	struct spi_device	*us;
	struct mutex		buf_lock;
	__be32			tx ____cacheline_aligned;
	__be32			rx;

};

/**
 * adxrs450_spi_read_reg_16() - read 2 bytes from a register pair
 * @indio_dev: device associated with child of actual iio_dev
 * @reg_address: the address of the lower of the two registers, which should be
 *	an even address, the second register's address is reg_address + 1.
 * @val: somewhere to pass back the value read
 **/
static int adxrs450_spi_read_reg_16(struct iio_dev *indio_dev,
				    u8 reg_address,
				    u16 *val)
{
	struct adxrs450_state *st = iio_priv(indio_dev);
	u32 tx;
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &st->tx,
			.bits_per_word = 8,
			.len = sizeof(st->tx),
			.cs_change = 1,
		}, {
			.rx_buf = &st->rx,
			.bits_per_word = 8,
			.len = sizeof(st->rx),
		},
	};

	mutex_lock(&st->buf_lock);
	tx = ADXRS450_READ_DATA | (reg_address << 17);

	if (!(hweight32(tx) & 1))
		tx |= ADXRS450_P;

	st->tx = cpu_to_be32(tx);
	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	if (ret) {
		dev_err(&st->us->dev, "problem while reading 16 bit register 0x%02x\n",
				reg_address);
		goto error_ret;
	}

	*val = (be32_to_cpu(st->rx) >> 5) & 0xFFFF;

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_write_reg_16() - write 2 bytes data to a register pair
 * @indio_dev: device associated with child of actual actual iio_dev
 * @reg_address: the address of the lower of the two registers,which should be
 *	an even address, the second register's address is reg_address + 1.
 * @val: value to be written.
 **/
static int adxrs450_spi_write_reg_16(struct iio_dev *indio_dev,
				     u8 reg_address,
				     u16 val)
{
	struct adxrs450_state *st = iio_priv(indio_dev);
	u32 tx;
	int ret;

	mutex_lock(&st->buf_lock);
	tx = ADXRS450_WRITE_DATA | (reg_address << 17) | (val << 1);

	if (!(hweight32(tx) & 1))
		tx |= ADXRS450_P;

	st->tx = cpu_to_be32(tx);
	ret = spi_write(st->us, &st->tx, sizeof(st->tx));
	if (ret)
		dev_err(&st->us->dev, "problem while writing 16 bit register 0x%02x\n",
			reg_address);
	usleep_range(100, 1000); /* enforce sequential transfer delay 0.1ms */
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_sensor_data() - read 2 bytes sensor data
 * @indio_dev: device associated with child of actual iio_dev
 * @val: somewhere to pass back the value read
 **/
static int adxrs450_spi_sensor_data(struct iio_dev *indio_dev, s16 *val)
{
	struct adxrs450_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &st->tx,
			.bits_per_word = 8,
			.len = sizeof(st->tx),
			.cs_change = 1,
		}, {
			.rx_buf = &st->rx,
			.bits_per_word = 8,
			.len = sizeof(st->rx),
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx = cpu_to_be32(ADXRS450_SENSOR_DATA);

	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	if (ret) {
		dev_err(&st->us->dev, "Problem while reading sensor data\n");
		goto error_ret;
	}

	*val = (be32_to_cpu(st->rx) >> 10) & 0xFFFF;

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_initial() - use for initializing procedure.
 * @st: device instance specific data
 * @val: somewhere to pass back the value read
 * @chk: Whether to perform fault check
 **/
static int adxrs450_spi_initial(struct adxrs450_state *st,
		u32 *val, char chk)
{
	int ret;
	u32 tx;
	struct spi_transfer xfers = {
		.tx_buf = &st->tx,
		.rx_buf = &st->rx,
		.bits_per_word = 8,
		.len = sizeof(st->tx),
	};

	mutex_lock(&st->buf_lock);
	tx = ADXRS450_SENSOR_DATA;
	if (chk)
		tx |= (ADXRS450_CHK | ADXRS450_P);
	st->tx = cpu_to_be32(tx);
	ret = spi_sync_transfer(st->us, &xfers, 1);
	if (ret) {
		dev_err(&st->us->dev, "Problem while reading initializing data\n");
		goto error_ret;
	}

	*val = be32_to_cpu(st->rx);

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/* Recommended Startup Sequence by spec */
static int adxrs450_initial_setup(struct iio_dev *indio_dev)
{
	u32 t;
	u16 data;
	int ret;
	struct adxrs450_state *st = iio_priv(indio_dev);

	msleep(ADXRS450_STARTUP_DELAY*2);
	ret = adxrs450_spi_initial(st, &t, 1);
	if (ret)
		return ret;
	if (t != 0x01)
		dev_warn(&st->us->dev, "The initial power on response is not correct! Restart without reset?\n");

	msleep(ADXRS450_STARTUP_DELAY);
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;

	msleep(ADXRS450_STARTUP_DELAY);
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;
	if (((t & 0xff) | 0x01) != 0xff || ADXRS450_GET_ST(t) != 2) {
		dev_err(&st->us->dev, "The second response is not correct!\n");
		return -EIO;

	}
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;
	if (((t & 0xff) | 0x01) != 0xff || ADXRS450_GET_ST(t) != 2) {
		dev_err(&st->us->dev, "The third response is not correct!\n");
		return -EIO;

	}
	ret = adxrs450_spi_read_reg_16(indio_dev, ADXRS450_FAULT1, &data);
	if (ret)
		return ret;
	if (data & 0x0fff) {
		dev_err(&st->us->dev, "The device is not in normal status!\n");
		return -EINVAL;
	}

	return 0;
}

static int adxrs450_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val,
			      int val2,
			      long mask)
{
	int ret;
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < -0x400 || val >= 0x400)
			return -EINVAL;
		ret = adxrs450_spi_write_reg_16(indio_dev,
						ADXRS450_DNC1, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int adxrs450_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	int ret;
	s16 t;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			ret = adxrs450_spi_sensor_data(indio_dev, &t);
			if (ret)
				break;
			*val = t;
			ret = IIO_VAL_INT;
			break;
		case IIO_TEMP:
			ret = adxrs450_spi_read_reg_16(indio_dev,
						       ADXRS450_TEMP1, &t);
			if (ret)
				break;
			*val = (t >> 6) + 225;
			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = 218166;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			*val = 200;
			*val2 = 0;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW:
		ret = adxrs450_spi_read_reg_16(indio_dev, ADXRS450_QUAD1, &t);
		if (ret)
			break;
		*val = t;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = adxrs450_spi_read_reg_16(indio_dev, ADXRS450_DNC1, &t);
		if (ret)
			break;
		*val = sign_extend32(t, 9);
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct iio_chan_spec adxrs450_channels[2][2] = {
	[ID_ADXRS450] = {
		{
			.type = IIO_ANGL_VEL,
			.modified = 1,
			.channel2 = IIO_MOD_Z,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBBIAS) |
			BIT(IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		}, {
			.type = IIO_TEMP,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		}
	},
	[ID_ADXRS453] = {
		{
			.type = IIO_ANGL_VEL,
			.modified = 1,
			.channel2 = IIO_MOD_Z,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW),
		}, {
			.type = IIO_TEMP,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		}
	},
};

static const struct iio_info adxrs450_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &adxrs450_read_raw,
	.write_raw = &adxrs450_write_raw,
};

static int adxrs450_probe(struct spi_device *spi)
{
	int ret;
	struct adxrs450_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* This is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adxrs450_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels =
		adxrs450_channels[spi_get_device_id(spi)->driver_data];
	indio_dev->num_channels = ARRAY_SIZE(adxrs450_channels);
	indio_dev->name = spi->dev.driver->name;

	ret = iio_device_register(indio_dev);
	if (ret)
		return ret;

	/* Get the device into a sane initial state */
	ret = adxrs450_initial_setup(indio_dev);
	if (ret)
		goto error_initial;
	return 0;
error_initial:
	iio_device_unregister(indio_dev);
	return ret;
}

static int adxrs450_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));

	return 0;
}

static const struct spi_device_id adxrs450_id[] = {
	{"adxrs450", ID_ADXRS450},
	{"adxrs453", ID_ADXRS453},
	{}
};
MODULE_DEVICE_TABLE(spi, adxrs450_id);

static struct spi_driver adxrs450_driver = {
	.driver = {
		.name = "adxrs450",
		.owner = THIS_MODULE,
	},
	.probe = adxrs450_probe,
	.remove = adxrs450_remove,
	.id_table	= adxrs450_id,
};
module_spi_driver(adxrs450_driver);

MODULE_AUTHOR("Cliff Cai <cliff.cai@xxxxxxxxxx>");
MODULE_DESCRIPTION("Analog Devices ADXRS450/ADXRS453 Gyroscope SPI driver");
MODULE_LICENSE("GPL v2");
