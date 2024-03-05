// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2hh12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <asm/unaligned.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/platform_data/stm/lis2hh12.h>

#include "st_lis2hh12.h"

#define ST_LIS2HH12_DEV_ATTR_SAMP_FREQ() \
		IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO, \
			lis2hh12_sysfs_get_sampling_frequency, \
			lis2hh12_sysfs_set_sampling_frequency)

#define ST_LIS2HH12_DEV_ATTR_SAMP_FREQ_AVAIL() \
		IIO_DEV_ATTR_SAMP_FREQ_AVAIL( \
			lis2hh12_sysfs_sampling_frequency_avail)

#define ST_LIS2HH12_DEV_ATTR_SCALE_AVAIL(name) \
		IIO_DEVICE_ATTR(name, S_IRUGO, \
			lis2hh12_sysfs_scale_avail, NULL , 0);

#define LIS2HH12_ADD_CHANNEL(device_type, modif, index, mod, endian, sbits,\
							rbits, addr, s) \
{ \
	.type = device_type, \
	.modified = modif, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = index, \
	.channel2 = mod, \
	.address = addr, \
	.scan_type = { \
		.sign = s, \
		.realbits = rbits, \
		.shift = sbits - rbits, \
		.storagebits = sbits, \
		.endianness = endian, \
	}, \
}

struct lis2hh12_odr_reg {
	u32 hz;
	u8 value;
};

static const struct lis2hh12_odr_table_t {
	u8 addr;
	u8 mask;
	struct lis2hh12_odr_reg odr_avl[LIS2HH12_ODR_LIST_NUM];
} lis2hh12_odr_table = {
	.addr = LIS2HH12_ODR_ADDR,
	.mask = LIS2HH12_ODR_MASK,

	.odr_avl[0] = {.hz = 0,.value = LIS2HH12_ODR_POWER_DOWN_VAL,},
	.odr_avl[1] = {.hz = 10,.value = LIS2HH12_ODR_10HZ_VAL,},
	.odr_avl[2] = {.hz = 50,.value = LIS2HH12_ODR_50HZ_VAL,},
	.odr_avl[3] = {.hz = 100,.value = LIS2HH12_ODR_100HZ_VAL,},
	.odr_avl[4] = {.hz = 200,.value = LIS2HH12_ODR_200HZ_VAL,},
	.odr_avl[5] = {.hz = 400,.value = LIS2HH12_ODR_400HZ_VAL,},
	.odr_avl[6] = {.hz = 800,.value = LIS2HH12_ODR_800HZ_VAL,},
};

struct lis2hh12_fs_reg {
	unsigned int gain;
	u8 value;
};

static struct lis2hh12_fs_table {
	u8 addr;
	u8 mask;
	struct lis2hh12_fs_reg fs_avl[LIS2HH12_FS_LIST_NUM];
} lis2hh12_fs_table = {
	.addr = LIS2HH12_FS_ADDR,
	.mask = LIS2HH12_FS_MASK,
	.fs_avl[0] = {
		.gain = LIS2HH12_FS_2G_GAIN,
		.value = LIS2HH12_FS_2G_VAL,
	},
	.fs_avl[1] = {
		.gain = LIS2HH12_FS_4G_GAIN,
		.value = LIS2HH12_FS_4G_VAL,
	},
	.fs_avl[2] = {
		.gain = LIS2HH12_FS_8G_GAIN,
		.value = LIS2HH12_FS_8G_VAL,
	},
};

const struct iio_event_spec lis2hh12_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct lis2hh12_sensors_table {
	const char *name;
	const char *description;
	const u32 min_odr_hz;
	const u8 iio_channel_size;
	const struct iio_chan_spec iio_channel[LIS2HH12_MAX_CHANNEL_SPEC];
} lis2hh12_sensors_table[LIS2HH12_SENSORS_NUMB] = {
	[LIS2HH12_ACCEL] = {
		.name = "accel",
		.description = "ST LIS2HH12 Accelerometer Sensor",
		.min_odr_hz = 10,
		.iio_channel = {
			LIS2HH12_ADD_CHANNEL(IIO_ACCEL, 1, 0, IIO_MOD_X, IIO_LE,
					16, 16, LIS2HH12_OUTX_L_ADDR, 's'),
			LIS2HH12_ADD_CHANNEL(IIO_ACCEL, 1, 1, IIO_MOD_Y, IIO_LE,
					16, 16, LIS2HH12_OUTY_L_ADDR, 's'),
			LIS2HH12_ADD_CHANNEL(IIO_ACCEL, 1, 2, IIO_MOD_Z, IIO_LE,
					16, 16, LIS2HH12_OUTZ_L_ADDR, 's'),
			ST_LIS2HH12_FLUSH_CHANNEL(IIO_ACCEL),
			IIO_CHAN_SOFT_TIMESTAMP(3)
		},
		.iio_channel_size = LIS2HH12_MAX_CHANNEL_SPEC,
	},
};

