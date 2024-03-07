// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics mag3d driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "st_mag3d.h"

#define ST_MAG3D_WHOAMI_ADDR		0x0f
#define ST_MAG3D_DEF_WHOAMI		0x3d

#define ST_MAG3D_CTRL1_ADDR		0x20
#define ST_MAG3D_ODR_MASK		0x1c
#define ST_MAG3D_OM_XY_MASK		0x60
#define ST_MAG3D_CTRL2_ADDR		0x21
#define ST_MAG3D_FS_MASK		0x60
#define ST_MAG3D_CTRL3_ADDR		0x22
#define ST_MAG3D_PWR_MASK		0x03
#define ST_MAG3D_PWR_ON			0x00
#define ST_MAG3D_PWR_OFF		0x03
#define ST_MAG3D_CTRL4_ADDR		0x23
#define ST_MAG3D_OM_Z_MASK		0x0c
#define ST_MAG3D_CTRL5_ADDR		0x24
#define ST_MAG3D_BDU_MASK		0x40

#define ST_MAG3D_OUT_X_L_ADDR		0x28
#define ST_MAG3D_OUT_Y_L_ADDR		0x2a
#define ST_MAG3D_OUT_Z_L_ADDR		0x2c

struct st_mag3d_odr {
	unsigned int hz;
	u8 val;
};

const struct st_mag3d_odr st_mag3d_odr_table[] = {
	{  1, 0x01 },
	{  3, 0x02 },
	{  5, 0x03 },
	{ 10, 0x04 },
	{ 20, 0x05 },
	{ 40, 0x06 },
	{ 80, 0x07 },
};

const u8 st_mag3d_stodis_table[] = {
	0, /* 1Hz */
	1, /* 3Hz */
	2, /* 5Hz */
	5, /* 10Hz */
	6, /* 20Hz */
	6, /* 40Hz */
	7, /* 80Hz */
};

struct st_mag3d_fs {
	unsigned int gain;
	u8 val;
};

const struct st_mag3d_fs st_mag3d_fs_table[] = {
	{ 146, 0x0 }, /* 4000 */
	{ 292, 0x1 }, /* 8000 */
	{ 438, 0x2 }, /* 12000 */
	{ 584, 0x3 }, /* 16000 */
};

#define ST_MAG3D_CHANNEL(addr, mod, scan_idx)			\
{								\
	.type = IIO_MAGN,					\
	.address = addr,					\
	.modified = 1,						\
	.channel2 = mod,					\
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

static const struct iio_chan_spec st_mag3d_channels[] = {
	ST_MAG3D_CHANNEL(ST_MAG3D_OUT_X_L_ADDR, IIO_MOD_X, 0),
	ST_MAG3D_CHANNEL(ST_MAG3D_OUT_Y_L_ADDR, IIO_MOD_Y, 1),
	ST_MAG3D_CHANNEL(ST_MAG3D_OUT_Z_L_ADDR, IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static inline int st_mag3d_claim_direct_mode(struct iio_dev *iio_dev)
{
	mutex_lock(&iio_dev->mlock);

	if (iio_buffer_enabled(iio_dev)) {
		mutex_unlock(&iio_dev->mlock);
		return -EBUSY;
	}

	return 0;
}

static inline void st_mag3d_release_direct_mode(struct iio_dev *iio_dev)
{
	mutex_unlock(&iio_dev->mlock);
}

static int st_mag3d_write_with_mask(struct st_mag3d_hw *hw, u8 addr,
				    u8 mask, u8 val)
{
	u8 data;
	int err;

	mutex_lock(&hw->lock);

	err = hw->tf->read(hw->dev, addr, sizeof(data), &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %02x register\n", addr);
		goto out;
	}

	data = (data & ~mask) | ((val << __ffs(mask)) & mask);

	err = hw->tf->write(hw->dev, addr, sizeof(data), &data);
	if (err < 0)
		dev_err(hw->dev, "failed to write %02x register\n", addr);

out:
	mutex_unlock(&hw->lock);

	return err;
}

static int st_mag3d_set_odr(struct st_mag3d_hw *hw, unsigned int odr)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_mag3d_odr_table); i++) {
		if (odr == st_mag3d_odr_table[i].hz)
			break;
	}

	if (i == ARRAY_SIZE(st_mag3d_odr_table))
		return -EINVAL;

	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL1_ADDR,
				       ST_MAG3D_ODR_MASK,
				       st_mag3d_odr_table[i].val);
	if (err < 0)
		return err;

	hw->odr = odr;
	hw->stodis = st_mag3d_stodis_table[i];
	hw->delta_ts = div_s64(1000000000LL, odr);

	return 0;
}

static int st_mag3d_set_fullscale(struct st_mag3d_hw *hw, unsigned int gain)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_mag3d_fs_table); i++) {
		if (gain == st_mag3d_fs_table[i].gain)
			break;
	}

	if (i == ARRAY_SIZE(st_mag3d_fs_table))
		return -EINVAL;

	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL2_ADDR,
				       ST_MAG3D_FS_MASK,
				       st_mag3d_fs_table[i].val);
	if (err < 0)
		return err;

	hw->gain = gain;

	return 0;
}

int st_mag3d_enable_sensor(struct st_mag3d_hw *hw, bool enable)
{
	u8 val = enable ? ST_MAG3D_PWR_ON : ST_MAG3D_PWR_OFF;
	int err;

	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL3_ADDR,
				       ST_MAG3D_PWR_MASK, val);
	if (enable) {
		hw->timestamp = st_mag3d_get_time_ns(hw->iio_dev);
		hw->mag_ts = hw->timestamp;
	}

	return err < 0 ? err : 0;
}

