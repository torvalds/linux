// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Copyright (c) 2025 Robert Bosch GmbH.
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "smi330.h"

/* Register map */
#define SMI330_CHIP_ID_REG 0x00
#define SMI330_ERR_REG 0x01
#define SMI330_STATUS_REG 0x02
#define SMI330_ACCEL_X_REG 0x03
#define SMI330_GYRO_X_REG 0x06
#define SMI330_TEMP_REG 0x09
#define SMI330_INT1_STATUS_REG 0x0D
#define SMI330_ACCEL_CFG_REG 0x20
#define SMI330_GYRO_CFG_REG 0x21
#define SMI330_IO_INT_CTRL_REG 0x38
#define SMI330_INT_CONF_REG 0x39
#define SMI330_INT_MAP1_REG 0x3A
#define SMI330_INT_MAP2_REG 0x3B
#define SMI330_CMD_REG 0x7E

/* Register mask */
#define SMI330_CHIP_ID_MASK GENMASK(7, 0)
#define SMI330_ERR_FATAL_MASK BIT(0)
#define SMI330_ERR_ACC_CONF_MASK BIT(5)
#define SMI330_ERR_GYR_CONF_MASK BIT(6)
#define SMI330_STATUS_POR_MASK BIT(0)
#define SMI330_INT_STATUS_ACC_GYR_DRDY_MASK GENMASK(13, 12)
#define SMI330_CFG_ODR_MASK GENMASK(3, 0)
#define SMI330_CFG_RANGE_MASK GENMASK(6, 4)
#define SMI330_CFG_BW_MASK BIT(7)
#define SMI330_CFG_AVG_NUM_MASK GENMASK(10, 8)
#define SMI330_CFG_MODE_MASK GENMASK(14, 12)
#define SMI330_IO_INT_CTRL_INT1_MASK GENMASK(2, 0)
#define SMI330_IO_INT_CTRL_INT2_MASK GENMASK(10, 8)
#define SMI330_INT_CONF_LATCH_MASK BIT(0)
#define SMI330_INT_MAP2_ACC_DRDY_MASK GENMASK(11, 10)
#define SMI330_INT_MAP2_GYR_DRDY_MASK GENMASK(9, 8)

/* Register values */
#define SMI330_IO_INT_CTRL_LVL BIT(0)
#define SMI330_IO_INT_CTRL_OD BIT(1)
#define SMI330_IO_INT_CTRL_EN BIT(2)
#define SMI330_CMD_SOFT_RESET 0xDEAF

/* T°C = (temp / 512) + 23 */
#define SMI330_TEMP_OFFSET 11776 /* 23 * 512 */
#define SMI330_TEMP_SCALE 1953125 /* (1 / 512) * 1e9 */

#define SMI330_CHIP_ID 0x42
#define SMI330_SOFT_RESET_DELAY 2000

/* Non-constant mask variant of FIELD_GET() and FIELD_PREP() */
#define smi330_field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define smi330_field_prep(_mask, _val) (((_val) << (ffs(_mask) - 1)) & (_mask))

#define SMI330_ACCEL_CHANNEL(_axis) {					\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type =					\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |				\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |			\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_type_available =				\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |			\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_dir_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_index = SMI330_SCAN_ACCEL_##_axis,			\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
}

#define SMI330_GYRO_CHANNEL(_axis) {					\
	.type = IIO_ANGL_VEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type =					\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |				\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |			\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_type_available =				\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |			\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_dir_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_index = SMI330_SCAN_GYRO_##_axis,				\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
}

#define SMI330_TEMP_CHANNEL(_index) {			\
	.type = IIO_TEMP,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
		BIT(IIO_CHAN_INFO_OFFSET) |		\
		BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = _index,				\
	.scan_type = {					\
		.sign = 's',				\
		.realbits = 16,				\
		.storagebits = 16,			\
		.endianness = IIO_LE,			\
	},						\
}