struct lis2hh12_selftest_req {
	char *mode;
	u8 val;
} lis2hh12_selftest_table[] = {
	{ "disabled", 0x0 },
	{ "positive-sign", 0x1 },
	{ "negative-sign", 0x2 },
};

inline int lis2hh12_read_register(struct lis2hh12_data *cdata, u8 reg_addr, int data_len,
							u8 *data)
{
	return cdata->tf->read(cdata, reg_addr, data_len, data);
}

static int lis2hh12_write_register(struct lis2hh12_data *cdata, u8 reg_addr,
							u8 mask, u8 data)
{
	int err;
	u8 new_data = 0x00, old_data = 0x00;

	err = lis2hh12_read_register(cdata, reg_addr, 1, &old_data);
	if (err < 0)
		return err;

	new_data = ((old_data & (~mask)) | ((data << __ffs(mask)) & mask));
	if (new_data == old_data)
		return 1;

	return cdata->tf->write(cdata, reg_addr, 1, &new_data);
}

int lis2hh12_set_fifo_mode(struct lis2hh12_data *cdata, enum fifo_mode fm)
{
	int err;
	u8 reg_value;
	u8 set_bit = LIS2HH12_DIS_BIT;

	switch (fm) {
	case BYPASS:
		reg_value = LIS2HH12_FIFO_MODE_BYPASS;
		set_bit = LIS2HH12_DIS_BIT;
		break;
	case STREAM:
		reg_value = LIS2HH12_FIFO_MODE_STREAM;
		set_bit = LIS2HH12_EN_BIT;
		break;
	default:
		return -EINVAL;
	}

	err = lis2hh12_write_register(cdata, LIS2HH12_FIFO_MODE_ADDR,
				LIS2HH12_FIFO_MODE_MASK, reg_value);

	if (err < 0)
		return err;

	cdata->sensor_timestamp =
				iio_get_time_ns(cdata->iio_sensors_dev[LIS2HH12_ACCEL]);

	err = lis2hh12_write_register(cdata, LIS2HH12_FIFO_EN_ADDR,
				LIS2HH12_FIFO_EN_MASK, set_bit);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(lis2hh12_set_fifo_mode);

static int lis2hh12_get_odr(u32 odr)
{
	int i;

	for (i = 0; i < LIS2HH12_ODR_LIST_NUM; i++) {
		if (lis2hh12_odr_table.odr_avl[i].hz >= odr)
			break;
	}

	if (i == LIS2HH12_ODR_LIST_NUM)
		return -EINVAL;

	return i;
}

static int lis2hh12_set_odr(struct lis2hh12_sensor_data *sdata, u32 odr)
{
	int ret;

	ret = lis2hh12_get_odr(odr);
	if (ret < 0)
		return ret;

	return lis2hh12_write_register(sdata->cdata,
				       lis2hh12_odr_table.addr,
				       lis2hh12_odr_table.mask,
				       lis2hh12_odr_table.odr_avl[ret].value);
}

int lis2hh12_write_max_odr(struct lis2hh12_sensor_data *sdata)
{
	struct lis2hh12_sensor_data *t_sdata;
	u32 max_odr = 0;
	int err, i;

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++)
		if (CHECK_BIT(sdata->cdata->enabled_sensor, i)) {
			t_sdata = iio_priv(sdata->cdata->iio_sensors_dev[i]);
			max_odr = max_t(u32, t_sdata->odr, max_odr);
		}

	if (max_odr != sdata->cdata->common_odr) {
		err = lis2hh12_set_odr(sdata, max_odr);
		if (err < 0)
			return err;

		sdata->cdata->common_odr = max_odr;
		sdata->cdata->sensor_deltatime = (max_odr) ? 1000000000L / max_odr : 0;
	}

	return 0;
}

