// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lps22df driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/delay.h>
#include <asm/unaligned.h>

#include "st_lps22df.h"

struct st_lps22df_odr_table_t {
	u8 addr;
	u8 mask;
	u8 odr_avl[ST_LPS22DF_ODR_LIST_NUM];
};

const static struct st_lps22df_odr_table_t st_lps22df_odr_table = {
	.addr = ST_LPS22DF_CTRL_REG1_ADDR,
	.mask = ST_LPS22DF_ODR_MASK,
	.odr_avl = { 0, 1, 4, 10, 25, 50, 75, 100, 200 },
};

const static struct st_lps22df_fs_table_t st_lps22df_fs_table = {
	.addr = ST_LPS22DF_CTRL_REG2_ADDR,
	.mask = ST_LPS22DF_FS_MODE_MASK,
	.fs_avl = {
			{ ST_LPS22DF_PRESS_1260_FS_AVL_GAIN, 0 },
			{ ST_LPS22DF_PRESS_4060_FS_AVL_GAIN, 1 },
		},
};

const struct iio_event_spec st_lps22df_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_chan_spec st_lps22df_press_channels[] = {
	{
		.type = IIO_PRESSURE,
		.address = ST_LPS22DF_PRESS_OUT_XL_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_NO_MOD,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_PRESSURE,
		.scan_index = -1,
		.indexed = -1,
		.event_spec = &st_lps22df_fifo_flush_event,
		.num_event_specs = 1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_lps22df_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_LPS22DF_TEMP_OUT_L_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_NO_MOD,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
};

static const struct st_lps22df_settings st_lps22df_sensor_settings[] = {
	{
		.id = {
			{
				.hw_id = ST_LPS22DF_ID,
				.name = ST_LPS22DF_DEV_NAME,
			},
		},
		.fs_table = {
			.addr = ST_LPS22DF_CTRL_REG2_ADDR,
			.mask = ST_LPS22DF_FS_MODE_MASK,
			.fs_len = 1,
			.fs_avl = {
				{ ST_LPS22DF_PRESS_1260_FS_AVL_GAIN, 0 },
			},
		},
		.st_multi_scale = false,
	},
	{
		.id = {
			{
				.hw_id = ST_LPS28DFW_ID,
				.name = ST_LPS28DFW_DEV_NAME,
			},
		},
		.fs_table = {
			.addr = ST_LPS22DF_CTRL_REG2_ADDR,
			.mask = ST_LPS22DF_FS_MODE_MASK,
			.fs_len = 2,
			.fs_avl = {
				{ ST_LPS22DF_PRESS_1260_FS_AVL_GAIN, 0 },
				{ ST_LPS22DF_PRESS_4060_FS_AVL_GAIN, 1 },
			},
		},
		.st_multi_scale = true,
	},
};

int st_lps22df_write_with_mask(struct st_lps22df_hw *hw, u8 addr, u8 mask,
			       u8 val)
{
	int err;
	u8 data;

	mutex_lock(&hw->lock);

	err = hw->tf->read(hw->dev, addr, sizeof(data), &data);
	if (err < 0)
		goto unlock;

	data = (data & ~mask) | ((val << __ffs(mask)) & mask);
	err = hw->tf->write(hw->dev, addr, sizeof(data), &data);
unlock:
	mutex_unlock(&hw->lock);

	return err;
}

static int st_lps22df_check_whoami(struct st_lps22df_hw *hw, int hw_id)
{
	int err, i, j;
	u8 data;

	for (i = 0; i < ARRAY_SIZE(st_lps22df_sensor_settings); i++) {
		for (j = 0; j < ST_LPS22DF_MAX_ID; j++) {
			if (st_lps22df_sensor_settings[i].id[j].name &&
			    st_lps22df_sensor_settings[i].id[j].hw_id == hw_id)
				break;
		}

		if (j < ST_LPS22DF_MAX_ID)
			break;
	}

	if (i == ARRAY_SIZE(st_lps22df_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", hw_id);

		return -ENODEV;
	}

	err = hw->tf->read(hw->dev, ST_LPS22DF_WHO_AM_I_ADDR,
			   sizeof(data), &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read Who-Am-I register\n");

		return err;
	}

	if (data != ST_LPS22DF_WHO_AM_I_VAL) {
		dev_err(hw->dev, "Who-Am-I value not valid (%x)\n",
			data);

		return -ENODEV;
	}

	hw->settings = &st_lps22df_sensor_settings[i];

	return 0;
}

static int st_lps22df_get_odr(struct st_lps22df_sensor *sensor, u8 odr)
{
	int i;

	for (i = 0; i < ST_LPS22DF_ODR_LIST_NUM; i++) {
		if (st_lps22df_odr_table.odr_avl[i] == odr)
			break;
	}

	return i == ST_LPS22DF_ODR_LIST_NUM ? -EINVAL : i;
}

int st_lps22df_set_enable(struct st_lps22df_sensor *sensor, bool enable)
{
	struct st_lps22df_hw *hw = sensor->hw;
	u32 max_odr = enable ? sensor->odr : 0;
	int i;

	for (i = 0; i < ST_LPS22DF_SENSORS_NUMB; i++) {
		if (sensor->type == i)
			continue;

		if (hw->enable_mask & BIT(i)) {
			struct st_lps22df_sensor *temp;

			temp = iio_priv(hw->iio_devs[i]);
			max_odr = max_t(u32, max_odr, temp->odr);
		}
	}

	if (max_odr != hw->odr) {
		int err, ret;

		ret = st_lps22df_get_odr(sensor, max_odr);
		if (ret < 0)
			return ret;

		err = st_lps22df_write_with_mask(hw, st_lps22df_odr_table.addr,
						 st_lps22df_odr_table.mask,
						 ret);
		if (err < 0)
			return err;

		hw->odr = max_odr;
	}

	if (enable)
		hw->enable_mask |= BIT(sensor->type);
	else
		hw->enable_mask &= ~BIT(sensor->type);

	return 0;
}

int st_lps22df_init_sensors(struct st_lps22df_hw *hw)
{
	int err;

	/* reboot memory content */
	err = st_lps22df_write_with_mask(hw, ST_LPS22DF_CTRL_REG2_ADDR,
					 ST_LPS22DF_BOOT_MASK, 1);
	if (err < 0)
		return err;

	usleep_range(8000, 10000);

	/* soft reset the device on power on */
	err = st_lps22df_write_with_mask(hw, ST_LPS22DF_CTRL_REG2_ADDR,
					 ST_LPS22DF_SWRESET_MASK, 1);
	if (err < 0)
		return err;

	usleep_range(100, 200);

	/* enable latched interrupt mode */
	err = st_lps22df_write_with_mask(hw, ST_LPS22DF_INTERRUPT_CFG_ADDR,
					 ST_LPS22DF_LIR_MASK, 1);
	if (err < 0)
		return err;

	/* enable BDU */
	return st_lps22df_write_with_mask(hw, ST_LPS22DF_CTRL_REG2_ADDR,
					  ST_LPS22DF_BDU_MASK, 1);
}

static ssize_t
st_lps22df_get_sampling_frequency_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int i, len = 0;

	for (i = 1; i < ST_LPS22DF_ODR_LIST_NUM; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_lps22df_odr_table.odr_avl[i]);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_lps22df_sysfs_get_hwfifo_watermark(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct st_lps22df_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->hw->watermark);
}

static ssize_t
st_lps22df_sysfs_get_hwfifo_watermark_max(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%d\n", ST_LPS22DF_MAX_FIFO_LENGTH);
}

static int st_lps22df_get_pressure_gain(struct st_lps22df_sensor *sensor,
					int val, int val2)
{
	struct st_lps22df_hw *hw = sensor->hw;
	int i;

	for (i = 0; i < hw->settings->fs_table.fs_len; i++) {
		if (val2 == hw->settings->fs_table.fs_avl[i].gain)
			return hw->settings->fs_table.fs_avl[i].val;
	}

	return -EINVAL;
}

static int st_lps22df_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_lps22df_sensor *sensor = iio_priv(indio_dev);
	struct st_lps22df_hw *hw = sensor->hw;
	int ret, delay;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		u8 data[4] = {};
		u8 len;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = st_lps22df_set_enable(sensor, true);
		if (ret < 0)
			goto unlock;

		/* wait at least 10% more than one odr */
		delay = 1100000 / sensor->odr;
		usleep_range(delay, 2 * delay);
		len = ch->scan_type.realbits >> 3;
		ret = hw->tf->read(hw->dev, ch->address, len, data);
		if (ret < 0)
			goto unlock;

		if (sensor->type == ST_LPS22DF_PRESS)
			*val = (s32)get_unaligned_le32(data);
		else if (sensor->type == ST_LPS22DF_TEMP)
			*val = (s16)get_unaligned_le16(data);

unlock:
		ret = st_lps22df_set_enable(sensor, false);
		iio_device_release_direct_mode(indio_dev);

		ret = ret < 0 ? ret : IIO_VAL_INT;
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 1000;
			*val2 = sensor->gain;
			ret = IIO_VAL_FRACTIONAL;
			break;
		case IIO_PRESSURE:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		default:
			ret = -ENODEV;
			break;
		}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = sensor->odr;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lps22df_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *ch,
				int val, int val2, long mask)
{
	struct st_lps22df_sensor *sensor = iio_priv(indio_dev);
	struct st_lps22df_hw *hw = sensor->hw;
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int index;

		index = st_lps22df_get_odr(sensor, val);
		if (index > 0) {
			sensor->odr = val;
			ret = 0;
		}
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_PRESSURE: {
			int fs_val;

			fs_val = st_lps22df_get_pressure_gain(sensor,
							      val,
							      val2);
			if (fs_val < 0)
				return fs_val;

			ret = st_lps22df_write_with_mask(hw,
					hw->settings->fs_table.addr,
					hw->settings->fs_table.mask,
					fs_val);
			if (ret < 0)
				return ret;

			sensor->gain = val2;
			break;
		}
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t st_lps22df_sysfs_scale_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct st_lps22df_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lps22df_hw *hw = sensor->hw;
	int i, len = 0;

	for (i = 0; i < hw->settings->fs_table.fs_len; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 hw->settings->fs_table.fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static int st_lps22df_write_raw_get_fmt(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PRESSURE:
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static IIO_DEVICE_ATTR(in_pressure_scale_available, 0444,
		       st_lps22df_sysfs_scale_avail, NULL, 0);
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lps22df_get_sampling_frequency_avail);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644,
		       st_lps22df_sysfs_get_hwfifo_watermark,
		       st_lps22df_sysfs_set_hwfifo_watermark, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lps22df_sysfs_get_hwfifo_watermark_max, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL,
		       st_lps22df_sysfs_flush_fifo, 0);

static struct attribute *st_lps22df_press_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_in_pressure_scale_available.dev_attr.attr,
	NULL,
};