enum smi330_accel_range {
	SMI330_ACCEL_RANGE_2G = 0x00,
	SMI330_ACCEL_RANGE_4G = 0x01,
	SMI330_ACCEL_RANGE_8G = 0x02,
	SMI330_ACCEL_RANGE_16G = 0x03
};

enum smi330_gyro_range {
	SMI330_GYRO_RANGE_125 = 0x0,
	SMI330_GYRO_RANGE_250 = 0x01,
	SMI330_GYRO_RANGE_500 = 0x02
};

enum smi330_odr {
	SMI330_ODR_12_5_HZ = 0x05,
	SMI330_ODR_25_HZ = 0x06,
	SMI330_ODR_50_HZ = 0x07,
	SMI330_ODR_100_HZ = 0x08,
	SMI330_ODR_200_HZ = 0x09,
	SMI330_ODR_400_HZ = 0x0A,
	SMI330_ODR_800_HZ = 0x0B,
	SMI330_ODR_1600_HZ = 0x0C,
	SMI330_ODR_3200_HZ = 0x0D,
	SMI330_ODR_6400_HZ = 0x0E
};

enum smi330_avg_num {
	SMI330_AVG_NUM_1 = 0x00,
	SMI330_AVG_NUM_2 = 0x01,
	SMI330_AVG_NUM_4 = 0x02,
	SMI330_AVG_NUM_8 = 0x03,
	SMI330_AVG_NUM_16 = 0x04,
	SMI330_AVG_NUM_32 = 0x05,
	SMI330_AVG_NUM_64 = 0x06
};

enum smi330_mode {
	SMI330_MODE_SUSPEND = 0x00,
	SMI330_MODE_GYRO_DRIVE = 0x01,
	SMI330_MODE_LOW_POWER = 0x03,
	SMI330_MODE_NORMAL = 0x04,
	SMI330_MODE_HIGH_PERF = 0x07
};

enum smi330_bw {
	SMI330_BW_2 = 0x00, /* ODR/2 */
	SMI330_BW_4 = 0x01 /* ODR/4 */
};

enum smi330_operation_mode {
	SMI330_POLLING,
	SMI330_DATA_READY,
};

enum smi330_sensor {
	SMI330_ACCEL,
	SMI330_GYRO,
};

enum smi330_sensor_conf_select {
	SMI330_ODR,
	SMI330_RANGE,
	SMI330_BW,
	SMI330_AVG_NUM,
};

enum smi330_int_out {
	SMI330_INT_DISABLED,
	SMI330_INT_1,
	SMI330_INT_2,
};

struct smi330_attributes {
	int *reg_vals;
	int *vals;
	int len;
	int type;
	int mask;
};

struct smi330_cfg {
	enum smi330_operation_mode op_mode;
	enum smi330_int_out data_irq;
};

struct smi330_data {
	struct regmap *regmap;
	struct smi330_cfg cfg;
	struct iio_trigger *trig;
	IIO_DECLARE_BUFFER_WITH_TS(__le16, buf, SMI330_SCAN_LEN);
};

const struct regmap_config smi330_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};
EXPORT_SYMBOL_NS_GPL(smi330_regmap_config, "IIO_SMI330");

static const struct iio_chan_spec smi330_channels[] = {
	SMI330_ACCEL_CHANNEL(X),
	SMI330_ACCEL_CHANNEL(Y),
	SMI330_ACCEL_CHANNEL(Z),
	SMI330_GYRO_CHANNEL(X),
	SMI330_GYRO_CHANNEL(Y),
	SMI330_GYRO_CHANNEL(Z),
	SMI330_TEMP_CHANNEL(-1), /* No buffer support */
	IIO_CHAN_SOFT_TIMESTAMP(SMI330_SCAN_TIMESTAMP),
};

static const unsigned long smi330_avail_scan_masks[] = {
	(BIT(SMI330_SCAN_ACCEL_X) | BIT(SMI330_SCAN_ACCEL_Y) |
	 BIT(SMI330_SCAN_ACCEL_Z) | BIT(SMI330_SCAN_GYRO_X) |
	 BIT(SMI330_SCAN_GYRO_Y) | BIT(SMI330_SCAN_GYRO_Z)),
	0
};

