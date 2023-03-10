// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_imu68 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2019 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "st_imu68.h"

#define ST_IMU68_WHOAMI_ADDR			0x0f
#define ST_IMU68_WHOAMI				0x68

#define ST_IMU68_REG_GYRO_ODR_ADDR		0x10
#define ST_IMU68_REG_GYRO_ODR_MASK		GENMASK(7, 5)
#define ST_IMU68_REG_GYRO_FS_ADDR		0x10
#define ST_IMU68_REG_GYRO_FS_MASK		GENMASK(4, 3)
#define ST_IMU68_REG_CTRL4_ADDR			0x1e
#define ST_IMU68_GYRO_EN_MASK			GENMASK(5, 3)
#define ST_IMU68_REG_CTRL5_ADDR			0x1f
#define ST_IMU68_ACC_EN_MASK			GENMASK(5, 3)
#define ST_IMU68_REG_ACC_ODR_ADDR		0x20
#define ST_IMU68_REG_ACC_ODR_MASK		GENMASK(7, 5)
#define ST_IMU68_REG_ACC_FS_ADDR		0x20
#define ST_IMU68_REG_ACC_FS_MASK		GENMASK(4, 3)

#define ST_IMU68_INT_ACC_DRDY_MASK		BIT(0)
#define ST_IMU68_ACC_STATUS_MASK		BIT(0)
#define ST_IMU68_INT_GYRO_DRDY_MASK		BIT(1)
#define ST_IMU68_GYRO_STATUS_MASK		BIT(1)

#define ST_IMU68_REG_GYRO_OUT_X_L_ADDR		0x18
#define ST_IMU68_REG_GYRO_OUT_Y_L_ADDR		0x1a
#define ST_IMU68_REG_GYRO_OUT_Z_L_ADDR		0x1c

#define ST_IMU68_REG_ACC_OUT_X_L_ADDR		0x28
#define ST_IMU68_REG_ACC_OUT_Y_L_ADDR		0x2a
#define ST_IMU68_REG_ACC_OUT_Z_L_ADDR		0x2c

#define ST_IMU68_ACC_FS_2G_GAIN			IIO_G_TO_M_S_2(61000)
#define ST_IMU68_ACC_FS_4G_GAIN			IIO_G_TO_M_S_2(122000)
#define ST_IMU68_ACC_FS_8G_GAIN			IIO_G_TO_M_S_2(244000)
#define ST_IMU68_ACC_FS_16G_GAIN		IIO_G_TO_M_S_2(732000)

#define ST_IMU68_GYRO_FS_250_GAIN		IIO_DEGREE_TO_RAD(8750000)
#define ST_IMU68_GYRO_FS_500_GAIN		IIO_DEGREE_TO_RAD(17500000)
#define ST_IMU68_GYRO_FS_2000_GAIN		IIO_DEGREE_TO_RAD(70000000)

struct st_imu68_odr {
	u16 hz;
	u8 val;
};

#define ST_IMU68_ODR_LIST_SIZE		5
struct st_imu68_odr_table_entry {
	struct st_imu68_reg reg;
	struct st_imu68_odr odr_avl[ST_IMU68_ODR_LIST_SIZE];
};

static const struct st_imu68_odr_table_entry st_imu68_odr_table[] = {
	[ST_IMU68_ID_ACC] = {
		.reg = {
			.addr = ST_IMU68_REG_ACC_ODR_ADDR,
			.mask = ST_IMU68_REG_ACC_ODR_MASK,
		},
		.odr_avl[0] = {  10, 0x01 },
		.odr_avl[1] = {  50, 0x02 },
		.odr_avl[2] = { 119, 0x03 },
		.odr_avl[3] = { 238, 0x04 },
		.odr_avl[4] = { 476, 0x05 },
	},
	[ST_IMU68_ID_GYRO] = {
		.reg = {
			.addr = ST_IMU68_REG_GYRO_ODR_ADDR,
			.mask = ST_IMU68_REG_GYRO_ODR_MASK,
		},
		.odr_avl[0] = {  15, 0x01 },
		.odr_avl[1] = {  60, 0x02 },
		.odr_avl[2] = { 119, 0x03 },
		.odr_avl[3] = { 238, 0x04 },
		.odr_avl[4] = { 476, 0x05 },
	}
};

struct st_imu68_fs {
	u32 gain;
	u8 val;
};

#define ST_IMU68_FS_LIST_SIZE		4
struct st_imu68_fs_table_entry {
	struct st_imu68_reg reg;
	struct st_imu68_fs fs_avl[ST_IMU68_FS_LIST_SIZE];
	u8 depth;
};