static struct attribute *st_lps22df_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lps22df_press_attribute_group = {
	.attrs = st_lps22df_press_attributes,
};
static const struct attribute_group st_lps22df_temp_attribute_group = {
	.attrs = st_lps22df_temp_attributes,
};

static const struct iio_info st_lps22df_press_info = {
	.write_raw_get_fmt = st_lps22df_write_raw_get_fmt,
	.attrs = &st_lps22df_press_attribute_group,
	.read_raw = st_lps22df_read_raw,
	.write_raw = st_lps22df_write_raw,
};

static const struct iio_info st_lps22df_temp_info = {
	.attrs = &st_lps22df_temp_attribute_group,
	.read_raw = st_lps22df_read_raw,
	.write_raw = st_lps22df_write_raw,
};

int st_lps22df_common_probe(struct device *dev, int irq, int hw_id,
			    const struct st_lps22df_transfer_function *tf_ops)
{
	struct st_lps22df_sensor *sensor;
	struct st_lps22df_hw *hw;
	struct iio_dev *iio_dev;
	const char *name;
	int err, i;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);
	hw->dev = dev;
	hw->tf = tf_ops;
	hw->irq = irq;

	/* set initial watermark */
	hw->watermark = 1;

	mutex_init(&hw->lock);
	mutex_init(&hw->fifo_lock);

	err = st_lps22df_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	name = hw->settings->id->name;
	for (i = 0; i < ST_LPS22DF_SENSORS_NUMB; i++) {
		iio_dev = devm_iio_device_alloc(dev, sizeof(*sensor));
		if (!iio_dev)
			return -ENOMEM;

		hw->iio_devs[i] = iio_dev;
		sensor = iio_priv(iio_dev);
		sensor->hw = hw;
		sensor->type = i;
		sensor->odr = 1;

		switch (i) {
		case ST_LPS22DF_PRESS:
			sensor->gain = ST_LPS22DF_PRESS_1260_FS_AVL_GAIN;
			scnprintf(sensor->name, sizeof(sensor->name),
				  "%s_press", name);
			iio_dev->channels = st_lps22df_press_channels;
			iio_dev->num_channels =
				ARRAY_SIZE(st_lps22df_press_channels);
			iio_dev->info = &st_lps22df_press_info;
			break;
		case ST_LPS22DF_TEMP:
			sensor->gain = ST_LPS22DF_TEMP_FS_AVL_GAIN;
			scnprintf(sensor->name, sizeof(sensor->name),
				  "%s_temp", name);
			iio_dev->channels = st_lps22df_temp_channels;
			iio_dev->num_channels =
				ARRAY_SIZE(st_lps22df_temp_channels);
			iio_dev->info = &st_lps22df_temp_info;
			break;
		default:
			return -EINVAL;
		}

		iio_dev->name = sensor->name;
		iio_dev->modes = INDIO_DIRECT_MODE;
	}

	err = st_lps22df_init_sensors(hw);
	if (err < 0)
		return err;

	if (irq > 0) {
		err = st_lps22df_allocate_buffers(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_LPS22DF_SENSORS_NUMB; i++) {
		err = devm_iio_device_register(dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(st_lps22df_common_probe);

MODULE_DESCRIPTION("STMicroelectronics lps22df driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