static int st_mag3d_check_whoami(struct st_mag3d_hw *hw)
{
	int err;
	u8 data;

	err = hw->tf->read(hw->dev, ST_MAG3D_WHOAMI_ADDR, sizeof(data),
			   &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_MAG3D_DEF_WHOAMI) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	return 0;
}

static int st_mag3d_read_oneshot(struct st_mag3d_hw *hw,
				 u8 addr, int *val)
{
	int err, delay;
	__le16 data;

	err = st_mag3d_enable_sensor(hw, true);
	if (err < 0)
		return err;

	delay = 1000000 / hw->odr;
	usleep_range(delay, 2 * delay);

	err = hw->tf->read(hw->dev, addr, sizeof(data), (u8 *)&data);
	if (err < 0)
		return err;

	err = st_mag3d_enable_sensor(hw, false);
	if (err < 0)
		return err;

	*val = (s16)data;

	return IIO_VAL_INT;
}

static int st_mag3d_read_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *ch,
			     int *val, int *val2, long mask)
{
	struct st_mag3d_hw *hw = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = st_mag3d_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_mag3d_read_oneshot(hw, ch->address, val);
		st_mag3d_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = hw->gain;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_mag3d_write_raw(struct iio_dev *iio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct st_mag3d_hw *hw = iio_priv(iio_dev);
	int ret;

	ret = st_mag3d_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = st_mag3d_set_fullscale(hw, val2);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	st_mag3d_release_direct_mode(iio_dev);

	return ret;
}

static ssize_t st_mag3d_get_sampling_frequency(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct st_mag3d_hw *hw = iio_priv(dev_get_drvdata(dev));

	return sprintf(buf, "%d\n", hw->odr);
}

static ssize_t st_mag3d_set_sampling_frequency(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf,
					       size_t count)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag3d_hw *hw = iio_priv(iio_dev);
	unsigned int odr;
	int err;

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	err = st_mag3d_set_odr(hw, odr);

	return err < 0 ? err : count;
}

static ssize_t
st_mag3d_get_sampling_frequency_avail(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st_mag3d_odr_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_mag3d_odr_table[i].hz);
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ(0644,
			      st_mag3d_get_sampling_frequency,
			      st_mag3d_set_sampling_frequency);
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_mag3d_get_sampling_frequency_avail);

static struct attribute *st_mag3d_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_mag3d_attribute_group = {
	.attrs = st_mag3d_attributes,
};

static const struct iio_info st_mag3d_info = {
	.read_raw = st_mag3d_read_raw,
	.write_raw = st_mag3d_write_raw,
	.attrs = &st_mag3d_attribute_group,
};

static const unsigned long st_mag3d_available_scan_masks[] = {0x7, 0x0};

int st_mag3d_probe(struct device *dev, int irq, const char *name,
		   const struct st_mag3d_transfer_function *tf_ops)
{
	struct st_mag3d_hw *hw;
	struct iio_dev *iio_dev;
	int err;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, iio_dev);

	iio_dev->dev.parent = dev;
	iio_dev->name = name;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->available_scan_masks = st_mag3d_available_scan_masks;
	iio_dev->channels = st_mag3d_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_mag3d_channels);
	iio_dev->info = &st_mag3d_info;

	hw = iio_priv(iio_dev);
	mutex_init(&hw->lock);
	hw->dev = dev;
	hw->tf = tf_ops;
	hw->irq = irq;
	hw->iio_dev = iio_dev;

	err = st_mag3d_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_mag3d_set_odr(hw, st_mag3d_odr_table[0].hz);
	if (err < 0)
		return err;

	err = st_mag3d_set_fullscale(hw, st_mag3d_fs_table[0].gain);
	if (err < 0)
		return err;

	/* enable BDU */
	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL5_ADDR,
				       ST_MAG3D_BDU_MASK, 1);
	if (err < 0)
		return err;

	/* enable OM mode */
	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL1_ADDR,
				       ST_MAG3D_OM_XY_MASK, 3);
	if (err < 0)
		return err;

	err = st_mag3d_write_with_mask(hw, ST_MAG3D_CTRL4_ADDR,
				       ST_MAG3D_OM_Z_MASK, 3);
	if (err < 0)
		return err;

	if (irq > 0) {
		err = st_mag3d_allocate_buffer(iio_dev);
		if (err < 0)
			return err;

		err = st_mag3d_allocate_trigger(iio_dev);
		if (err < 0) {
			st_mag3d_deallocate_buffer(iio_dev);
			return err;
		}
	}

	err = devm_iio_device_register(dev, iio_dev);
	if (err < 0 && irq > 0) {
		st_mag3d_deallocate_trigger(iio_dev);
		st_mag3d_deallocate_buffer(iio_dev);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(st_mag3d_probe);

void st_mag3d_remove(struct iio_dev *iio_dev)
{
	struct st_mag3d_hw *hw = iio_priv(iio_dev);

	if (hw->irq > 0) {
		st_mag3d_deallocate_trigger(iio_dev);
		st_mag3d_deallocate_buffer(iio_dev);
	}
}
EXPORT_SYMBOL(st_mag3d_remove);

MODULE_DESCRIPTION("STMicroelectronics mag3d driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