static const struct st_imu68_fs_table_entry st_imu68_fs_table[] = {
	[ST_IMU68_ID_ACC] = {
		.reg = {
			.addr = ST_IMU68_REG_ACC_FS_ADDR,
			.mask = ST_IMU68_REG_ACC_FS_MASK,
		},
		.fs_avl[0] = {  ST_IMU68_ACC_FS_2G_GAIN, 0x0 },
		.fs_avl[1] = {  ST_IMU68_ACC_FS_4G_GAIN, 0x2 },
		.fs_avl[2] = {  ST_IMU68_ACC_FS_8G_GAIN, 0x3 },
		.fs_avl[3] = { ST_IMU68_ACC_FS_16G_GAIN, 0x1 },
	},
	[ST_IMU68_ID_GYRO] = {
		.reg = {
			.addr = ST_IMU68_REG_GYRO_FS_ADDR,
			.mask = ST_IMU68_REG_GYRO_FS_MASK,
		},
		.fs_avl[0] = {  ST_IMU68_GYRO_FS_250_GAIN, 0x0 },
		.fs_avl[1] = {  ST_IMU68_GYRO_FS_500_GAIN, 0x1 },
		.fs_avl[2] = { ST_IMU68_GYRO_FS_2000_GAIN, 0x3 },
	}
};