static const struct smi330_attributes smi330_accel_scale_attr = {
	.reg_vals = (int[]){ SMI330_ACCEL_RANGE_2G, SMI330_ACCEL_RANGE_4G,
			     SMI330_ACCEL_RANGE_8G, SMI330_ACCEL_RANGE_16G },
	.vals = (int[]){ 0, 61035, 0, 122070, 0, 244140, 0, 488281 },
	.len = 8,
	.type = IIO_VAL_INT_PLUS_NANO,
	.mask = SMI330_CFG_RANGE_MASK
};

static const struct smi330_attributes smi330_gyro_scale_attr = {
	.reg_vals = (int[]){ SMI330_GYRO_RANGE_125, SMI330_GYRO_RANGE_250,
			     SMI330_GYRO_RANGE_500 },
	.vals = (int[]){ 0, 3814697, 0, 7629395, 0, 15258789 },
	.len = 6,
	.type = IIO_VAL_INT_PLUS_NANO,
	.mask = SMI330_CFG_RANGE_MASK
};

static const struct smi330_attributes smi330_average_attr = {
	.reg_vals = (int[]){ SMI330_AVG_NUM_1, SMI330_AVG_NUM_2,
			     SMI330_AVG_NUM_4, SMI330_AVG_NUM_8,
			     SMI330_AVG_NUM_16, SMI330_AVG_NUM_32,
			     SMI330_AVG_NUM_64 },
	.vals = (int[]){ 1, 2, 4, 8, 16, 32, 64 },
	.len = 7,
	.type = IIO_VAL_INT,
	.mask = SMI330_CFG_AVG_NUM_MASK
};

static const struct smi330_attributes smi330_bandwidth_attr = {
	.reg_vals = (int[]){ SMI330_BW_2, SMI330_BW_4 },
	.vals = (int[]){ 2, 4 },
	.len = 2,
	.type = IIO_VAL_INT,
	.mask = SMI330_CFG_BW_MASK
};

static const struct smi330_attributes smi330_odr_attr = {
	.reg_vals = (int[]){ SMI330_ODR_12_5_HZ, SMI330_ODR_25_HZ,
			     SMI330_ODR_50_HZ, SMI330_ODR_100_HZ,
			     SMI330_ODR_200_HZ, SMI330_ODR_400_HZ,
			     SMI330_ODR_800_HZ, SMI330_ODR_1600_HZ,
			     SMI330_ODR_3200_HZ, SMI330_ODR_6400_HZ },
	.vals = (int[]){ 12, 25, 50, 100, 200, 400, 800, 1600, 3200, 6400 },
	.len = 10,
	.type = IIO_VAL_INT,
	.mask = SMI330_CFG_ODR_MASK
};

static int smi330_get_attributes(enum smi330_sensor_conf_select config,
				 enum smi330_sensor sensor,
				 const struct smi330_attributes **attr)
{
	switch (config) {
	case SMI330_ODR:
		*attr = &smi330_odr_attr;
		return 0;
	case SMI330_RANGE:
		if (sensor == SMI330_ACCEL)
			*attr = &smi330_accel_scale_attr;
		else
			*attr = &smi330_gyro_scale_attr;
		return 0;
	case SMI330_BW:
		*attr = &smi330_bandwidth_attr;
		return 0;
	case SMI330_AVG_NUM:
		*attr = &smi330_average_attr;
		return 0;
	default:
		return -EINVAL;
	}
}

static int smi330_get_config_reg(enum smi330_sensor sensor, int *reg)
{
	switch (sensor) {
	case SMI330_ACCEL:
		*reg = SMI330_ACCEL_CFG_REG;
		return 0;
	case SMI330_GYRO:
		*reg = SMI330_GYRO_CFG_REG;
		return 0;
	default:
		return -EINVAL;
	}
}