int lis2hh12_set_fs(struct lis2hh12_sensor_data *sdata, unsigned int gain)
{
	int err, i;

	for (i = 0; i < LIS2HH12_FS_LIST_NUM; i++) {
		if (lis2hh12_fs_table.fs_avl[i].gain == gain)
			break;
	}

	if (i == LIS2HH12_FS_LIST_NUM)
		return -EINVAL;

	err = lis2hh12_write_register(sdata->cdata,
				lis2hh12_fs_table.addr, lis2hh12_fs_table.mask,
				lis2hh12_fs_table.fs_avl[i].value);
	if (err < 0)
		return err;

	sdata->gain = lis2hh12_fs_table.fs_avl[i].gain;

	return 0;
}

int lis2hh12_update_drdy_irq(struct lis2hh12_sensor_data *sdata, bool state)
{
	u8 reg_addr, reg_val, reg_mask;

	switch (sdata->sindex) {
	case LIS2HH12_ACCEL:
		reg_addr = LIS2HH12_INT_CFG_ADDR;
		if (sdata->cdata->hwfifo_enabled)
			reg_mask = (LIS2HH12_INT_FTH_MASK);
		else
			reg_mask = (LIS2HH12_INT_DRDY_MASK);

		if (state)
			reg_val = LIS2HH12_EN_BIT;
		else
			reg_val = LIS2HH12_DIS_BIT;

		break;

	default:
		return -EINVAL;
	}

	return lis2hh12_write_register(sdata->cdata, reg_addr, reg_mask, reg_val);
}
EXPORT_SYMBOL(lis2hh12_update_drdy_irq);

static int lis2hh12_alloc_fifo(struct lis2hh12_data *cdata)
{
	int fifo_size;

	fifo_size = LIS2HH12_MAX_FIFO_LENGHT * LIS2HH12_FIFO_BYTE_FOR_SAMPLE;

	cdata->fifo_data = kmalloc(fifo_size, GFP_KERNEL);
	if (!cdata->fifo_data)
		return -ENOMEM;

	cdata->fifo_size = fifo_size;

	return 0;
}

int lis2hh12_update_fifo_ths(struct lis2hh12_data *cdata, u8 fifo_len)
{
	int err;
	struct iio_dev *indio_dev;

	indio_dev = cdata->iio_sensors_dev[LIS2HH12_ACCEL];

	err = lis2hh12_write_register(cdata, LIS2HH12_FIFO_THS_ADDR,
				LIS2HH12_FIFO_THS_MASK,
				fifo_len);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(lis2hh12_update_fifo_ths);

int lis2hh12_set_enable(struct lis2hh12_sensor_data *sdata, bool state)
{
	int err = 0;
	u8 mode;

	if (sdata->enabled == state)
		return 0;

	/*
	 * Start assuming the sensor enabled if state == true.
	 * It will be restored if an error occur.
	 */
	if (state) {
		SET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
		mode = STREAM;
	} else {
		RESET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
		mode = BYPASS;
	}

	/* Program the device */
	err = lis2hh12_update_drdy_irq(sdata, state);
	if (err < 0)
		goto enable_sensor_error;

	err =  lis2hh12_set_fifo_mode(sdata->cdata, mode);
	if (err < 0)
		goto enable_sensor_error;

	err = lis2hh12_write_max_odr(sdata);
	if (err < 0)
		goto enable_sensor_error;

	sdata->enabled = state;

	return 0;

enable_sensor_error:
	if (state) {
		RESET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
	} else
		SET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);

	return err;
}
EXPORT_SYMBOL(lis2hh12_set_enable);