#define ST_IMU68_CHANNEL(chan_type, addr, mod, scan_idx)	\
{								\
	.type = chan_type,					\
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

static const struct iio_chan_spec st_imu68_acc_channels[] = {
	ST_IMU68_CHANNEL(IIO_ACCEL, ST_IMU68_REG_ACC_OUT_X_L_ADDR,
			 IIO_MOD_X, 0),
	ST_IMU68_CHANNEL(IIO_ACCEL, ST_IMU68_REG_ACC_OUT_Y_L_ADDR,
			 IIO_MOD_Y, 1),
	ST_IMU68_CHANNEL(IIO_ACCEL, ST_IMU68_REG_ACC_OUT_Z_L_ADDR,
			 IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_imu68_gyro_channels[] = {
	ST_IMU68_CHANNEL(IIO_ANGL_VEL, ST_IMU68_REG_GYRO_OUT_X_L_ADDR,
			 IIO_MOD_X, 0),
	ST_IMU68_CHANNEL(IIO_ANGL_VEL, ST_IMU68_REG_GYRO_OUT_Y_L_ADDR,
			 IIO_MOD_Y, 1),
	ST_IMU68_CHANNEL(IIO_ANGL_VEL, ST_IMU68_REG_GYRO_OUT_Z_L_ADDR,
			 IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static inline int st_imu68_claim_direct_mode(struct iio_dev *iio_dev)
{
	mutex_lock(&iio_dev->mlock);

	if (iio_buffer_enabled(iio_dev)) {
		mutex_unlock(&iio_dev->mlock);
		return -EBUSY;
	}
	return 0;
}

static inline void st_imu68_release_direct_mode(struct iio_dev *iio_dev)
{
	mutex_unlock(&iio_dev->mlock);
}

int st_imu68_write_with_mask(struct st_imu68_hw *hw, u8 addr, u8 mask, u8 val)
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

static int st_imu68_check_whoami(struct st_imu68_hw *hw)
{
	int err;
	u8 data;

	err = hw->tf->read(hw->dev, ST_IMU68_WHOAMI_ADDR, sizeof(data),
			   &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_IMU68_WHOAMI) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	return 0;
}

static int st_imu68_set_full_scale(struct st_imu68_sensor *sensor, u32 gain)
{
	enum st_imu68_sensor_id id = sensor->id;
	int i, err;
	u8 val;

	for (i = 0; i < ST_IMU68_FS_LIST_SIZE; i++)
		if ((st_imu68_fs_table[id].fs_avl[i].gain == gain) &&
		    st_imu68_fs_table[id].fs_avl[i].gain)
			break;

	if (i == ST_IMU68_FS_LIST_SIZE)
		return -EINVAL;

	val = st_imu68_fs_table[id].fs_avl[i].val;
	err = st_imu68_write_with_mask(sensor->hw,
				       st_imu68_fs_table[id].reg.addr,
				       st_imu68_fs_table[id].reg.mask, val);
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static int st_imu68_set_odr(struct st_imu68_sensor *sensor, u16 odr)
{
	enum st_imu68_sensor_id id = sensor->id;
	int i, err;
	u8 val;

	for (i = 0; i < ST_IMU68_ODR_LIST_SIZE; i++)
		if (st_imu68_odr_table[id].odr_avl[i].hz >= odr)
			break;

	if (i == ST_IMU68_ODR_LIST_SIZE)
		return -EINVAL;

	val = st_imu68_odr_table[id].odr_avl[i].val;
	err = st_imu68_write_with_mask(sensor->hw,
				       st_imu68_odr_table[id].reg.addr,
				       st_imu68_odr_table[id].reg.mask, val);
	if (err < 0)
		return err;

	sensor->odr = st_imu68_odr_table[id].odr_avl[i].hz;

	return 0;
}

static ssize_t
st_imu68_sysfs_sampling_frequency_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct st_imu68_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_imu68_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < ST_IMU68_ODR_LIST_SIZE; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_imu68_odr_table[id].odr_avl[i].hz);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_imu68_sysfs_scale_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct st_imu68_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_imu68_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < ST_IMU68_FS_LIST_SIZE; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 st_imu68_fs_table[id].fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static int st_imu68_read_oneshot(struct st_imu68_sensor *sensor, u8 addr,
				 int *val)
{
	int err, delay;
	__le16 data;

	err = st_imu68_sensor_enable(sensor, true);
	if (err < 0)
		return err;

	delay = 1000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = sensor->hw->tf->read(sensor->hw->dev, addr, sizeof(data),
				   (u8 *)&data);
	if (err < 0)
		return err;

	st_imu68_sensor_enable(sensor, false);

	*val = (s16)data;

	return IIO_VAL_INT;
}

static int st_imu68_read_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *ch,
			     int *val, int *val2, long mask)
{
	struct st_imu68_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = st_imu68_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_imu68_read_oneshot(sensor, ch->address, val);
		st_imu68_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->gain;
		ret = IIO_VAL_INT_PLUS_NANO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_imu68_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	if (mask == IIO_CHAN_INFO_SCALE) {
		if ((chan->type == IIO_ANGL_VEL) ||
		    (chan->type == IIO_ACCEL))
			return IIO_VAL_INT_PLUS_NANO;
	}

	return -EINVAL;
}

static int st_imu68_write_raw(struct iio_dev *iio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct st_imu68_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = st_imu68_claim_direct_mode(iio_dev);
	if (err)
		return err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_imu68_set_full_scale(sensor, val2);
		break;
	default:
		err = -EINVAL;
		break;
	}

	st_imu68_release_direct_mode(iio_dev);

	return err;
}

int st_imu68_sensor_enable(struct st_imu68_sensor *sensor, bool enable)
{
	int err;
	enum st_imu68_sensor_id id = sensor->id;

	if (enable) {
		u16 odr;
		struct st_imu68_sensor *sensor_acc =
			iio_priv(sensor->hw->iio_devs[ST_IMU68_ID_ACC]);

		/* Check if Gyro enabling with Acc already on */
		if ((id == ST_IMU68_ID_GYRO) &&
		    (sensor->hw->enabled_mask & BIT(ST_IMU68_ID_ACC))) {
			odr = max(sensor->odr, sensor_acc->odr);
		} else {
			odr = sensor->odr;
		}

		err = st_imu68_set_odr(sensor, odr);
		if (err < 0)
			return err;

		sensor->hw->enabled_mask |= BIT(id);
	} else {
		err = st_imu68_write_with_mask(sensor->hw,
					       st_imu68_odr_table[id].reg.addr,
					       st_imu68_odr_table[id].reg.mask,
					       0);
		if (err < 0)
			return err;

		sensor->hw->enabled_mask &= ~BIT(id);
	}

	return 0;
}

static ssize_t st_imu68_get_sampling_frequency(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct st_imu68_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->odr);
}

static ssize_t st_imu68_set_sampling_frequency(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct st_imu68_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	int err, odr;

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	err = st_imu68_set_odr(sensor, odr);

	return err < 0 ? err : count;
}

ssize_t st_imu68_get_module_id(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_imu68_sensor *sensor = iio_priv(iio_dev);
	struct st_imu68_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->module_id);
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_imu68_sysfs_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_imu68_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_imu68_sysfs_scale_avail, NULL, 0);
static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      st_imu68_get_sampling_frequency,
			      st_imu68_set_sampling_frequency);
static IIO_DEVICE_ATTR(module_id, 0444, st_imu68_get_module_id, NULL, 0);

