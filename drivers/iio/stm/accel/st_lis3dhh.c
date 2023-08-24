// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis3dhh sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <asm/unaligned.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lis3dhh.h"

#define LIS3DHH_DEV_NAME		"lis3dhh"
#define IIS3DHHC_DEV_NAME		"iis3dhhc"

#define REG_WHOAMI_ADDR			0x0f
#define REG_WHOAMI_VAL			0x11

#define REG_CTRL1_ADDR			0x20
#define REG_CTRL1_BDU_MASK		BIT(0)
#define REG_CTRL1_SW_RESET_MASK		BIT(2)
#define REG_CTRL1_EN_MASK		BIT(7)

#define REG_INT1_CTRL_ADDR		0x21
#define REG_INT2_CTRL_ADDR		0x22
#define REG_INT_FTM_MASK		BIT(3)

#define ST_LIS3DHH_FS			IIO_G_TO_M_S_2(76)

#define ST_LIS3DHH_DATA_CHANNEL(addr, modx, scan_idx)		\
{								\
	.type = IIO_ACCEL,					\
	.address = addr,					\
	.modified = 1,						\
	.channel2 = modx,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = scan_idx,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_LE,				\
	},							\
}

#define ST_LIS3DHH_FLUSH_CHANNEL()				\
{								\
	.type = IIO_ACCEL,					\
	.modified = 0,						\
	.scan_index = -1,					\
	.indexed = -1,						\
	.event_spec = &st_lis3dhh_fifo_flush_event,		\
	.num_event_specs = 1,					\
}