int lis2hh12_init_sensors(struct lis2hh12_data *cdata)
{
	int err;

	/*
	 * Soft reset the device on power on.
	 */
	err = lis2hh12_write_register(cdata, LIS2HH12_SOFT_RESET_ADDR,
				LIS2HH12_SOFT_RESET_MASK,
				LIS2HH12_EN_BIT);
	if (err < 0)
		return err;

	mdelay(40);

	/*
	 * Enable latched interrupt mode on INT1.
	 */
	err = lis2hh12_write_register(cdata, LIS2HH12_LIR_ADDR,
				LIS2HH12_LIR1_MASK,
				LIS2HH12_EN_BIT);
	if (err < 0)
		return err;

	/*
	 * Enable block data update feature.
	 */
	err = lis2hh12_write_register(cdata, LIS2HH12_BDU_ADDR,
				LIS2HH12_BDU_MASK,
				LIS2HH12_EN_BIT);
	if (err < 0)
		return err;

	return 0;
}

static ssize_t lis2hh12_sysfs_get_sampling_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct lis2hh12_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sdata->odr);
}

ssize_t lis2hh12_sysfs_set_sampling_frequency(struct device * dev,
		struct device_attribute * attr, const char *buf, size_t count)
{
	int err;
	u8 mode_count;
	unsigned int odr, i;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	if (sdata->odr == odr)
		return count;

	mode_count = LIS2HH12_ODR_LIST_NUM;

	for (i = 0; i < mode_count; i++) {
		if (lis2hh12_odr_table.odr_avl[i].hz >= odr)
			break;
	}
	if (i == LIS2HH12_ODR_LIST_NUM)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);
	sdata->odr = lis2hh12_odr_table.odr_avl[i].hz;
	mutex_unlock(&indio_dev->mlock);

	err = lis2hh12_write_max_odr(sdata);

	return (err < 0) ? err : count;
}

static ssize_t lis2hh12_sysfs_sampling_frequency_avail(struct device *dev,
						struct device_attribute
						*attr, char *buf)
{
	int i, len = 0;

	for (i = 1; i < LIS2HH12_ODR_LIST_NUM; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				lis2hh12_odr_table.odr_avl[i].hz);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t lis2hh12_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < LIS2HH12_FS_LIST_NUM; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
			lis2hh12_fs_table.fs_avl[i].gain);
	}
	buf[len - 1] = '\n';

	return len;
}

ssize_t lis2hh12_sysfs_flush_fifo(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u64 event_type;
	int64_t sensor_last_timestamp;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);

	if (lis2hh12_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
		disable_irq(sdata->cdata->irq);
	} else {
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	sensor_last_timestamp = sdata->cdata->sensor_timestamp;

	lis2hh12_read_fifo(sdata->cdata, true);

	if (sensor_last_timestamp == sdata->cdata->sensor_timestamp)
		event_type = STM_IIO_EV_DIR_FIFO_EMPTY;
	else
		event_type = STM_IIO_EV_DIR_FIFO_DATA;

	iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_ACCEL,
				-1, STM_IIO_EV_TYPE_FIFO_FLUSH, event_type),
				sdata->cdata->sensor_timestamp);

	enable_irq(sdata->cdata->irq);
	mutex_unlock(&indio_dev->mlock);

	return size;
}

static ssize_t lis2hh12_sysfs_get_hwfifo_enabled(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", sdata->cdata->hwfifo_enabled);
}

ssize_t lis2hh12_sysfs_set_hwfifo_enabled(struct device * dev,
		struct device_attribute * attr, const char *buf, size_t count)
{
	int err = 0, enable = 0;
	u8 mode = BYPASS;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &enable);
	if (err < 0)
			return err;

	if (enable != 0x0 && enable != 0x1)
		return -EINVAL;

	mode = (enable == 0x0) ? BYPASS : STREAM;

	err = lis2hh12_set_fifo_mode(sdata->cdata, mode);
	if (err < 0)
		return err;

	sdata->cdata->hwfifo_enabled = enable;

	return count;
}

static ssize_t lis2hh12_sysfs_get_hwfifo_watermark(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", sdata->cdata->hwfifo_watermark);
}

ssize_t lis2hh12_sysfs_set_hwfifo_watermark(struct device * dev,
		struct device_attribute * attr, const char *buf, size_t count)
{
	int err = 0, watermark = 0;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &watermark);
	if (err < 0)
			return err;

	if ((watermark < 1) || (watermark > LIS2HH12_MAX_FIFO_THS))
		return -EINVAL;

	err = lis2hh12_update_fifo_ths(sdata->cdata, watermark);
	if (err < 0)
		return err;

	sdata->cdata->hwfifo_watermark = watermark;

	return count;
}