static int smi330_get_sensor_config(struct smi330_data *data,
				    enum smi330_sensor sensor,
				    enum smi330_sensor_conf_select config,
				    int *value)

{
	int ret, reg, reg_val, i;
	const struct smi330_attributes *attr;

	ret = smi330_get_config_reg(sensor, &reg);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, reg, &reg_val);
	if (ret)
		return ret;

	ret = smi330_get_attributes(config, sensor, &attr);
	if (ret)
		return ret;

	reg_val = smi330_field_get(attr->mask, reg_val);

	if (attr->type == IIO_VAL_INT) {
		for (i = 0; i < attr->len; i++) {
			if (attr->reg_vals[i] == reg_val) {
				*value = attr->vals[i];
				return 0;
			}
		}
	} else {
		for (i = 0; i < attr->len / 2; i++) {
			if (attr->reg_vals[i] == reg_val) {
				*value = attr->vals[2 * i + 1];
				return 0;
			}
		}
	}

	return -EINVAL;
}

static int smi330_set_sensor_config(struct smi330_data *data,
				    enum smi330_sensor sensor,
				    enum smi330_sensor_conf_select config,
				    int value)
{
	int ret, i, reg, reg_val, error;
	const struct smi330_attributes *attr;

	ret = smi330_get_attributes(config, sensor, &attr);
	if (ret)
		return ret;

	for (i = 0; i < attr->len; i++) {
		if (attr->vals[i] == value) {
			if (attr->type == IIO_VAL_INT)
				reg_val = attr->reg_vals[i];
			else
				reg_val = attr->reg_vals[i / 2];
			break;
		}
	}
	if (i == attr->len)
		return -EINVAL;

	ret = smi330_get_config_reg(sensor, &reg);
	if (ret)
		return ret;

	reg_val = smi330_field_prep(attr->mask, reg_val);
	ret = regmap_update_bits(data->regmap, reg, attr->mask, reg_val);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, SMI330_ERR_REG, &error);
	if (ret)
		return ret;

	if (FIELD_GET(SMI330_ERR_ACC_CONF_MASK, error) ||
	    FIELD_GET(SMI330_ERR_GYR_CONF_MASK, error))
		return -EIO;

	return 0;
}

static int smi330_get_data(struct smi330_data *data, int chan_type, int axis,
			   int *val)
{
	u8 reg;
	int ret, sample;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = SMI330_ACCEL_X_REG + (axis - IIO_MOD_X);
		break;
	case IIO_ANGL_VEL:
		reg = SMI330_GYRO_X_REG + (axis - IIO_MOD_X);
		break;
	case IIO_TEMP:
		reg = SMI330_TEMP_REG;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, reg, &sample);
	if (ret)
		return ret;

	*val = sign_extend32(sample, 15);

	return 0;
}

