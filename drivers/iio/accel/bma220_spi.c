// SPDX-License-Identifier: GPL-2.0-only
/*
 * BMA220 Digital triaxial acceleration sensor driver
 *
 * Copyright (c) 2016,2020 Intel Corporation.
 */

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define BMA220_REG_ID				0x00
#define BMA220_REG_ACCEL_X			0x02
#define BMA220_REG_ACCEL_Y			0x03
#define BMA220_REG_ACCEL_Z			0x04
#define BMA220_REG_RANGE			0x11
#define BMA220_REG_SUSPEND			0x18

#define BMA220_CHIP_ID				0xDD
#define BMA220_READ_MASK			BIT(7)
#define BMA220_RANGE_MASK			GENMASK(1, 0)
#define BMA220_SUSPEND_SLEEP			0xFF
#define BMA220_SUSPEND_WAKE			0x00

#define BMA220_DEVICE_NAME			"bma220"

#define BMA220_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = index,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 6,						\
		.storagebits = 8,					\
		.shift = 2,						\
		.endianness = IIO_CPU,					\
	},								\
}

enum bma220_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

static const int bma220_scale_table[][2] = {
	{0, 623000}, {1, 248000}, {2, 491000}, {4, 983000},
};

struct bma220_data {
	struct spi_device *spi_device;
	struct mutex lock;
	struct {
		s8 chans[3];
		/* Ensure timestamp is naturally aligned. */
		s64 timestamp __aligned(8);
	} scan;
	u8 tx_buf[2] __aligned(IIO_DMA_MINALIGN);
};

static const struct iio_chan_spec bma220_channels[] = {
	BMA220_ACCEL_CHANNEL(0, BMA220_REG_ACCEL_X, X),
	BMA220_ACCEL_CHANNEL(1, BMA220_REG_ACCEL_Y, Y),
	BMA220_ACCEL_CHANNEL(2, BMA220_REG_ACCEL_Z, Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static inline int bma220_read_reg(struct spi_device *spi, u8 reg)
{
	return spi_w8r8(spi, reg | BMA220_READ_MASK);
}

static const unsigned long bma220_accel_scan_masks[] = {
	BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
	0
};

static irqreturn_t bma220_trigger_handler(int irq, void *p)
{
	int ret;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bma220_data *data = iio_priv(indio_dev);
	struct spi_device *spi = data->spi_device;

	mutex_lock(&data->lock);
	data->tx_buf[0] = BMA220_REG_ACCEL_X | BMA220_READ_MASK;
	ret = spi_write_then_read(spi, data->tx_buf, 1, &data->scan.chans,
				  ARRAY_SIZE(bma220_channels) - 1);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   pf->timestamp);
err:
	mutex_unlock(&data->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bma220_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	u8 range_idx;
	struct bma220_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = bma220_read_reg(data->spi_device, chan->address);
		if (ret < 0)
			return -EINVAL;
		*val = sign_extend32(ret >> chan->scan_type.shift,
				     chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = bma220_read_reg(data->spi_device, BMA220_REG_RANGE);
		if (ret < 0)
			return ret;
		range_idx = ret & BMA220_RANGE_MASK;
		*val = bma220_scale_table[range_idx][0];
		*val2 = bma220_scale_table[range_idx][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int bma220_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int i;
	int ret;
	int index = -1;
	struct bma220_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ARRAY_SIZE(bma220_scale_table); i++)
			if (val == bma220_scale_table[i][0] &&
			    val2 == bma220_scale_table[i][1]) {
				index = i;
				break;
			}
		if (index < 0)
			return -EINVAL;

		mutex_lock(&data->lock);
		data->tx_buf[0] = BMA220_REG_RANGE;
		data->tx_buf[1] = index;
		ret = spi_write(data->spi_device, data->tx_buf,
				sizeof(data->tx_buf));
		if (ret < 0)
			dev_err(&data->spi_device->dev,
				"failed to set measurement range\n");
		mutex_unlock(&data->lock);

		return 0;
	}

	return -EINVAL;
}

static int bma220_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)bma220_scale_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(bma220_scale_table) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bma220_info = {
	.read_raw		= bma220_read_raw,
	.write_raw		= bma220_write_raw,
	.read_avail		= bma220_read_avail,
};

static int bma220_init(struct spi_device *spi)
{
	int ret;

	ret = bma220_read_reg(spi, BMA220_REG_ID);
	if (ret != BMA220_CHIP_ID)
		return -ENODEV;

	/* Make sure the chip is powered on */
	ret = bma220_read_reg(spi, BMA220_REG_SUSPEND);
	if (ret == BMA220_SUSPEND_WAKE)
		ret = bma220_read_reg(spi, BMA220_REG_SUSPEND);
	if (ret < 0)
		return ret;
	if (ret == BMA220_SUSPEND_WAKE)
		return -EBUSY;

	return 0;
}

static int bma220_power(struct spi_device *spi, bool up)
{
	int i, ret;

	/**
	 * The chip can be suspended/woken up by a simple register read.
	 * So, we need up to 2 register reads of the suspend register
	 * to make sure that the device is in the desired state.
	 */
	for (i = 0; i < 2; i++) {
		ret = bma220_read_reg(spi, BMA220_REG_SUSPEND);
		if (ret < 0)
			return ret;

		if (up && ret == BMA220_SUSPEND_SLEEP)
			return 0;

		if (!up && ret == BMA220_SUSPEND_WAKE)
			return 0;
	}

	return -EBUSY;
}

static void bma220_deinit(void *spi)
{
	bma220_power(spi, false);
}

static int bma220_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev;
	struct bma220_data *data;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&spi->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->spi_device = spi;
	mutex_init(&data->lock);

	indio_dev->info = &bma220_info;
	indio_dev->name = BMA220_DEVICE_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = bma220_channels;
	indio_dev->num_channels = ARRAY_SIZE(bma220_channels);
	indio_dev->available_scan_masks = bma220_accel_scan_masks;

	ret = bma220_init(data->spi_device);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, bma220_deinit, spi);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					      iio_pollfunc_store_time,
					      bma220_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&spi->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static int bma220_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	return bma220_power(spi, false);
}

static int bma220_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	return bma220_power(spi, true);
}
static DEFINE_SIMPLE_DEV_PM_OPS(bma220_pm_ops, bma220_suspend, bma220_resume);

static const struct spi_device_id bma220_spi_id[] = {
	{"bma220", 0},
	{}
};

static const struct acpi_device_id bma220_acpi_id[] = {
	{"BMA0220", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, bma220_spi_id);

static struct spi_driver bma220_driver = {
	.driver = {
		.name = "bma220_spi",
		.pm = pm_sleep_ptr(&bma220_pm_ops),
		.acpi_match_table = bma220_acpi_id,
	},
	.probe =            bma220_probe,
	.id_table =         bma220_spi_id,
};
module_spi_driver(bma220_driver);

MODULE_AUTHOR("Tiberiu Breana <tiberiu.a.breana@intel.com>");
MODULE_DESCRIPTION("BMA220 acceleration sensor driver");
MODULE_LICENSE("GPL v2");