static ssize_t lis2hh12_sysfs_get_hwfifo_watermark_min(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 1);
}

static ssize_t lis2hh12_sysfs_get_hwfifo_watermark_max(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", LIS2HH12_MAX_FIFO_THS);
}

static int lis2hh12_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	u8 outdata[2], nbytes;
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (lis2hh12_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		err = lis2hh12_set_enable(sdata, true);
		if (err < 0) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		msleep(40);

		nbytes = ch->scan_type.realbits / 8;

		err = lis2hh12_read_register(sdata->cdata, ch->address, nbytes, outdata);
		if (err < 0) {
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		*val = (s16)get_unaligned_le16(outdata);
		*val = *val >> ch->scan_type.shift;

		err = lis2hh12_set_enable(sdata, false);
		mutex_unlock(&indio_dev->mlock);

		if (err < 0)
			return err;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sdata->gain;

		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}

	return 0;
}

static int lis2hh12_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err, i;
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&indio_dev->mlock);

		if (lis2hh12_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		for (i = 0; i < LIS2HH12_FS_LIST_NUM; i++) {
			if (lis2hh12_fs_table.fs_avl[i].gain == val2)
				break;
		}

		err = lis2hh12_set_fs(sdata, lis2hh12_fs_table.fs_avl[i].gain);
		mutex_unlock(&indio_dev->mlock);

		break;

	default:
		return -EINVAL;
	}

	return err;
}

static ssize_t lis2hh12_get_selftest_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sprintf(buf, "%s, %s\n", lis2hh12_selftest_table[1].mode,
		       lis2hh12_selftest_table[2].mode);
}

static ssize_t lis2hh12_get_selftest_status(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(iio_dev);
	struct lis2hh12_data *cdata = sdata->cdata;
	char *ret;

	switch (cdata->st_status) {
	case LIS2HH12_ST_PASS:
		ret = "pass";
		break;
	case LIS2HH12_ST_FAIL:
		ret = "fail";
		break;
	default:
		ret = "na";
		break;
	}

	return sprintf(buf, "%s\n", ret);
}

