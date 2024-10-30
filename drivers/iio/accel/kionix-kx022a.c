// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX accelerometer driver
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "kionix-kx022a.h"

/*
 * The KX022A has FIFO which can store 43 samples of HiRes data from 2
 * channels. This equals to 43 (samples) * 3 (channels) * 2 (bytes/sample) to
 * 258 bytes of sample data. The quirk to know is that the amount of bytes in
 * the FIFO is advertised via 8 bit register (max value 255). The thing to note
 * is that full 258 bytes of data is indicated using the max value 255.
 */
#define KX022A_FIFO_LENGTH			43
#define KX022A_FIFO_FULL_VALUE			255
#define KX022A_SOFT_RESET_WAIT_TIME_US		(5 * USEC_PER_MSEC)
#define KX022A_SOFT_RESET_TOTAL_WAIT_TIME_US	(500 * USEC_PER_MSEC)

/* 3 axis, 2 bytes of data for each of the axis */
#define KX022A_FIFO_SAMPLES_SIZE_BYTES		6
#define KX022A_FIFO_MAX_BYTES					\
	(KX022A_FIFO_LENGTH * KX022A_FIFO_SAMPLES_SIZE_BYTES)

enum {
	KX022A_STATE_SAMPLE,
	KX022A_STATE_FIFO,
};

/* kx022a Regmap configs */
static const struct regmap_range kx022a_volatile_ranges[] = {
	{
		.range_min = KX022A_REG_XHP_L,
		.range_max = KX022A_REG_COTR,
	}, {
		.range_min = KX022A_REG_TSCP,
		.range_max = KX022A_REG_INT_REL,
	}, {
		/* The reset bit will be cleared by sensor */
		.range_min = KX022A_REG_CNTL2,
		.range_max = KX022A_REG_CNTL2,
	}, {
		.range_min = KX022A_REG_BUF_STATUS_1,
		.range_max = KX022A_REG_BUF_READ,
	},
};

static const struct regmap_access_table kx022a_volatile_regs = {
	.yes_ranges = &kx022a_volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx022a_volatile_ranges),
};

static const struct regmap_range kx022a_precious_ranges[] = {
	{
		.range_min = KX022A_REG_INT_REL,
		.range_max = KX022A_REG_INT_REL,
	},
};

static const struct regmap_access_table kx022a_precious_regs = {
	.yes_ranges = &kx022a_precious_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx022a_precious_ranges),
};

/*
 * The HW does not set WHO_AM_I reg as read-only but we don't want to write it
 * so we still include it in the read-only ranges.
 */
static const struct regmap_range kx022a_read_only_ranges[] = {
	{
		.range_min = KX022A_REG_XHP_L,
		.range_max = KX022A_REG_INT_REL,
	}, {
		.range_min = KX022A_REG_BUF_STATUS_1,
		.range_max = KX022A_REG_BUF_STATUS_2,
	}, {
		.range_min = KX022A_REG_BUF_READ,
		.range_max = KX022A_REG_BUF_READ,
	},
};

static const struct regmap_access_table kx022a_ro_regs = {
	.no_ranges = &kx022a_read_only_ranges[0],
	.n_no_ranges = ARRAY_SIZE(kx022a_read_only_ranges),
};

static const struct regmap_range kx022a_write_only_ranges[] = {
	{
		.range_min = KX022A_REG_BTS_WUF_TH,
		.range_max = KX022A_REG_BTS_WUF_TH,
	}, {
		.range_min = KX022A_REG_MAN_WAKE,
		.range_max = KX022A_REG_MAN_WAKE,
	}, {
		.range_min = KX022A_REG_SELF_TEST,
		.range_max = KX022A_REG_SELF_TEST,
	}, {
		.range_min = KX022A_REG_BUF_CLEAR,
		.range_max = KX022A_REG_BUF_CLEAR,
	},
};

static const struct regmap_access_table kx022a_wo_regs = {
	.no_ranges = &kx022a_write_only_ranges[0],
	.n_no_ranges = ARRAY_SIZE(kx022a_write_only_ranges),
};

static const struct regmap_range kx022a_noinc_read_ranges[] = {
	{
		.range_min = KX022A_REG_BUF_READ,
		.range_max = KX022A_REG_BUF_READ,
	},
};

static const struct regmap_access_table kx022a_nir_regs = {
	.yes_ranges = &kx022a_noinc_read_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx022a_noinc_read_ranges),
};

static const struct regmap_config kx022a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &kx022a_volatile_regs,
	.rd_table = &kx022a_wo_regs,
	.wr_table = &kx022a_ro_regs,
	.rd_noinc_table = &kx022a_nir_regs,
	.precious_table = &kx022a_precious_regs,
	.max_register = KX022A_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

/* Regmap configs kx132 */
static const struct regmap_range kx132_volatile_ranges[] = {
	{
		.range_min = KX132_REG_XADP_L,
		.range_max = KX132_REG_COTR,
	}, {
		.range_min = KX132_REG_TSCP,
		.range_max = KX132_REG_INT_REL,
	}, {
		/* The reset bit will be cleared by sensor */
		.range_min = KX132_REG_CNTL2,
		.range_max = KX132_REG_CNTL2,
	}, {
		.range_min = KX132_REG_CNTL5,
		.range_max = KX132_REG_CNTL5,
	}, {
		.range_min = KX132_REG_BUF_STATUS_1,
		.range_max = KX132_REG_BUF_READ,
	},
};