static int smi330_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, const int **vals,
			     int *type, int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_ACCEL) {
			*vals = smi330_accel_scale_attr.vals;
			*length = smi330_accel_scale_attr.len;
			*type = smi330_accel_scale_attr.type;
		} else {
			*vals = smi330_gyro_scale_attr.vals;
			*length = smi330_gyro_scale_attr.len;
			*type = smi330_gyro_scale_attr.type;
		}
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = smi330_average_attr.vals;
		*length = smi330_average_attr.len;
		*type = smi330_average_attr.type;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = smi330_bandwidth_attr.vals;
		*length = smi330_bandwidth_attr.len;
		*type = smi330_bandwidth_attr.type;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = smi330_odr_attr.vals;
		*length = smi330_odr_attr.len;
		*type = smi330_odr_attr.type;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int smi330_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct smi330_data *data = iio_priv(indio_dev);
	enum smi330_sensor sensor;

	/* valid for all channel types */
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = smi330_get_data(data, chan->type, chan->channel2, val);
		iio_device_release_direct(indio_dev);
		return ret ? ret : IIO_VAL_INT;
	default:
		break;
	}

	switch (chan->type) {
	case IIO_ACCEL:
		sensor = SMI330_ACCEL;
		break;
	case IIO_ANGL_VEL:
		sensor = SMI330_GYRO;
		break;
	case IIO_TEMP:
		switch (mask) {
		case IIO_CHAN_INFO_SCALE:
			*val = SMI330_TEMP_SCALE / GIGA;
			*val2 = SMI330_TEMP_SCALE % GIGA;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_CHAN_INFO_OFFSET:
			*val = SMI330_TEMP_OFFSET;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	/* valid for acc and gyro channels */
	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = smi330_get_sensor_config(data, sensor, SMI330_AVG_NUM,
					       val);
		return ret ? ret : IIO_VAL_INT;

	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		ret = smi330_get_sensor_config(data, sensor, SMI330_BW, val);
		return ret ? ret : IIO_VAL_INT;

	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = smi330_get_sensor_config(data, sensor, SMI330_ODR, val);
		return ret ? ret : IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		ret = smi330_get_sensor_config(data, sensor, SMI330_RANGE,
					       val2);
		return ret ? ret : IIO_VAL_INT_PLUS_NANO;

	default:
		return -EINVAL;
	}
}

static int smi330_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct smi330_data *data = iio_priv(indio_dev);
	enum smi330_sensor sensor;

	switch (chan->type) {
	case IIO_ACCEL:
		sensor = SMI330_ACCEL;
		break;
	case IIO_ANGL_VEL:
		sensor = SMI330_GYRO;
		break;
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return smi330_set_sensor_config(data, sensor, SMI330_RANGE,
						val2);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return smi330_set_sensor_config(data, sensor, SMI330_AVG_NUM,
						val);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return smi330_set_sensor_config(data, sensor, SMI330_BW, val);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return smi330_set_sensor_config(data, sensor, SMI330_ODR, val);
	default:
		return -EINVAL;
	}
}

static int smi330_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int smi330_soft_reset(struct smi330_data *data)
{
	int ret, dummy_byte;

	ret = regmap_write(data->regmap, SMI330_CMD_REG, SMI330_CMD_SOFT_RESET);
	if (ret)
		return ret;
	fsleep(SMI330_SOFT_RESET_DELAY);

	/* Performing a dummy read after a soft-reset */
	regmap_read(data->regmap, SMI330_CHIP_ID_REG, &dummy_byte);

	return 0;
}