const struct iio_event_spec st_lis3dhh_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_chan_spec st_lis3dhh_channels[] = {
	ST_LIS3DHH_DATA_CHANNEL(0x28, IIO_MOD_X, 0),
	ST_LIS3DHH_DATA_CHANNEL(0x2a, IIO_MOD_Y, 1),
	ST_LIS3DHH_DATA_CHANNEL(0x2c, IIO_MOD_Z, 2),
	ST_LIS3DHH_FLUSH_CHANNEL(),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

#define SENSORS_SPI_READ	BIT(7)
 int st_lis3dhh_spi_read(struct st_lis3dhh_hw *hw, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(hw->dev);
	int err;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = hw->tb.tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = hw->tb.rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	hw->tb.tx_buf[0] = addr | SENSORS_SPI_READ;
	err = spi_sync_transfer(spi, xfers,  ARRAY_SIZE(xfers));
	if (err < 0)
		return err;

	memcpy(data, hw->tb.rx_buf, len * sizeof(u8));

	return len;
}

static int st_lis3dhh_spi_write(struct st_lis3dhh_hw *hw, u8 addr,
				int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(hw->dev);

	if (len >= ST_LIS3DHH_TX_MAX_LENGTH)
		return -ENOMEM;

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

int st_lis3dhh_write_with_mask(struct st_lis3dhh_hw *hw, u8 addr, u8 mask,
			       u8 val)
{
	u8 data;
	int err;

	mutex_lock(&hw->lock);

	err = st_lis3dhh_spi_read(hw, addr, 1, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %02x register\n", addr);
		mutex_unlock(&hw->lock);

		return err;
	}

	data = (data & ~mask) | ((val << __ffs(mask)) & mask);

	err = st_lis3dhh_spi_write(hw, addr, 1, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to write %02x register\n", addr);
		mutex_unlock(&hw->lock);

		return err;
	}

	mutex_unlock(&hw->lock);

	return 0;
}

int st_lis3dhh_set_enable(struct st_lis3dhh_hw *hw, bool enable)
{
	return st_lis3dhh_write_with_mask(hw, REG_CTRL1_ADDR,
					  REG_CTRL1_EN_MASK, enable);
}

static int st_lis3dhh_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_lis3dhh_hw *hw = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int err, delay;
		u8 data[2];

		mutex_lock(&iio_dev->mlock);

		if (iio_buffer_enabled(iio_dev)) {
			mutex_unlock(&iio_dev->mlock);
			return -EBUSY;
		}

		err = st_lis3dhh_set_enable(hw, true);
		if (err < 0) {
			mutex_unlock(&iio_dev->mlock);
			return err;
		}

		/* sample to discard, 3 * odr us */
		delay = 3000000 / ST_LIS3DHH_ODR;
		usleep_range(delay, delay + 1);

		err = st_lis3dhh_spi_read(hw, ch->address, 2, data);
		if (err < 0) {
			mutex_unlock(&iio_dev->mlock);
			return err;
		}

		err = st_lis3dhh_set_enable(hw, false);
		if (err < 0) {
			mutex_unlock(&iio_dev->mlock);
			return err;
		}

		*val = (s16)get_unaligned_le16(data);
		*val = *val >> ch->scan_type.shift;

		mutex_unlock(&iio_dev->mlock);

		ret = IIO_VAL_INT;
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = ST_LIS3DHH_FS;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = ST_LIS3DHH_ODR;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t
st_lis3dhh_get_sampling_frequency_avail(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", ST_LIS3DHH_ODR);
}

static ssize_t st_lis3dhh_get_scale_avail(struct device *device,
					  struct device_attribute *attr,
					  char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0.%06d\n", (int)ST_LIS3DHH_FS);
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lis3dhh_get_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lis3dhh_get_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644,
		       st_lis3dhh_get_hwfifo_watermark,
		       st_lis3dhh_set_hwfifo_watermark, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lis3dhh_get_max_hwfifo_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL, st_lis3dhh_flush_hwfifo, 0);

static struct attribute *st_lis3dhh_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis3dhh_attribute_group = {
	.attrs = st_lis3dhh_attributes,
};

static const struct iio_info st_lis3dhh_info = {
	.attrs = &st_lis3dhh_attribute_group,
	.read_raw = st_lis3dhh_read_raw,
};

static int st_lis3dhh_check_whoami(struct st_lis3dhh_hw *hw)
{
	u8 data;
	int err;

	err = st_lis3dhh_spi_read(hw, REG_WHOAMI_ADDR, sizeof(data), &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != REG_WHOAMI_VAL) {
		dev_err(hw->dev, "wrong whoami {%02x-%02x}\n",
			data, REG_WHOAMI_VAL);
		return -ENODEV;
	}

	return 0;
}

static int st_lis3dhh_of_get_drdy_pin(struct st_lis3dhh_hw *hw, int *drdy_pin)
{
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,drdy-int-pin", drdy_pin);
}

static int st_lis3dhh_set_drdy_reg(struct st_lis3dhh_hw *hw)
{
	int drdy_pin;
	u8 drdy_reg;

	if (st_lis3dhh_of_get_drdy_pin(hw, &drdy_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		drdy_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (drdy_pin) {
	case 1:
		drdy_reg = REG_INT1_CTRL_ADDR;
		break;
	case 2:
		drdy_reg = REG_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported data ready pin\n");
		return -EINVAL;
	}

	return st_lis3dhh_write_with_mask(hw, drdy_reg, REG_INT_FTM_MASK, 1);
}

static int st_lis3dhh_init_device(struct st_lis3dhh_hw *hw)
{
	int err;

	err = st_lis3dhh_write_with_mask(hw, REG_CTRL1_ADDR,
					 REG_CTRL1_SW_RESET_MASK, 1);
	if (err < 0)
		return err;

	msleep(200);

	err = st_lis3dhh_write_with_mask(hw, REG_CTRL1_ADDR,
					 REG_CTRL1_BDU_MASK, 1);
	if (err < 0)
		return err;

	err = st_lis3dhh_update_watermark(hw, hw->watermark);
	if (err < 0)
		return err;

	return st_lis3dhh_set_drdy_reg(hw);
}

static int st_lis3dhh_spi_probe(struct spi_device *spi)
{
	struct st_lis3dhh_hw *hw;
	struct iio_dev *iio_dev;
	int err;

	iio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, iio_dev);

	iio_dev->channels = st_lis3dhh_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_lis3dhh_channels);
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->info = &st_lis3dhh_info;
	iio_dev->dev.parent = &spi->dev;
	iio_dev->name = spi->modalias;

	hw = iio_priv(iio_dev);

	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->lock);

	hw->watermark = 1;
	hw->dev = &spi->dev;
	hw->name = spi->modalias;
	hw->irq = spi->irq;
	hw->iio_dev = iio_dev;

	err = st_lis3dhh_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_lis3dhh_init_device(hw);
	if (err < 0)
		return err;

	if (hw->irq > 0) {
		err = st_lis3dhh_fifo_setup(hw);
		if (err < 0)
			return err;
	}

	return devm_iio_device_register(hw->dev, iio_dev);
}

static const struct of_device_id st_lis3dhh_spi_of_match[] = {
	{
		.compatible = "st,lis3dhh",
		.data = LIS3DHH_DEV_NAME,
	},
	{
		.compatible = "st,iis3dhhc",
		.data = IIS3DHHC_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis3dhh_spi_of_match);

static const struct spi_device_id st_lis3dhh_spi_id_table[] = {
	{ LIS3DHH_DEV_NAME },
	{ IIS3DHHC_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lis3dhh_spi_id_table);

static struct spi_driver st_lis3dhh_driver = {
	.driver = {
		.name = "st_lis3dhh",
		.of_match_table = of_match_ptr(st_lis3dhh_spi_of_match),
	},
	.probe = st_lis3dhh_spi_probe,
	.id_table = st_lis3dhh_spi_id_table,
};
module_spi_driver(st_lis3dhh_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis3dhh sensor driver");
MODULE_LICENSE("GPL v2");