static const struct regmap_access_table kx132_volatile_regs = {
	.yes_ranges = &kx132_volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx132_volatile_ranges),
};

static const struct regmap_range kx132_precious_ranges[] = {
	{
		.range_min = KX132_REG_INT_REL,
		.range_max = KX132_REG_INT_REL,
	},
};

static const struct regmap_access_table kx132_precious_regs = {
	.yes_ranges = &kx132_precious_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx132_precious_ranges),
};

static const struct regmap_range kx132_read_only_ranges[] = {
	{
		.range_min = KX132_REG_XADP_L,
		.range_max = KX132_REG_INT_REL,
	}, {
		.range_min = KX132_REG_BUF_STATUS_1,
		.range_max = KX132_REG_BUF_STATUS_2,
	}, {
		.range_min = KX132_REG_BUF_READ,
		.range_max = KX132_REG_BUF_READ,
	}, {
		/* Kionix reserved registers: should not be written */
		.range_min = 0x28,
		.range_max = 0x28,
	}, {
		.range_min = 0x35,
		.range_max = 0x36,
	}, {
		.range_min = 0x3c,
		.range_max = 0x48,
	}, {
		.range_min = 0x4e,
		.range_max = 0x5c,
	}, {
		.range_min = 0x77,
		.range_max = 0x7f,
	},
};

static const struct regmap_access_table kx132_ro_regs = {
	.no_ranges = &kx132_read_only_ranges[0],
	.n_no_ranges = ARRAY_SIZE(kx132_read_only_ranges),
};

static const struct regmap_range kx132_write_only_ranges[] = {
	{
		.range_min = KX132_REG_SELF_TEST,
		.range_max = KX132_REG_SELF_TEST,
	}, {
		.range_min = KX132_REG_BUF_CLEAR,
		.range_max = KX132_REG_BUF_CLEAR,
	},
};

static const struct regmap_access_table kx132_wo_regs = {
	.no_ranges = &kx132_write_only_ranges[0],
	.n_no_ranges = ARRAY_SIZE(kx132_write_only_ranges),
};

static const struct regmap_range kx132_noinc_read_ranges[] = {
	{
		.range_min = KX132_REG_BUF_READ,
		.range_max = KX132_REG_BUF_READ,
	},
};

static const struct regmap_access_table kx132_nir_regs = {
	.yes_ranges = &kx132_noinc_read_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(kx132_noinc_read_ranges),
};

static const struct regmap_config kx132_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &kx132_volatile_regs,
	.rd_table = &kx132_wo_regs,
	.wr_table = &kx132_ro_regs,
	.rd_noinc_table = &kx132_nir_regs,
	.precious_table = &kx132_precious_regs,
	.max_register = KX132_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

struct kx022a_data {
	struct regmap *regmap;
	const struct kx022a_chip_info *chip_info;
	struct iio_trigger *trig;
	struct device *dev;
	struct iio_mount_matrix orientation;
	int64_t timestamp, old_timestamp;

	int irq;
	int inc_reg;
	int ien_reg;

	unsigned int state;
	unsigned int odr_ns;

	bool trigger_enabled;
	/*
	 * Prevent toggling the sensor stby/active state (PC1 bit) in the
	 * middle of a configuration, or when the fifo is enabled. Also,
	 * protect the data stored/retrieved from this structure from
	 * concurrent accesses.
	 */
	struct mutex mutex;
	u8 watermark;

	__le16 *fifo_buffer;

	/* 3 x 16bit accel data + timestamp */
	__le16 buffer[8] __aligned(IIO_DMA_MINALIGN);
	struct {
		__le16 channels[3];
		s64 ts __aligned(8);
	} scan;
};

static const struct iio_mount_matrix *
kx022a_get_mount_matrix(const struct iio_dev *idev,
			const struct iio_chan_spec *chan)
{
	struct kx022a_data *data = iio_priv(idev);

	return &data->orientation;
}

enum {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	AXIS_MAX
};

static const unsigned long kx022a_scan_masks[] = {
	BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z), 0
};

static const struct iio_chan_spec_ext_info kx022a_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, kx022a_get_mount_matrix),
	{ }
};

#define KX022A_ACCEL_CHAN(axis, reg, index)			\
{								\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_type_available =			\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.ext_info = kx022a_ext_info,				\
	.address = reg,						\
	.scan_index = index,					\
	.scan_type = {                                          \
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_LE,				\
	},							\
}