static struct attribute *st_imu68_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_imu68_acc_attribute_group = {
	.attrs = st_imu68_acc_attributes,
};

static const struct iio_info st_imu68_acc_info = {
	.attrs = &st_imu68_acc_attribute_group,
	.read_raw = st_imu68_read_raw,
	.write_raw = st_imu68_write_raw,
	.write_raw_get_fmt = st_imu68_write_raw_get_fmt,
};

static struct attribute *st_imu68_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_imu68_gyro_attribute_group = {
	.attrs = st_imu68_gyro_attributes,
};

static const struct iio_info st_imu68_gyro_info = {
	.attrs = &st_imu68_gyro_attribute_group,
	.read_raw = st_imu68_read_raw,
	.write_raw = st_imu68_write_raw,
	.write_raw_get_fmt = st_imu68_write_raw_get_fmt,
};

static const unsigned long st_imu68_available_scan_masks[] = {0x7, 0x0};

static struct iio_dev *st_imu68_alloc_iiodev(struct st_imu68_hw *hw,
					     enum st_imu68_sensor_id id)
{
	struct st_imu68_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;
	iio_dev->available_scan_masks = st_imu68_available_scan_masks;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->odr = st_imu68_odr_table[id].odr_avl[0].hz;
	sensor->gain = st_imu68_fs_table[id].fs_avl[0].gain;

	switch (id) {
	case ST_IMU68_ID_ACC:
		iio_dev->channels = st_imu68_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_imu68_acc_channels);
		iio_dev->name = kasprintf(GFP_KERNEL, "%s_accel", hw->name);
		iio_dev->info = &st_imu68_acc_info;

		sensor->drdy_mask = ST_IMU68_INT_ACC_DRDY_MASK;
		sensor->status_mask = ST_IMU68_ACC_STATUS_MASK;
		break;
	case ST_IMU68_ID_GYRO:
		iio_dev->channels = st_imu68_gyro_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_imu68_gyro_channels);
		iio_dev->name = kasprintf(GFP_KERNEL, "%s_gyro", hw->name);
		iio_dev->info = &st_imu68_gyro_info;

		sensor->drdy_mask = ST_IMU68_INT_GYRO_DRDY_MASK;
		sensor->status_mask = ST_IMU68_GYRO_STATUS_MASK;
		break;
	default:
		return NULL;
	}

	return iio_dev;
}

static void st_imu68_get_properties(struct st_imu68_hw *hw)
{
	if (device_property_read_u32(hw->dev, "st,module_id",
				     &hw->module_id)) {
		hw->module_id = 1;
	}
}

int st_imu68_probe(struct device *dev, int irq, const char *name,
		   const struct st_imu68_transfer_function *tf_ops)
{
	struct st_imu68_hw *hw;
	int err, i;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->lock);
	hw->name = name;
	hw->dev = dev;
	hw->irq = irq;
	hw->tf = tf_ops;
	hw->enabled_mask = 0;

	err = st_imu68_check_whoami(hw);
	if (err < 0)
		return err;

	st_imu68_get_properties(hw);

	err = st_imu68_write_with_mask(hw, ST_IMU68_REG_CTRL4_ADDR,
				       ST_IMU68_GYRO_EN_MASK, 0x7);
	if (err < 0)
		return err;

	err = st_imu68_write_with_mask(hw, ST_IMU68_REG_CTRL5_ADDR,
				       ST_IMU68_ACC_EN_MASK, 0x7);
	if (err < 0)
		return err;

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		hw->iio_devs[i] = st_imu68_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	if (irq > 0) {
		err = st_imu68_allocate_buffers(hw);
		if (err)
			return err;

		err = st_imu68_allocate_triggers(hw);
		if (err)
			goto deallocate_iio_buffers;
	}

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			goto deallocate_iio_triggers;
	}

	return 0;

deallocate_iio_triggers:
	if (irq > 0)
		st_imu68_deallocate_triggers(hw);
deallocate_iio_buffers:
	if (irq > 0)
		st_imu68_deallocate_buffers(hw);

	return err;
}
EXPORT_SYMBOL(st_imu68_probe);

int st_imu68_remove(struct device *dev)
{
	struct st_imu68_hw *hw = dev_get_drvdata(dev);

	if (hw->irq) {
		st_imu68_deallocate_triggers(hw);
		st_imu68_deallocate_buffers(hw);
	}

	return 0;
}
EXPORT_SYMBOL(st_imu68_remove);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_imu68 driver");
MODULE_LICENSE("GPL v2");
