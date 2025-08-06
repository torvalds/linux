// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO accel core driver for Freescale MMA7455L 3-axis 10-bit accelerometer
 * Copyright 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * UNSUPPORTED hardware features:
 *  - 8-bit mode with different scales
 *  - INT1/INT2 interrupts
 *  - Offset calibration
 *  - Events
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "mma7455.h"

#define MMA7455_REG_XOUTL		0x00
#define MMA7455_REG_XOUTH		0x01
#define MMA7455_REG_YOUTL		0x02
#define MMA7455_REG_YOUTH		0x03
#define MMA7455_REG_ZOUTL		0x04
#define MMA7455_REG_ZOUTH		0x05
#define MMA7455_REG_STATUS		0x09
#define  MMA7455_STATUS_DRDY		BIT(0)
#define MMA7455_REG_WHOAMI		0x0f
#define  MMA7455_WHOAMI_ID		0x55
#define MMA7455_REG_MCTL		0x16
#define  MMA7455_MCTL_MODE_STANDBY	0x00
#define  MMA7455_MCTL_MODE_MEASURE	0x01
#define MMA7455_REG_CTL1		0x18
#define  MMA7455_CTL1_DFBW_MASK		BIT(7)
#define  MMA7455_CTL1_DFBW_125HZ	BIT(7)
#define  MMA7455_CTL1_DFBW_62_5HZ	0
#define MMA7455_REG_TW			0x1e

/*
 * When MMA7455 is used in 10-bit it has a fullscale of -8g
 * corresponding to raw value -512. The userspace interface
 * uses m/s^2 and we declare micro units.
 * So scale factor is given by:
 *       g * 8 * 1e6 / 512 = 153228.90625, with g = 9.80665
 */
#define MMA7455_10BIT_SCALE	153229

struct mma7455_data {
	struct regmap *regmap;
	/*
	 * Used to reorganize data.  Will ensure correct alignment of
	 * the timestamp if present
	 */
	struct {
		__le16 channels[3];
		aligned_s64 ts;
	} scan;
};

static int mma7455_drdy(struct mma7455_data *mma7455)
{
	struct device *dev = regmap_get_device(mma7455->regmap);
	unsigned int reg;
	int tries = 3;
	int ret;

	while (tries-- > 0) {
		ret = regmap_read(mma7455->regmap, MMA7455_REG_STATUS, &reg);
		if (ret)
			return ret;

		if (reg & MMA7455_STATUS_DRDY)
			return 0;

		msleep(20);
	}

	dev_warn(dev, "data not ready\n");

	return -EIO;
}

static irqreturn_t mma7455_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mma7455_data *mma7455 = iio_priv(indio_dev);
	int ret;

	ret = mma7455_drdy(mma7455);
	if (ret)
		goto done;

	ret = regmap_bulk_read(mma7455->regmap, MMA7455_REG_XOUTL,
			       mma7455->scan.channels,
			       sizeof(mma7455->scan.channels));
	if (ret)
		goto done;

	iio_push_to_buffers_with_ts(indio_dev, &mma7455->scan,
				    sizeof(mma7455->scan),
				    iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int mma7455_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mma7455_data *mma7455 = iio_priv(indio_dev);
	unsigned int reg;
	__le16 data;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		ret = mma7455_drdy(mma7455);
		if (ret)
			return ret;

		ret = regmap_bulk_read(mma7455->regmap, chan->address, &data,
				       sizeof(data));
		if (ret)
			return ret;

		*val = sign_extend32(le16_to_cpu(data),
				     chan->scan_type.realbits - 1);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = MMA7455_10BIT_SCALE;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(mma7455->regmap, MMA7455_REG_CTL1, &reg);
		if (ret)
			return ret;

		if (reg & MMA7455_CTL1_DFBW_MASK)
			*val = 250;
		else
			*val = 125;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int mma7455_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mma7455_data *mma7455 = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val == 250 && val2 == 0)
			i = MMA7455_CTL1_DFBW_125HZ;
		else if (val == 125 && val2 == 0)
			i = MMA7455_CTL1_DFBW_62_5HZ;
		else
			return -EINVAL;

		return regmap_update_bits(mma7455->regmap, MMA7455_REG_CTL1,
					  MMA7455_CTL1_DFBW_MASK, i);

	case IIO_CHAN_INFO_SCALE:
		/* In 10-bit mode there is only one scale available */
		if (val == 0 && val2 == MMA7455_10BIT_SCALE)
			return 0;
		break;
	}

	return -EINVAL;
}

static IIO_CONST_ATTR(sampling_frequency_available, "125 250");

static struct attribute *mma7455_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mma7455_group = {
	.attrs = mma7455_attributes,
};

static const struct iio_info mma7455_info = {
	.attrs = &mma7455_group,
	.read_raw = mma7455_read_raw,
	.write_raw = mma7455_write_raw,
};

#define MMA7455_CHANNEL(axis, idx) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.address = MMA7455_REG_##axis##OUTL,\
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
				    BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 10, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	}, \
}

static const struct iio_chan_spec mma7455_channels[] = {
	MMA7455_CHANNEL(X, 0),
	MMA7455_CHANNEL(Y, 1),
	MMA7455_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const unsigned long mma7455_scan_masks[] = {0x7, 0};

const struct regmap_config mma7455_core_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MMA7455_REG_TW,
};
EXPORT_SYMBOL_NS_GPL(mma7455_core_regmap, "IIO_MMA7455");

int mma7455_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name)
{
	struct mma7455_data *mma7455;
	struct iio_dev *indio_dev;
	unsigned int reg;
	int ret;

	ret = regmap_read(regmap, MMA7455_REG_WHOAMI, &reg);
	if (ret) {
		dev_err(dev, "unable to read reg\n");
		return ret;
	}

	if (reg != MMA7455_WHOAMI_ID) {
		dev_err(dev, "device id mismatch\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*mma7455));
	if (!indio_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, indio_dev);
	mma7455 = iio_priv(indio_dev);
	mma7455->regmap = regmap;

	indio_dev->info = &mma7455_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mma7455_channels;
	indio_dev->num_channels = ARRAY_SIZE(mma7455_channels);
	indio_dev->available_scan_masks = mma7455_scan_masks;

	regmap_write(mma7455->regmap, MMA7455_REG_MCTL,
		     MMA7455_MCTL_MODE_MEASURE);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 mma7455_trigger_handler, NULL);
	if (ret) {
		dev_err(dev, "unable to setup triggered buffer\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "unable to register device\n");
		iio_triggered_buffer_cleanup(indio_dev);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mma7455_core_probe, "IIO_MMA7455");

void mma7455_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct mma7455_data *mma7455 = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	regmap_write(mma7455->regmap, MMA7455_REG_MCTL,
		     MMA7455_MCTL_MODE_STANDBY);
}
EXPORT_SYMBOL_NS_GPL(mma7455_core_remove, "IIO_MMA7455");

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("Freescale MMA7455L core accelerometer driver");
MODULE_LICENSE("GPL v2");