static const struct iio_chan_spec kx022a_channels[] = {
	KX022A_ACCEL_CHAN(X, KX022A_REG_XOUT_L, 0),
	KX022A_ACCEL_CHAN(Y, KX022A_REG_YOUT_L, 1),
	KX022A_ACCEL_CHAN(Z, KX022A_REG_ZOUT_L, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec kx132_channels[] = {
	KX022A_ACCEL_CHAN(X, KX132_REG_XOUT_L, 0),
	KX022A_ACCEL_CHAN(Y, KX132_REG_YOUT_L, 1),
	KX022A_ACCEL_CHAN(Z, KX132_REG_ZOUT_L, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

/*
 * The sensor HW can support ODR up to 1600 Hz, which is beyond what most of the
 * Linux CPUs can handle without dropping samples. Also, the low power mode is
 * not available for higher sample rates. Thus, the driver only supports 200 Hz
 * and slower ODRs. The slowest is 0.78 Hz.
 */
static const int kx022a_accel_samp_freq_table[][2] = {
	{ 0, 780000 },
	{ 1, 563000 },
	{ 3, 125000 },
	{ 6, 250000 },
	{ 12, 500000 },
	{ 25, 0 },
	{ 50, 0 },
	{ 100, 0 },
	{ 200, 0 },
};

static const unsigned int kx022a_odrs[] = {
	1282051282,
	639795266,
	320 * MEGA,
	160 * MEGA,
	80 * MEGA,
	40 * MEGA,
	20 * MEGA,
	10 * MEGA,
	5 * MEGA,
};

/*
 * range is typically +-2G/4G/8G/16G, distributed over the amount of bits.
 * The scale table can be calculated using
 *	(range / 2^bits) * g = (range / 2^bits) * 9.80665 m/s^2
 *	=> KX022A uses 16 bit (HiRes mode - assume the low 8 bits are zeroed
 *	in low-power mode(?) )
 *	=> +/-2G  => 4 / 2^16 * 9,80665
 *	=> +/-2G  - 0.000598550415
 *	   +/-4G  - 0.00119710083
 *	   +/-8G  - 0.00239420166
 *	   +/-16G - 0.00478840332
 */
static const int kx022a_scale_table[][2] = {
	{ 0, 598550 },
	{ 0, 1197101 },
	{ 0, 2394202 },
	{ 0, 4788403 },
};

static int kx022a_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)kx022a_accel_samp_freq_table;
		*length = ARRAY_SIZE(kx022a_accel_samp_freq_table) *
			  ARRAY_SIZE(kx022a_accel_samp_freq_table[0]);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*vals = (const int *)kx022a_scale_table;
		*length = ARRAY_SIZE(kx022a_scale_table) *
			  ARRAY_SIZE(kx022a_scale_table[0]);
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

#define KX022A_DEFAULT_PERIOD_NS (20 * NSEC_PER_MSEC)

static void kx022a_reg2freq(unsigned int val,  int *val1, int *val2)
{
	*val1 = kx022a_accel_samp_freq_table[val & KX022A_MASK_ODR][0];
	*val2 = kx022a_accel_samp_freq_table[val & KX022A_MASK_ODR][1];
}

static void kx022a_reg2scale(unsigned int val, unsigned int *val1,
			     unsigned int *val2)
{
	val &= KX022A_MASK_GSEL;
	val >>= KX022A_GSEL_SHIFT;

	*val1 = kx022a_scale_table[val][0];
	*val2 = kx022a_scale_table[val][1];
}

static int kx022a_turn_on_off_unlocked(struct kx022a_data *data, bool on)
{
	int ret;

	if (on)
		ret = regmap_set_bits(data->regmap, data->chip_info->cntl,
				      KX022A_MASK_PC1);
	else
		ret = regmap_clear_bits(data->regmap, data->chip_info->cntl,
					KX022A_MASK_PC1);
	if (ret)
		dev_err(data->dev, "Turn %s fail %d\n", str_on_off(on), ret);

	return ret;
}

static int kx022a_turn_off_lock(struct kx022a_data *data)
{
	int ret;

	mutex_lock(&data->mutex);
	ret = kx022a_turn_on_off_unlocked(data, false);
	if (ret)
		mutex_unlock(&data->mutex);

	return ret;
}

static int kx022a_turn_on_unlock(struct kx022a_data *data)
{
	int ret;

	ret = kx022a_turn_on_off_unlocked(data, true);
	mutex_unlock(&data->mutex);

	return ret;
}

static int kx022a_write_raw_get_fmt(struct iio_dev *idev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int kx022a_write_raw(struct iio_dev *idev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct kx022a_data *data = iio_priv(idev);
	int ret, n;

	/*
	 * We should not allow changing scale or frequency when FIFO is running
	 * as it will mess the timestamp/scale for samples existing in the
	 * buffer. If this turns out to be an issue we can later change logic
	 * to internally flush the fifo before reconfiguring so the samples in
	 * fifo keep matching the freq/scale settings. (Such setup could cause
	 * issues if users trust the watermark to be reached within known
	 * time-limit).
	 */
	ret = iio_device_claim_direct_mode(idev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		n = ARRAY_SIZE(kx022a_accel_samp_freq_table);

		while (n--)
			if (val == kx022a_accel_samp_freq_table[n][0] &&
			    val2 == kx022a_accel_samp_freq_table[n][1])
				break;
		if (n < 0) {
			ret = -EINVAL;
			goto unlock_out;
		}
		ret = kx022a_turn_off_lock(data);
		if (ret)
			break;

		ret = regmap_update_bits(data->regmap,
					 data->chip_info->odcntl,
					 KX022A_MASK_ODR, n);
		data->odr_ns = kx022a_odrs[n];
		kx022a_turn_on_unlock(data);
		break;
	case IIO_CHAN_INFO_SCALE:
		n = ARRAY_SIZE(kx022a_scale_table);

		while (n-- > 0)
			if (val == kx022a_scale_table[n][0] &&
			    val2 == kx022a_scale_table[n][1])
				break;
		if (n < 0) {
			ret = -EINVAL;
			goto unlock_out;
		}

		ret = kx022a_turn_off_lock(data);
		if (ret)
			break;

		ret = regmap_update_bits(data->regmap, data->chip_info->cntl,
					 KX022A_MASK_GSEL,
					 n << KX022A_GSEL_SHIFT);
		kx022a_turn_on_unlock(data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

unlock_out:
	iio_device_release_direct_mode(idev);

	return ret;
}

static int kx022a_fifo_set_wmi(struct kx022a_data *data)
{
	u8 threshold;

	threshold = data->watermark;

	return regmap_update_bits(data->regmap, data->chip_info->buf_cntl1,
				  KX022A_MASK_WM_TH, threshold);
}

static int kx022a_get_axis(struct kx022a_data *data,
			   struct iio_chan_spec const *chan,
			   int *val)
{
	int ret;

	ret = regmap_bulk_read(data->regmap, chan->address, &data->buffer[0],
			       sizeof(__le16));
	if (ret)
		return ret;

	*val = (s16)le16_to_cpu(data->buffer[0]);

	return IIO_VAL_INT;
}

static int kx022a_read_raw(struct iio_dev *idev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct kx022a_data *data = iio_priv(idev);
	unsigned int regval;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(idev);
		if (ret)
			return ret;

		mutex_lock(&data->mutex);
		ret = kx022a_get_axis(data, chan, val);
		mutex_unlock(&data->mutex);

		iio_device_release_direct_mode(idev);

		return ret;

	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(data->regmap, data->chip_info->odcntl, &regval);
		if (ret)
			return ret;

		if ((regval & KX022A_MASK_ODR) >
		    ARRAY_SIZE(kx022a_accel_samp_freq_table)) {
			dev_err(data->dev, "Invalid ODR\n");
			return -EINVAL;
		}

		kx022a_reg2freq(regval, val, val2);

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(data->regmap, data->chip_info->cntl, &regval);
		if (ret < 0)
			return ret;

		kx022a_reg2scale(regval, val, val2);

		return IIO_VAL_INT_PLUS_NANO;
	}

	return -EINVAL;
};

static int kx022a_set_watermark(struct iio_dev *idev, unsigned int val)
{
	struct kx022a_data *data = iio_priv(idev);

	val = min(data->chip_info->fifo_length, val);

	mutex_lock(&data->mutex);
	data->watermark = val;
	mutex_unlock(&data->mutex);

	return 0;
}

static ssize_t hwfifo_enabled_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct kx022a_data *data = iio_priv(idev);
	bool state;

	mutex_lock(&data->mutex);
	state = data->state;
	mutex_unlock(&data->mutex);

	return sysfs_emit(buf, "%d\n", state);
}

static ssize_t hwfifo_watermark_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct kx022a_data *data = iio_priv(idev);
	int wm;

	mutex_lock(&data->mutex);
	wm = data->watermark;
	mutex_unlock(&data->mutex);

	return sysfs_emit(buf, "%d\n", wm);
}

static IIO_DEVICE_ATTR_RO(hwfifo_enabled, 0);
static IIO_DEVICE_ATTR_RO(hwfifo_watermark, 0);

static const struct iio_dev_attr *kx022a_fifo_attributes[] = {
	&iio_dev_attr_hwfifo_watermark,
	&iio_dev_attr_hwfifo_enabled,
	NULL
};

static int kx022a_drop_fifo_contents(struct kx022a_data *data)
{
	/*
	 * We must clear the old time-stamp to avoid computing the timestamps
	 * based on samples acquired when buffer was last enabled.
	 *
	 * We don't need to protect the timestamp as long as we are only
	 * called from fifo-disable where we can guarantee the sensor is not
	 * triggering interrupts and where the mutex is locked to prevent the
	 * user-space access.
	 */
	data->timestamp = 0;

	return regmap_write(data->regmap, data->chip_info->buf_clear, 0x0);
}

static int kx022a_get_fifo_bytes_available(struct kx022a_data *data)
{
	int ret, fifo_bytes;

	ret = regmap_read(data->regmap, KX022A_REG_BUF_STATUS_1, &fifo_bytes);
	if (ret) {
		dev_err(data->dev, "Error reading buffer status\n");
		return ret;
	}

	if (fifo_bytes == KX022A_FIFO_FULL_VALUE)
		return KX022A_FIFO_MAX_BYTES;

	return fifo_bytes;
}

static int kx132_get_fifo_bytes_available(struct kx022a_data *data)
{
	__le16 buf_status;
	int ret, fifo_bytes;

	ret = regmap_bulk_read(data->regmap, data->chip_info->buf_status1,
			       &buf_status, sizeof(buf_status));
	if (ret) {
		dev_err(data->dev, "Error reading buffer status\n");
		return ret;
	}

	fifo_bytes = le16_to_cpu(buf_status);
	fifo_bytes &= data->chip_info->buf_smp_lvl_mask;
	fifo_bytes = min((unsigned int)fifo_bytes, data->chip_info->fifo_length *
			 KX022A_FIFO_SAMPLES_SIZE_BYTES);

	return fifo_bytes;
}

static int __kx022a_fifo_flush(struct iio_dev *idev, unsigned int samples,
			       bool irq)
{
	struct kx022a_data *data = iio_priv(idev);
	uint64_t sample_period;
	int count, fifo_bytes;
	bool renable = false;
	int64_t tstamp;
	int ret, i;

	fifo_bytes = data->chip_info->get_fifo_bytes_available(data);

	if (fifo_bytes % KX022A_FIFO_SAMPLES_SIZE_BYTES)
		dev_warn(data->dev, "Bad FIFO alignment. Data may be corrupt\n");

	count = fifo_bytes / KX022A_FIFO_SAMPLES_SIZE_BYTES;
	if (!count)
		return 0;

	/*
	 * If we are being called from IRQ handler we know the stored timestamp
	 * is fairly accurate for the last stored sample. Otherwise, if we are
	 * called as a result of a read operation from userspace and hence
	 * before the watermark interrupt was triggered, take a timestamp
	 * now. We can fall anywhere in between two samples so the error in this
	 * case is at most one sample period.
	 */
	if (!irq) {
		/*
		 * We need to have the IRQ disabled or we risk of messing-up
		 * the timestamps. If we are ran from IRQ, then the
		 * IRQF_ONESHOT has us covered - but if we are ran by the
		 * user-space read we need to disable the IRQ to be on a safe
		 * side. We do this usng synchronous disable so that if the
		 * IRQ thread is being ran on other CPU we wait for it to be
		 * finished.
		 */
		disable_irq(data->irq);
		renable = true;

		data->old_timestamp = data->timestamp;
		data->timestamp = iio_get_time_ns(idev);
	}

	/*
	 * Approximate timestamps for each of the sample based on the sampling
	 * frequency, timestamp for last sample and number of samples.
	 *
	 * We'd better not use the current bandwidth settings to compute the
	 * sample period. The real sample rate varies with the device and
	 * small variation adds when we store a large number of samples.
	 *
	 * To avoid this issue we compute the actual sample period ourselves
	 * based on the timestamp delta between the last two flush operations.
	 */
	if (data->old_timestamp) {
		sample_period = data->timestamp - data->old_timestamp;
		do_div(sample_period, count);
	} else {
		sample_period = data->odr_ns;
	}
	tstamp = data->timestamp - (count - 1) * sample_period;

	if (samples && count > samples) {
		/*
		 * Here we leave some old samples to the buffer. We need to
		 * adjust the timestamp to match the first sample in the buffer
		 * or we will miscalculate the sample_period at next round.
		 */
		data->timestamp -= (count - samples) * sample_period;
		count = samples;
	}

	fifo_bytes = count * KX022A_FIFO_SAMPLES_SIZE_BYTES;
	ret = regmap_noinc_read(data->regmap, data->chip_info->buf_read,
				data->fifo_buffer, fifo_bytes);
	if (ret)
		goto renable_out;

	for (i = 0; i < count; i++) {
		__le16 *sam = &data->fifo_buffer[i * 3];
		__le16 *chs;
		int bit;

		chs = &data->scan.channels[0];
		for_each_set_bit(bit, idev->active_scan_mask, AXIS_MAX)
			chs[bit] = sam[bit];

		iio_push_to_buffers_with_timestamp(idev, &data->scan, tstamp);

		tstamp += sample_period;
	}

	ret = count;

renable_out:
	if (renable)
		enable_irq(data->irq);

	return ret;
}

static int kx022a_fifo_flush(struct iio_dev *idev, unsigned int samples)
{
	struct kx022a_data *data = iio_priv(idev);
	int ret;

	mutex_lock(&data->mutex);
	ret = __kx022a_fifo_flush(idev, samples, false);
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_info kx022a_info = {
	.read_raw = &kx022a_read_raw,
	.write_raw = &kx022a_write_raw,
	.write_raw_get_fmt = &kx022a_write_raw_get_fmt,
	.read_avail = &kx022a_read_avail,

	.validate_trigger	= iio_validate_own_trigger,
	.hwfifo_set_watermark	= kx022a_set_watermark,
	.hwfifo_flush_to_buffer	= kx022a_fifo_flush,
};

static int kx022a_set_drdy_irq(struct kx022a_data *data, bool en)
{
	if (en)
		return regmap_set_bits(data->regmap, data->chip_info->cntl,
				       KX022A_MASK_DRDY);

	return regmap_clear_bits(data->regmap, data->chip_info->cntl,
				 KX022A_MASK_DRDY);
}

static int kx022a_prepare_irq_pin(struct kx022a_data *data)
{
	/* Enable IRQ1 pin. Set polarity to active low */
	int mask = KX022A_MASK_IEN | KX022A_MASK_IPOL |
		   KX022A_MASK_ITYP;
	int val = KX022A_MASK_IEN | KX022A_IPOL_LOW |
		  KX022A_ITYP_LEVEL;
	int ret;

	ret = regmap_update_bits(data->regmap, data->inc_reg, mask, val);
	if (ret)
		return ret;

	/* We enable WMI to IRQ pin only at buffer_enable */
	mask = KX022A_MASK_INS2_DRDY;

	return regmap_set_bits(data->regmap, data->ien_reg, mask);
}

static int kx022a_fifo_disable(struct kx022a_data *data)
{
	int ret = 0;

	ret = kx022a_turn_off_lock(data);
	if (ret)
		return ret;

	ret = regmap_clear_bits(data->regmap, data->ien_reg, KX022A_MASK_WMI);
	if (ret)
		goto unlock_out;

	ret = regmap_clear_bits(data->regmap, data->chip_info->buf_cntl2,
				KX022A_MASK_BUF_EN);
	if (ret)
		goto unlock_out;

	data->state &= ~KX022A_STATE_FIFO;

	kx022a_drop_fifo_contents(data);

	kfree(data->fifo_buffer);

	return kx022a_turn_on_unlock(data);

unlock_out:
	mutex_unlock(&data->mutex);

	return ret;
}

static int kx022a_buffer_predisable(struct iio_dev *idev)
{
	struct kx022a_data *data = iio_priv(idev);

	if (iio_device_get_current_mode(idev) == INDIO_BUFFER_TRIGGERED)
		return 0;

	return kx022a_fifo_disable(data);
}

static int kx022a_fifo_enable(struct kx022a_data *data)
{
	int ret;

	data->fifo_buffer = kmalloc_array(data->chip_info->fifo_length,
					  KX022A_FIFO_SAMPLES_SIZE_BYTES,
					  GFP_KERNEL);
	if (!data->fifo_buffer)
		return -ENOMEM;

	ret = kx022a_turn_off_lock(data);
	if (ret)
		return ret;

	/* Update watermark to HW */
	ret = kx022a_fifo_set_wmi(data);
	if (ret)
		goto unlock_out;

	/* Enable buffer */
	ret = regmap_set_bits(data->regmap, data->chip_info->buf_cntl2,
			      KX022A_MASK_BUF_EN);
	if (ret)
		goto unlock_out;

	data->state |= KX022A_STATE_FIFO;
	ret = regmap_set_bits(data->regmap, data->ien_reg,
			      KX022A_MASK_WMI);
	if (ret)
		goto unlock_out;

	return kx022a_turn_on_unlock(data);

unlock_out:
	mutex_unlock(&data->mutex);

	return ret;
}

static int kx022a_buffer_postenable(struct iio_dev *idev)
{
	struct kx022a_data *data = iio_priv(idev);

	/*
	 * If we use data-ready trigger, then the IRQ masks should be handled by
	 * trigger enable and the hardware buffer is not used but we just update
	 * results to the IIO fifo when data-ready triggers.
	 */
	if (iio_device_get_current_mode(idev) == INDIO_BUFFER_TRIGGERED)
		return 0;

	return kx022a_fifo_enable(data);
}

static const struct iio_buffer_setup_ops kx022a_buffer_ops = {
	.postenable = kx022a_buffer_postenable,
	.predisable = kx022a_buffer_predisable,
};

static irqreturn_t kx022a_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *idev = pf->indio_dev;
	struct kx022a_data *data = iio_priv(idev);
	int ret;

	ret = regmap_bulk_read(data->regmap, data->chip_info->xout_l, data->buffer,
			       KX022A_FIFO_SAMPLES_SIZE_BYTES);
	if (ret < 0)
		goto err_read;

	iio_push_to_buffers_with_timestamp(idev, data->buffer, data->timestamp);
err_read:
	iio_trigger_notify_done(idev->trig);

	return IRQ_HANDLED;
}

/* Get timestamps and wake the thread if we need to read data */
static irqreturn_t kx022a_irq_handler(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct kx022a_data *data = iio_priv(idev);

	data->old_timestamp = data->timestamp;
	data->timestamp = iio_get_time_ns(idev);

	if (data->state & KX022A_STATE_FIFO || data->trigger_enabled)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

/*
 * WMI and data-ready IRQs are acked when results are read. If we add
 * TILT/WAKE or other IRQs - then we may need to implement the acking
 * (which is racy).
 */
static irqreturn_t kx022a_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct kx022a_data *data = iio_priv(idev);
	irqreturn_t ret = IRQ_NONE;

	mutex_lock(&data->mutex);

	if (data->trigger_enabled) {
		iio_trigger_poll_nested(data->trig);
		ret = IRQ_HANDLED;
	}

	if (data->state & KX022A_STATE_FIFO) {
		int ok;

		ok = __kx022a_fifo_flush(idev, data->chip_info->fifo_length, true);
		if (ok > 0)
			ret = IRQ_HANDLED;
	}

	mutex_unlock(&data->mutex);

	return ret;
}

static int kx022a_trigger_set_state(struct iio_trigger *trig,
				    bool state)
{
	struct kx022a_data *data = iio_trigger_get_drvdata(trig);
	int ret = 0;

	mutex_lock(&data->mutex);

	if (data->trigger_enabled == state)
		goto unlock_out;

	if (data->state & KX022A_STATE_FIFO) {
		dev_warn(data->dev, "Can't set trigger when FIFO enabled\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	ret = kx022a_turn_on_off_unlocked(data, false);
	if (ret)
		goto unlock_out;

	data->trigger_enabled = state;
	ret = kx022a_set_drdy_irq(data, state);
	if (ret)
		goto unlock_out;

	ret = kx022a_turn_on_off_unlocked(data, true);

unlock_out:
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops kx022a_trigger_ops = {
	.set_trigger_state = kx022a_trigger_set_state,
};

static int kx022a_chip_init(struct kx022a_data *data)
{
	int ret, val;

	/* Reset the senor */
	ret = regmap_write(data->regmap, data->chip_info->cntl2, KX022A_MASK_SRST);
	if (ret)
		return ret;

	/*
	 * I've seen I2C read failures if we poll too fast after the sensor
	 * reset. Slight delay gives I2C block the time to recover.
	 */
	msleep(1);

	ret = regmap_read_poll_timeout(data->regmap, data->chip_info->cntl2, val,
				       !(val & KX022A_MASK_SRST),
				       KX022A_SOFT_RESET_WAIT_TIME_US,
				       KX022A_SOFT_RESET_TOTAL_WAIT_TIME_US);
	if (ret) {
		dev_err(data->dev, "Sensor reset %s\n",
			val & KX022A_MASK_SRST ? "timeout" : "fail#");
		return ret;
	}

	ret = regmap_reinit_cache(data->regmap, data->chip_info->regmap_config);
	if (ret) {
		dev_err(data->dev, "Failed to reinit reg cache\n");
		return ret;
	}

	/* set data res 16bit */
	ret = regmap_set_bits(data->regmap, data->chip_info->buf_cntl2,
			      KX022A_MASK_BRES16);
	if (ret) {
		dev_err(data->dev, "Failed to set data resolution\n");
		return ret;
	}

	return kx022a_prepare_irq_pin(data);
}

const struct kx022a_chip_info kx022a_chip_info = {
	.name				= "kx022-accel",
	.regmap_config			= &kx022a_regmap_config,
	.channels			= kx022a_channels,
	.num_channels			= ARRAY_SIZE(kx022a_channels),
	.fifo_length			= KX022A_FIFO_LENGTH,
	.who				= KX022A_REG_WHO,
	.id				= KX022A_ID,
	.cntl				= KX022A_REG_CNTL,
	.cntl2				= KX022A_REG_CNTL2,
	.odcntl				= KX022A_REG_ODCNTL,
	.buf_cntl1			= KX022A_REG_BUF_CNTL1,
	.buf_cntl2			= KX022A_REG_BUF_CNTL2,
	.buf_clear			= KX022A_REG_BUF_CLEAR,
	.buf_status1			= KX022A_REG_BUF_STATUS_1,
	.buf_read			= KX022A_REG_BUF_READ,
	.inc1				= KX022A_REG_INC1,
	.inc4				= KX022A_REG_INC4,
	.inc5				= KX022A_REG_INC5,
	.inc6				= KX022A_REG_INC6,
	.xout_l				= KX022A_REG_XOUT_L,
	.get_fifo_bytes_available	= kx022a_get_fifo_bytes_available,
};
EXPORT_SYMBOL_NS_GPL(kx022a_chip_info, IIO_KX022A);

const struct kx022a_chip_info kx132_chip_info = {
	.name			  = "kx132-1211",
	.regmap_config		  = &kx132_regmap_config,
	.channels		  = kx132_channels,
	.num_channels		  = ARRAY_SIZE(kx132_channels),
	.fifo_length		  = KX132_FIFO_LENGTH,
	.who			  = KX132_REG_WHO,
	.id			  = KX132_ID,
	.cntl			  = KX132_REG_CNTL,
	.cntl2			  = KX132_REG_CNTL2,
	.odcntl			  = KX132_REG_ODCNTL,
	.buf_cntl1		  = KX132_REG_BUF_CNTL1,
	.buf_cntl2		  = KX132_REG_BUF_CNTL2,
	.buf_clear		  = KX132_REG_BUF_CLEAR,
	.buf_status1		  = KX132_REG_BUF_STATUS_1,
	.buf_smp_lvl_mask	  = KX132_MASK_BUF_SMP_LVL,
	.buf_read		  = KX132_REG_BUF_READ,
	.inc1			  = KX132_REG_INC1,
	.inc4			  = KX132_REG_INC4,
	.inc5			  = KX132_REG_INC5,
	.inc6			  = KX132_REG_INC6,
	.xout_l			  = KX132_REG_XOUT_L,
	.get_fifo_bytes_available = kx132_get_fifo_bytes_available,
};
EXPORT_SYMBOL_NS_GPL(kx132_chip_info, IIO_KX022A);

/*
 * Despite the naming, KX132ACR-LBZ is not similar to KX132-1211 but it is
 * exact subset of KX022A. KX132ACR-LBZ is meant to be used for industrial
 * applications and the tap/double tap, free fall and tilt engines were
 * removed. Rest of the registers and functionalities (excluding the ID
 * register) are exact match to what is found in KX022.
 */
const struct kx022a_chip_info kx132acr_chip_info = {
	.name				= "kx132acr-lbz",
	.regmap_config			= &kx022a_regmap_config,
	.channels			= kx022a_channels,
	.num_channels			= ARRAY_SIZE(kx022a_channels),
	.fifo_length			= KX022A_FIFO_LENGTH,
	.who				= KX022A_REG_WHO,
	.id				= KX132ACR_LBZ_ID,
	.cntl				= KX022A_REG_CNTL,
	.cntl2				= KX022A_REG_CNTL2,
	.odcntl				= KX022A_REG_ODCNTL,
	.buf_cntl1			= KX022A_REG_BUF_CNTL1,
	.buf_cntl2			= KX022A_REG_BUF_CNTL2,
	.buf_clear			= KX022A_REG_BUF_CLEAR,
	.buf_status1			= KX022A_REG_BUF_STATUS_1,
	.buf_read			= KX022A_REG_BUF_READ,
	.inc1				= KX022A_REG_INC1,
	.inc4				= KX022A_REG_INC4,
	.inc5				= KX022A_REG_INC5,
	.inc6				= KX022A_REG_INC6,
	.xout_l				= KX022A_REG_XOUT_L,
	.get_fifo_bytes_available	= kx022a_get_fifo_bytes_available,
};
EXPORT_SYMBOL_NS_GPL(kx132acr_chip_info, IIO_KX022A);

int kx022a_probe_internal(struct device *dev, const struct kx022a_chip_info *chip_info)
{
	static const char * const regulator_names[] = {"io-vdd", "vdd"};
	struct iio_trigger *indio_trig;
	struct fwnode_handle *fwnode;
	struct kx022a_data *data;
	struct regmap *regmap;
	unsigned int chip_id;
	struct iio_dev *idev;
	int ret, irq;
	char *name;

	regmap = dev_get_regmap(dev, NULL);
	if (!regmap) {
		dev_err(dev, "no regmap\n");
		return -EINVAL;
	}

	fwnode = dev_fwnode(dev);
	if (!fwnode)
		return -ENODEV;

	idev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!idev)
		return -ENOMEM;

	data = iio_priv(idev);
	data->chip_info = chip_info;

	/*
	 * VDD is the analog and digital domain voltage supply and
	 * IO_VDD is the digital I/O voltage supply.
	 */
	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(regulator_names),
					     regulator_names);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable regulator\n");

	ret = regmap_read(regmap, chip_info->who, &chip_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to access sensor\n");

	if (chip_id != chip_info->id)
		dev_warn(dev, "unknown device 0x%x\n", chip_id);

	irq = fwnode_irq_get_byname(fwnode, "INT1");
	if (irq > 0) {
		data->inc_reg = chip_info->inc1;
		data->ien_reg = chip_info->inc4;
	} else {
		irq = fwnode_irq_get_byname(fwnode, "INT2");
		if (irq < 0)
			return dev_err_probe(dev, irq, "No suitable IRQ\n");

		data->inc_reg = chip_info->inc5;
		data->ien_reg = chip_info->inc6;
	}

	data->regmap = regmap;
	data->dev = dev;
	data->irq = irq;
	data->odr_ns = KX022A_DEFAULT_PERIOD_NS;
	mutex_init(&data->mutex);

	idev->channels = chip_info->channels;
	idev->num_channels = chip_info->num_channels;
	idev->name = chip_info->name;
	idev->info = &kx022a_info;
	idev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;
	idev->available_scan_masks = kx022a_scan_masks;

	/* Read the mounting matrix, if present */
	ret = iio_read_mount_matrix(dev, &data->orientation);
	if (ret)
		return ret;

	/* The sensor must be turned off for configuration */
	ret = kx022a_turn_off_lock(data);
	if (ret)
		return ret;

	ret = kx022a_chip_init(data);
	if (ret) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret = kx022a_turn_on_unlock(data);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup_ext(dev, idev,
						  &iio_pollfunc_store_time,
						  kx022a_trigger_handler,
						  IIO_BUFFER_DIRECTION_IN,
						  &kx022a_buffer_ops,
						  kx022a_fifo_attributes);

	if (ret)
		return dev_err_probe(data->dev, ret,
				     "iio_triggered_buffer_setup_ext FAIL\n");
	indio_trig = devm_iio_trigger_alloc(dev, "%sdata-rdy-dev%d", idev->name,
					    iio_device_id(idev));
	if (!indio_trig)
		return -ENOMEM;

	data->trig = indio_trig;

	indio_trig->ops = &kx022a_trigger_ops;
	iio_trigger_set_drvdata(indio_trig, data);

	/*
	 * No need to check for NULL. request_threaded_irq() defaults to
	 * dev_name() should the alloc fail.
	 */
	name = devm_kasprintf(data->dev, GFP_KERNEL, "%s-kx022a",
			      dev_name(data->dev));

	ret = devm_request_threaded_irq(data->dev, irq, kx022a_irq_handler,
					&kx022a_irq_thread_handler,
					IRQF_ONESHOT, name, idev);
	if (ret)
		return dev_err_probe(data->dev, ret, "Could not request IRQ\n");

	ret = devm_iio_trigger_register(dev, indio_trig);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Trigger registration failed\n");

	ret = devm_iio_device_register(data->dev, idev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Unable to register iio device\n");

	return ret;
}
EXPORT_SYMBOL_NS_GPL(kx022a_probe_internal, IIO_KX022A);

MODULE_DESCRIPTION("ROHM/Kionix KX022A accelerometer driver");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_LICENSE("GPL");