static ssize_t lis2hh12_enable_selftest(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct lis2hh12_sensor_data *sdata = iio_priv(iio_dev);
	struct lis2hh12_data *cdata = sdata->cdata;
	s16 acc_st_x = 0, acc_st_y = 0, acc_st_z = 0;
	s16 acc_x = 0, acc_y = 0, acc_z = 0;
	u8 data[LIS2HH12_DATA_SIZE], stval, status;
	int i, err, gain, odr, trycount;

	mutex_lock(&iio_dev->mlock);

	/* self test procedure run only when accel sensor is disabled */
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;

		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(lis2hh12_selftest_table); i++)
		if (!strncmp(buf, lis2hh12_selftest_table[i].mode,
			     size - 2))
			break;

	if (i == ARRAY_SIZE(lis2hh12_selftest_table)) {
		err = -EINVAL;

		goto unlock;
	}

	cdata->st_status = LIS2HH12_ST_RESET;
	stval = lis2hh12_selftest_table[i].val;

	/* save odr and gain before change it */
	gain = sdata->gain;
	odr = sdata->odr;

	/* fs = 2g, odr = 50Hz */
	err = lis2hh12_set_fs(sdata, LIS2HH12_FS_2G_GAIN);
	if (err < 0)
		goto unlock;

	err = lis2hh12_set_odr(sdata, 100);
	if (err < 0)
		goto unlock;

	/*
	 * read OUTX/OUTY/OUTZ to clear ZYXDA bit in register STATUS
	 * and discard output data
	 */
	for (trycount = 0; trycount < 3; trycount++) {
		/* avoid polling before one odr */
		usleep_range(10000, 11000);
		err = lis2hh12_read_register(cdata, LIS2HH12_STATUS_ADDR,
					     sizeof(status), &status);
		if (err < 0)
			goto unlock;

		if (status & LIS2HH12_DATA_XYZ_RDY) {
			err = lis2hh12_read_register(cdata,
						     LIS2HH12_OUTX_L_ADDR,
						     sizeof(data), data);
			if (err < 0)
				goto unlock;

			break;
		}
	}

	if (trycount == 3) {
		dev_err(cdata->dev,
			"self-test: Unable to collect sensor data.\n");

		goto unlock;
	}

	/* read the output registers after checking ZYXDA bit 5 times */
	trycount = 0;
	i = 0;
	do {
		err = lis2hh12_read_register(cdata, LIS2HH12_STATUS_ADDR,
					     sizeof(status), &status);
		if (err < 0)
			goto unlock;

		if (status & LIS2HH12_DATA_XYZ_RDY) {
			err = lis2hh12_read_register(cdata,
						     LIS2HH12_OUTX_L_ADDR,
						     sizeof(data), data);
			if (err < 0)
				goto unlock;

			acc_x += ((s16)get_unaligned_le16(&data[0])) / 5;
			acc_y += ((s16)get_unaligned_le16(&data[2])) / 5;
			acc_z += ((s16)get_unaligned_le16(&data[4])) / 5;
			i++;
		} else {
			trycount++;
			if (trycount == 3) {
				dev_err(cdata->dev,
					"self-test: Unable to collect sensor data.\n");

				goto unlock;
			}
		}

		usleep_range(10000, 11000);
	} while (i < 5);

	/* enable self test */
	err = lis2hh12_write_register(cdata, LIS2HH12_SELF_TEST_ADDR,
				      LIS2HH12_ST_MASK, stval);
	if (err < 0)
		goto unlock;

	usleep_range(80000, 90000);

	/*
	 * read OUTX/OUTY/OUTZ to clear ZYXDA bit in register STATUS
	 * and discard output data
	 */
	for (trycount = 0; trycount < 3; trycount++) {
		/* avoid polling before one odr */
		usleep_range(10000, 11000);

		err = lis2hh12_read_register(cdata, LIS2HH12_STATUS_ADDR,
					     sizeof(status), &status);
		if (err < 0)
			goto unlock;

		if (status & LIS2HH12_DATA_XYZ_RDY) {
			err = lis2hh12_read_register(cdata,
						     LIS2HH12_OUTX_L_ADDR,
						     sizeof(data), data);
			if (err < 0)
				goto unlock;

			break;
		}
	}

	if (trycount == 3) {
		dev_err(cdata->dev,
			"self-test: Unable to collect sensor data.\n");

		goto unlock;
	}

	/* read the output registers after checking ZYXDA bit 5 times */
	trycount = 0;
	i = 0;
	do {
		err = lis2hh12_read_register(cdata, LIS2HH12_STATUS_ADDR,
					     sizeof(status), &status);
		if (err < 0)
			goto unlock;

		if (status & LIS2HH12_DATA_XYZ_RDY) {
			err = lis2hh12_read_register(cdata,
						     LIS2HH12_OUTX_L_ADDR,
						     sizeof(data), data);
			if (err < 0)
				goto unlock;

			acc_st_x += ((s16)get_unaligned_le16(&data[0])) / 5;
			acc_st_y += ((s16)get_unaligned_le16(&data[2])) / 5;
			acc_st_z += ((s16)get_unaligned_le16(&data[4])) / 5;
			i++;
		} else {
			trycount++;
			if (trycount == 3) {
				dev_err(cdata->dev,
					"self-test: Unable to collect sensor data.\n");

				goto unlock;
			}
		}

		usleep_range(10000, 11000);
	} while (i < 5);

	if ((abs(acc_st_x - acc_x) < LIS2HH12_SELFTEST_MIN) ||
	    (abs(acc_st_x - acc_x) > LIS2HH12_SELFTEST_MAX)) {
		dev_warn(cdata->dev,
			 "self-test: failed x-axis test (delta %d)\n",
			 abs(acc_st_x - acc_x));
		cdata->st_status = LIS2HH12_ST_FAIL;
	} else if ((abs(acc_st_y - acc_y) < LIS2HH12_SELFTEST_MIN) ||
		   (abs(acc_st_y - acc_y) > LIS2HH12_SELFTEST_MAX)) {
		dev_warn(cdata->dev,
			 "self-test: failed y-axis test (delta %d)\n",
			 abs(acc_st_y - acc_y));
		cdata->st_status = LIS2HH12_ST_FAIL;
	} else if (abs(acc_st_z - acc_z) < LIS2HH12_SELFTEST_MIN ||
		  (abs(acc_st_z - acc_z) > LIS2HH12_SELFTEST_MAX)) {
		dev_warn(cdata->dev,
			 "self-test: failed z-axis test (delta %d)\n",
			 abs(acc_st_z - acc_z));
		cdata->st_status = LIS2HH12_ST_FAIL;
	} else {
		cdata->st_status = LIS2HH12_ST_PASS;
	}

	/* disable self test */
	err = lis2hh12_write_register(cdata, LIS2HH12_SELF_TEST_ADDR,
				      LIS2HH12_ST_MASK, 0);
	if (err < 0)
		goto unlock;

	err = lis2hh12_set_fs(sdata, gain);
	if (err < 0)
		goto unlock;

	err = lis2hh12_set_odr(sdata, odr);
	if (err < 0)
		goto unlock;

	err = lis2hh12_set_enable(sdata, false);

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