static irqreturn_t smi330_trigger_handler(int irq, void *p)
{
	int ret;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct smi330_data *data = iio_priv(indio_dev);

	ret = regmap_bulk_read(data->regmap, SMI330_ACCEL_X_REG, data->buf,
			       SMI330_SCAN_LEN);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buf, pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t smi330_irq_thread_handler(int irq, void *indio_dev_)
{
	int ret, int_stat;
	s16 int_status[2] = { 0 };
	struct iio_dev *indio_dev = indio_dev_;
	struct smi330_data *data = iio_priv(indio_dev);

	ret = regmap_bulk_read(data->regmap, SMI330_INT1_STATUS_REG, int_status, 2);
	if (ret)
		return IRQ_NONE;

	int_stat = int_status[0] | int_status[1];

	if (FIELD_GET(SMI330_INT_STATUS_ACC_GYR_DRDY_MASK, int_stat)) {
		indio_dev->pollfunc->timestamp = iio_get_time_ns(indio_dev);
		iio_trigger_poll_nested(data->trig);
	}

	return IRQ_HANDLED;
}

static int smi330_set_int_pin_config(struct smi330_data *data,
				     enum smi330_int_out irq_num,
				     bool active_high, bool open_drain,
				     bool latch)
{
	int ret, val;

	val = active_high ? SMI330_IO_INT_CTRL_LVL : 0;
	val |= open_drain ? SMI330_IO_INT_CTRL_OD : 0;
	val |= SMI330_IO_INT_CTRL_EN;

	switch (irq_num) {
	case SMI330_INT_1:
		val = FIELD_PREP(SMI330_IO_INT_CTRL_INT1_MASK, val);
		ret = regmap_update_bits(data->regmap, SMI330_IO_INT_CTRL_REG,
					 SMI330_IO_INT_CTRL_INT1_MASK, val);
		if (ret)
			return ret;
		break;
	case SMI330_INT_2:
		val = FIELD_PREP(SMI330_IO_INT_CTRL_INT2_MASK, val);
		ret = regmap_update_bits(data->regmap, SMI330_IO_INT_CTRL_REG,
					 SMI330_IO_INT_CTRL_INT2_MASK, val);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(data->regmap, SMI330_INT_CONF_REG,
				  SMI330_INT_CONF_LATCH_MASK,
				  FIELD_PREP(SMI330_INT_CONF_LATCH_MASK,
					     latch));
}

static int smi330_setup_irq(struct device *dev, struct iio_dev *indio_dev,
			    int irq, enum smi330_int_out irq_num)
{
	int ret, irq_type;
	bool open_drain, active_high, latch;
	struct smi330_data *data = iio_priv(indio_dev);
	struct irq_data *desc;

	desc = irq_get_irq_data(irq);
	if (!desc)
		return -EINVAL;

	irq_type = irqd_get_trigger_type(desc);
	switch (irq_type) {
	case IRQF_TRIGGER_RISING:
		latch = false;
		active_high = true;
		break;
	case IRQF_TRIGGER_HIGH:
		latch = true;
		active_high = true;
		break;
	case IRQF_TRIGGER_FALLING:
		latch = false;
		active_high = false;
		break;
	case IRQF_TRIGGER_LOW:
		latch = true;
		active_high = false;
		break;
	default:
		return -EINVAL;
	}

	open_drain = device_property_read_bool(dev, "drive-open-drain");

	ret = smi330_set_int_pin_config(data, irq_num, active_high, open_drain,
					latch);
	if (ret)
		return ret;

	return devm_request_threaded_irq(dev, irq, NULL,
					 smi330_irq_thread_handler,
					 irq_type | IRQF_ONESHOT,
					 indio_dev->name, indio_dev);
}

static int smi330_register_irq(struct device *dev, struct iio_dev *indio_dev)
{
	int ret, irq;
	struct smi330_data *data = iio_priv(indio_dev);
	struct fwnode_handle *fwnode;

	fwnode = dev_fwnode(dev);
	if (!fwnode)
		return -ENODEV;

	data->cfg.data_irq = SMI330_INT_DISABLED;

	irq = fwnode_irq_get_byname(fwnode, "INT1");
	if (irq > 0) {
		ret = smi330_setup_irq(dev, indio_dev, irq, SMI330_INT_1);
		if (ret)
			return ret;
		data->cfg.data_irq = SMI330_INT_1;
	} else {
		irq = fwnode_irq_get_byname(fwnode, "INT2");
		if (irq > 0) {
			ret = smi330_setup_irq(dev, indio_dev, irq,
					       SMI330_INT_2);
			if (ret)
				return ret;
			data->cfg.data_irq = SMI330_INT_2;
		}
	}

	return 0;
}

static int smi330_set_drdy_trigger_state(struct iio_trigger *trig, bool enable)
{
	int val;
	struct smi330_data *data = iio_trigger_get_drvdata(trig);

	if (enable)
		data->cfg.op_mode = SMI330_DATA_READY;
	else
		data->cfg.op_mode = SMI330_POLLING;

	val = FIELD_PREP(SMI330_INT_MAP2_ACC_DRDY_MASK,
			 enable ? data->cfg.data_irq : 0);
	val |= FIELD_PREP(SMI330_INT_MAP2_GYR_DRDY_MASK,
			  enable ? data->cfg.data_irq : 0);
	return regmap_update_bits(data->regmap, SMI330_INT_MAP2_REG,
				  SMI330_INT_MAP2_ACC_DRDY_MASK |
					  SMI330_INT_MAP2_GYR_DRDY_MASK,
				  val);
}

static const struct iio_trigger_ops smi330_trigger_ops = {
	.set_trigger_state = &smi330_set_drdy_trigger_state,
};

static struct iio_info smi330_info = {
	.read_avail = smi330_read_avail,
	.read_raw = smi330_read_raw,
	.write_raw = smi330_write_raw,
	.write_raw_get_fmt = smi330_write_raw_get_fmt,
};

static int smi330_dev_init(struct smi330_data *data)
{
	int ret, chip_id, val, mode;
	struct device *dev = regmap_get_device(data->regmap);

	ret = regmap_read(data->regmap, SMI330_CHIP_ID_REG, &chip_id);
	if (ret)
		return ret;

	chip_id = FIELD_GET(SMI330_CHIP_ID_MASK, chip_id);
	if (chip_id != SMI330_CHIP_ID)
		dev_info(dev, "Unknown chip id: 0x%04x\n", chip_id);

	ret = regmap_read(data->regmap, SMI330_ERR_REG, &val);
	if (ret)
		return ret;
	if (FIELD_GET(SMI330_ERR_FATAL_MASK, val))
		return -ENODEV;

	ret = regmap_read(data->regmap, SMI330_STATUS_REG, &val);
	if (ret)
		return ret;
	if (FIELD_GET(SMI330_STATUS_POR_MASK, val) == 0)
		return -ENODEV;

	mode = FIELD_PREP(SMI330_CFG_MODE_MASK, SMI330_MODE_NORMAL);

	ret = regmap_update_bits(data->regmap, SMI330_ACCEL_CFG_REG,
				 SMI330_CFG_MODE_MASK, mode);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, SMI330_GYRO_CFG_REG,
				  SMI330_CFG_MODE_MASK, mode);
}

int smi330_core_probe(struct device *dev, struct regmap *regmap)
{
	int ret;
	struct iio_dev *indio_dev;
	struct smi330_data *data;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;

	ret = smi330_soft_reset(data);
	if (ret)
		return dev_err_probe(dev, ret, "Soft reset failed\n");

	indio_dev->channels = smi330_channels;
	indio_dev->num_channels = ARRAY_SIZE(smi330_channels);
	indio_dev->available_scan_masks = smi330_avail_scan_masks;
	indio_dev->name = "smi330";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &smi330_info;

	data->cfg.op_mode = SMI330_POLLING;

	ret = smi330_dev_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "Init failed\n");

	ret = smi330_register_irq(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Register IRQ failed\n");

	if (data->cfg.data_irq != SMI330_INT_DISABLED) {
		data->trig = devm_iio_trigger_alloc(dev, "%s-drdy-trigger",
						    indio_dev->name);
		if (!data->trig)
			return -ENOMEM;

		data->trig->ops = &smi330_trigger_ops;
		iio_trigger_set_drvdata(data->trig, data);

		ret = devm_iio_trigger_register(dev, data->trig);
		if (ret)
			return dev_err_probe(dev, ret,
					     "IIO register trigger failed\n");

		/* Set default operation mode to data ready. */
		indio_dev->trig = iio_trigger_get(data->trig);
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      smi330_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "IIO buffer setup failed\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Register IIO device failed\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(smi330_core_probe, "IIO_SMI330");

MODULE_AUTHOR("Stefan Gutmann <stefan.gutmann@de.bosch.com>");
MODULE_AUTHOR("Roman Huber <roman.huber@de.bosch.com>");
MODULE_AUTHOR("Filip Andrei <Andrei.Filip@ro.bosch.com>");
MODULE_AUTHOR("Drimbarean Avram Andrei <Avram-Andrei.Drimbarean@ro.bosch.com>");
MODULE_DESCRIPTION("Bosch SMI330 IMU driver");
MODULE_LICENSE("Dual BSD/GPL");