static ST_LIS2HH12_DEV_ATTR_SAMP_FREQ();
static ST_LIS2HH12_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_LIS2HH12_DEV_ATTR_SCALE_AVAIL(in_accel_scale_available);

static ST_LIS2HH12_HWFIFO_ENABLED();
static ST_LIS2HH12_HWFIFO_WATERMARK();
static ST_LIS2HH12_HWFIFO_WATERMARK_MIN();
static ST_LIS2HH12_HWFIFO_WATERMARK_MAX();
static ST_LIS2HH12_HWFIFO_FLUSH();

static IIO_DEVICE_ATTR(selftest_available, 0444,
		       lis2hh12_get_selftest_avail, NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644, lis2hh12_get_selftest_status,
		       lis2hh12_enable_selftest, 0);


static struct attribute *lis2hh12_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	NULL,
};

static const struct attribute_group lis2hh12_accel_attribute_group = {
	.attrs = lis2hh12_accel_attributes,
};

static const struct iio_info lis2hh12_info[LIS2HH12_SENSORS_NUMB] = {
	[LIS2HH12_ACCEL] = {
		.attrs = &lis2hh12_accel_attribute_group,
		.read_raw = &lis2hh12_read_raw,
		.write_raw = &lis2hh12_write_raw,
	},
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops lis2hh12_trigger_ops = {
	.set_trigger_state = (&lis2hh12_trig_set_state),
};
#define LIS2HH12_TRIGGER_OPS (&lis2hh12_trigger_ops)
#else /*CONFIG_IIO_TRIGGER */
#define LIS2HH12_TRIGGER_OPS NULL
#endif /*CONFIG_IIO_TRIGGER */

#ifdef CONFIG_OF
static u32 lis2hh12_parse_dt(struct lis2hh12_data *cdata)
{
	u32 val;
	struct device_node *np;

	np = cdata->dev->of_node;
	if (!np)
		return -EINVAL;
	/*TODO for this device interrupt pin is only one!!*/
	if (!of_property_read_u32(np, "st,drdy-int-pin", &val) &&
							(val <= 1) && (val > 0))
		cdata->drdy_int_pin = (u8) val;
	else
		cdata->drdy_int_pin = 1;

	return 0;
}
#endif /*CONFIG_OF */

int lis2hh12_common_probe(struct lis2hh12_data *cdata, int irq)
{
	u8 wai = 0;
	int32_t err, i, n;
	struct iio_dev *piio_dev;
	struct lis2hh12_sensor_data *sdata;

	mutex_init(&cdata->tb.buf_lock);

	cdata->fifo_data = 0;
	cdata->hwfifo_enabled = 0;
	cdata->hwfifo_watermark = 0;

	err = lis2hh12_read_register(cdata, LIS2HH12_WHO_AM_I_ADDR, 1, &wai);
	if (err < 0) {
		dev_err(cdata->dev, "failed to read Who-Am-I register.\n");

		return err;
	}
	if (wai != LIS2HH12_WHO_AM_I_DEF) {
		dev_err(cdata->dev, "Who-Am-I value not valid.\n");

		return -ENODEV;
	}

	if (irq > 0) {
		cdata->irq = irq;
#ifdef CONFIG_OF
		err = lis2hh12_parse_dt(cdata);
		if (err < 0)
			return err;
#else /* CONFIG_OF */
		if (cdata->dev->platform_data) {
			cdata->drdy_int_pin = ((struct lis2hh12_platform_data *)
					cdata->dev->platform_data)->drdy_int_pin;

			if ((cdata->drdy_int_pin > 1) || (cdata->drdy_int_pin < 1))
				cdata->drdy_int_pin = 1;
		} else
			cdata->drdy_int_pin = 1;
#endif /* CONFIG_OF */

		dev_info(cdata->dev, "driver use DRDY int pin %d\n",
						cdata->drdy_int_pin);
	}

	cdata->common_odr = 0;
	cdata->enabled_sensor = 0;

	err = lis2hh12_alloc_fifo(cdata);
	if (err)
		return err;

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++) {
		piio_dev = devm_iio_device_alloc(cdata->dev,
					sizeof(struct lis2hh12_sensor_data *));
		if (piio_dev == NULL) {
			err = -ENOMEM;

			goto iio_device_free;
		}

		cdata->iio_sensors_dev[i] = piio_dev;
		sdata = iio_priv(piio_dev);
		sdata->enabled = false;
		sdata->cdata = cdata;
		sdata->sindex = i;
		sdata->name = lis2hh12_sensors_table[i].name;
		sdata->odr = lis2hh12_sensors_table[i].min_odr_hz;
		sdata->gain = lis2hh12_fs_table.fs_avl[0].gain;

		piio_dev->channels = lis2hh12_sensors_table[i].iio_channel;
		piio_dev->num_channels = lis2hh12_sensors_table[i].iio_channel_size;
		piio_dev->info = &lis2hh12_info[i];
		piio_dev->modes = INDIO_DIRECT_MODE;
		piio_dev->name = kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
								sdata->name);
	}

	err =  lis2hh12_set_fifo_mode(sdata->cdata, BYPASS);
	if (err < 0)
		goto iio_device_free;

	err = lis2hh12_init_sensors(cdata);
	if (err < 0)
		goto iio_device_free;

	err = lis2hh12_allocate_rings(cdata);
	if (err < 0)
		goto iio_device_free;

	if (irq > 0) {
		err = lis2hh12_allocate_triggers(cdata, LIS2HH12_TRIGGER_OPS);
		if (err < 0)
			goto deallocate_ring;
	}

	for (n = 0; n < LIS2HH12_SENSORS_NUMB; n++) {
		err = iio_device_register(cdata->iio_sensors_dev[n]);
		if (err)
			goto iio_device_unregister_and_trigger_deallocate;
	}

	dev_info(cdata->dev, "%s: probed\n", LIS2HH12_DEV_NAME);
	return 0;

iio_device_unregister_and_trigger_deallocate:
	for (n--; n >= 0; n--)
		iio_device_unregister(cdata->iio_sensors_dev[n]);

deallocate_ring:
	lis2hh12_deallocate_rings(cdata);

iio_device_free:
	for (i--; i >= 0; i--)
		iio_device_free(cdata->iio_sensors_dev[i]);

	return err;
}
EXPORT_SYMBOL(lis2hh12_common_probe);

void lis2hh12_common_remove(struct lis2hh12_data *cdata, int irq)
{
	int i;

	if (cdata->fifo_data) {
		kfree(cdata->fifo_data);
		cdata->fifo_size = 0;
	}

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++)
		iio_device_unregister(cdata->iio_sensors_dev[i]);

	if (irq > 0)
		lis2hh12_deallocate_triggers(cdata);

	lis2hh12_deallocate_rings(cdata);

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++)
		iio_device_free(cdata->iio_sensors_dev[i]);
}

EXPORT_SYMBOL(lis2hh12_common_remove);

#ifdef CONFIG_PM
int __maybe_unused lis2hh12_common_suspend(struct lis2hh12_data *cdata)
{
	return 0;
}

EXPORT_SYMBOL(lis2hh12_common_suspend);

int __maybe_unused lis2hh12_common_resume(struct lis2hh12_data *cdata)
{
	return 0;
}

EXPORT_SYMBOL(lis2hh12_common_resume);
#endif /* CONFIG_PM */

MODULE_DESCRIPTION("STMicroelectronics lis2hh12 driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
