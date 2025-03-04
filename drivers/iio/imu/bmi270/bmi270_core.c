// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "bmi270.h"

#define BMI270_CHIP_ID_REG				0x00

/* Checked to prevent sending incompatible firmware to BMI160 devices */
#define BMI160_CHIP_ID_VAL				0xD1

#define BMI260_CHIP_ID_VAL				0x27
#define BMI270_CHIP_ID_VAL				0x24
#define BMI270_CHIP_ID_MSK				GENMASK(7, 0)

#define BMI270_ACCEL_X_REG				0x0c
#define BMI270_ANG_VEL_X_REG				0x12

#define BMI270_INT_STATUS_1_REG				0x1d
#define BMI270_INT_STATUS_1_ACC_GYR_DRDY_MSK		GENMASK(7, 6)

#define BMI270_INTERNAL_STATUS_REG			0x21
#define BMI270_INTERNAL_STATUS_MSG_MSK			GENMASK(3, 0)
#define BMI270_INTERNAL_STATUS_MSG_INIT_OK		0x01
#define BMI270_INTERNAL_STATUS_AXES_REMAP_ERR_MSK	BIT(5)
#define BMI270_INTERNAL_STATUS_ODR_50HZ_ERR_MSK		BIT(6)

#define BMI270_TEMPERATURE_0_REG			0x22

#define BMI270_ACC_CONF_REG				0x40
#define BMI270_ACC_CONF_ODR_MSK				GENMASK(3, 0)
#define BMI270_ACC_CONF_ODR_100HZ			0x08
#define BMI270_ACC_CONF_BWP_MSK				GENMASK(6, 4)
#define BMI270_ACC_CONF_BWP_NORMAL_MODE			0x02
#define BMI270_ACC_CONF_FILTER_PERF_MSK			BIT(7)

#define BMI270_ACC_CONF_RANGE_REG			0x41
#define BMI270_ACC_CONF_RANGE_MSK			GENMASK(1, 0)

#define BMI270_GYR_CONF_REG				0x42
#define BMI270_GYR_CONF_ODR_MSK				GENMASK(3, 0)
#define BMI270_GYR_CONF_ODR_200HZ			0x09
#define BMI270_GYR_CONF_BWP_MSK				GENMASK(5, 4)
#define BMI270_GYR_CONF_BWP_NORMAL_MODE			0x02
#define BMI270_GYR_CONF_NOISE_PERF_MSK			BIT(6)
#define BMI270_GYR_CONF_FILTER_PERF_MSK			BIT(7)

#define BMI270_GYR_CONF_RANGE_REG			0x43
#define BMI270_GYR_CONF_RANGE_MSK			GENMASK(2, 0)

#define BMI270_INT1_IO_CTRL_REG				0x53
#define BMI270_INT2_IO_CTRL_REG				0x54
#define BMI270_INT_IO_CTRL_LVL_MSK			BIT(1)
#define BMI270_INT_IO_CTRL_OD_MSK			BIT(2)
#define BMI270_INT_IO_CTRL_OP_MSK			BIT(3)
#define BMI270_INT_IO_LVL_OD_OP_MSK			GENMASK(3, 1)

#define BMI270_INT_LATCH_REG				0x55
#define BMI270_INT_LATCH_REG_MSK			BIT(0)

#define BMI270_INT_MAP_DATA_REG				0x58
#define BMI270_INT_MAP_DATA_DRDY_INT1_MSK		BIT(2)
#define BMI270_INT_MAP_DATA_DRDY_INT2_MSK		BIT(6)

#define BMI270_INIT_CTRL_REG				0x59
#define BMI270_INIT_CTRL_LOAD_DONE_MSK			BIT(0)

#define BMI270_INIT_DATA_REG				0x5e

#define BMI270_PWR_CONF_REG				0x7c
#define BMI270_PWR_CONF_ADV_PWR_SAVE_MSK		BIT(0)
#define BMI270_PWR_CONF_FIFO_WKUP_MSK			BIT(1)
#define BMI270_PWR_CONF_FUP_EN_MSK			BIT(2)

#define BMI270_PWR_CTRL_REG				0x7d
#define BMI270_PWR_CTRL_AUX_EN_MSK			BIT(0)
#define BMI270_PWR_CTRL_GYR_EN_MSK			BIT(1)
#define BMI270_PWR_CTRL_ACCEL_EN_MSK			BIT(2)
#define BMI270_PWR_CTRL_TEMP_EN_MSK			BIT(3)

/* See datasheet section 4.6.14, Temperature Sensor */
#define BMI270_TEMP_OFFSET				11776
#define BMI270_TEMP_SCALE				1953125

#define BMI260_INIT_DATA_FILE "bmi260-init-data.fw"
#define BMI270_INIT_DATA_FILE "bmi270-init-data.fw"

enum bmi270_irq_pin {
	BMI270_IRQ_DISABLED,
	BMI270_IRQ_INT1,
	BMI270_IRQ_INT2,
};

struct bmi270_data {
	struct device *dev;
	struct regmap *regmap;
	const struct bmi270_chip_info *chip_info;
	enum bmi270_irq_pin irq_pin;
	struct iio_trigger *trig;
	 /* Protect device's private data from concurrent access */
	struct mutex mutex;

	/*
	 * Where IIO_DMA_MINALIGN may be larger than 8 bytes, align to
	 * that to ensure a DMA safe buffer.
	 */
	struct {
		__le16 channels[6];
		aligned_s64 timestamp;
	} buffer __aligned(IIO_DMA_MINALIGN);
};

enum bmi270_scan {
	BMI270_SCAN_ACCEL_X,
	BMI270_SCAN_ACCEL_Y,
	BMI270_SCAN_ACCEL_Z,
	BMI270_SCAN_GYRO_X,
	BMI270_SCAN_GYRO_Y,
	BMI270_SCAN_GYRO_Z,
	BMI270_SCAN_TIMESTAMP,
};

static const unsigned long bmi270_avail_scan_masks[] = {
	(BIT(BMI270_SCAN_ACCEL_X) |
	 BIT(BMI270_SCAN_ACCEL_Y) |
	 BIT(BMI270_SCAN_ACCEL_Z) |
	 BIT(BMI270_SCAN_GYRO_X) |
	 BIT(BMI270_SCAN_GYRO_Y) |
	 BIT(BMI270_SCAN_GYRO_Z)),
	0
};

const struct bmi270_chip_info bmi260_chip_info = {
	.name = "bmi260",
	.chip_id = BMI260_CHIP_ID_VAL,
	.fw_name = BMI260_INIT_DATA_FILE,
};
EXPORT_SYMBOL_NS_GPL(bmi260_chip_info, "IIO_BMI270");

const struct bmi270_chip_info bmi270_chip_info = {
	.name = "bmi270",
	.chip_id = BMI270_CHIP_ID_VAL,
	.fw_name = BMI270_INIT_DATA_FILE,
};
EXPORT_SYMBOL_NS_GPL(bmi270_chip_info, "IIO_BMI270");

enum bmi270_sensor_type {
	BMI270_ACCEL	= 0,
	BMI270_GYRO,
	BMI270_TEMP,
};

struct bmi270_scale {
	int scale;
	int uscale;
};

struct bmi270_odr {
	int odr;
	int uodr;
};

static const struct bmi270_scale bmi270_accel_scale[] = {
	{ 0, 598 },
	{ 0, 1197 },
	{ 0, 2394 },
	{ 0, 4788 },
};

static const struct bmi270_scale bmi270_gyro_scale[] = {
	{ 0, 1065 },
	{ 0, 532 },
	{ 0, 266 },
	{ 0, 133 },
	{ 0, 66 },
};

static const struct bmi270_scale bmi270_temp_scale[] = {
	{ BMI270_TEMP_SCALE / MICRO, BMI270_TEMP_SCALE % MICRO },
};

struct bmi270_scale_item {
	const struct bmi270_scale *tbl;
	int num;
};

static const struct bmi270_scale_item bmi270_scale_table[] = {
	[BMI270_ACCEL] = {
		.tbl	= bmi270_accel_scale,
		.num	= ARRAY_SIZE(bmi270_accel_scale),
	},
	[BMI270_GYRO] = {
		.tbl	= bmi270_gyro_scale,
		.num	= ARRAY_SIZE(bmi270_gyro_scale),
	},
	[BMI270_TEMP] = {
		.tbl	= bmi270_temp_scale,
		.num	= ARRAY_SIZE(bmi270_temp_scale),
	},
};

static const struct bmi270_odr bmi270_accel_odr[] = {
	{ 0, 781250 },
	{ 1, 562500 },
	{ 3, 125000 },
	{ 6, 250000 },
	{ 12, 500000 },
	{ 25, 0 },
	{ 50, 0 },
	{ 100, 0 },
	{ 200, 0 },
	{ 400, 0 },
	{ 800, 0 },
	{ 1600, 0 },
};

static const u8 bmi270_accel_odr_vals[] = {
	0x01,
	0x02,
	0x03,
	0x04,
	0x05,
	0x06,
	0x07,
	0x08,
	0x09,
	0x0A,
	0x0B,
	0x0C,
};

static const struct bmi270_odr bmi270_gyro_odr[] = {
	{ 25, 0 },
	{ 50, 0 },
	{ 100, 0 },
	{ 200, 0 },
	{ 400, 0 },
	{ 800, 0 },
	{ 1600, 0 },
	{ 3200, 0 },
};

static const u8 bmi270_gyro_odr_vals[] = {
	0x06,
	0x07,
	0x08,
	0x09,
	0x0A,
	0x0B,
	0x0C,
	0x0D,
};

struct bmi270_odr_item {
	const struct bmi270_odr *tbl;
	const u8 *vals;
	int num;
};

static const struct  bmi270_odr_item bmi270_odr_table[] = {
	[BMI270_ACCEL] = {
		.tbl	= bmi270_accel_odr,
		.vals   = bmi270_accel_odr_vals,
		.num	= ARRAY_SIZE(bmi270_accel_odr),
	},
	[BMI270_GYRO] = {
		.tbl	= bmi270_gyro_odr,
		.vals   = bmi270_gyro_odr_vals,
		.num	= ARRAY_SIZE(bmi270_gyro_odr),
	},
};

static int bmi270_set_scale(struct bmi270_data *data, int chan_type, int uscale)
{
	int i;
	int reg, mask;
	struct bmi270_scale_item bmi270_scale_item;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = BMI270_ACC_CONF_RANGE_REG;
		mask = BMI270_ACC_CONF_RANGE_MSK;
		bmi270_scale_item = bmi270_scale_table[BMI270_ACCEL];
		break;
	case IIO_ANGL_VEL:
		reg = BMI270_GYR_CONF_RANGE_REG;
		mask = BMI270_GYR_CONF_RANGE_MSK;
		bmi270_scale_item = bmi270_scale_table[BMI270_GYRO];
		break;
	default:
		return -EINVAL;
	}

	guard(mutex)(&data->mutex);

	for (i = 0; i < bmi270_scale_item.num; i++) {
		if (bmi270_scale_item.tbl[i].uscale != uscale)
			continue;

		return regmap_update_bits(data->regmap, reg, mask, i);
	}

	return -EINVAL;
}

static int bmi270_get_scale(struct bmi270_data *data, int chan_type, int *scale,
			    int *uscale)
{
	int ret;
	unsigned int val;
	struct bmi270_scale_item bmi270_scale_item;

	guard(mutex)(&data->mutex);

	switch (chan_type) {
	case IIO_ACCEL:
		ret = regmap_read(data->regmap, BMI270_ACC_CONF_RANGE_REG, &val);
		if (ret)
			return ret;

		val = FIELD_GET(BMI270_ACC_CONF_RANGE_MSK, val);
		bmi270_scale_item = bmi270_scale_table[BMI270_ACCEL];
		break;
	case IIO_ANGL_VEL:
		ret = regmap_read(data->regmap, BMI270_GYR_CONF_RANGE_REG, &val);
		if (ret)
			return ret;

		val = FIELD_GET(BMI270_GYR_CONF_RANGE_MSK, val);
		bmi270_scale_item = bmi270_scale_table[BMI270_GYRO];
		break;
	case IIO_TEMP:
		val = 0;
		bmi270_scale_item = bmi270_scale_table[BMI270_TEMP];
		break;
	default:
		return -EINVAL;
	}

	if (val >= bmi270_scale_item.num)
		return -EINVAL;

	*scale = bmi270_scale_item.tbl[val].scale;
	*uscale = bmi270_scale_item.tbl[val].uscale;
	return 0;
}

static int bmi270_set_odr(struct bmi270_data *data, int chan_type, int odr,
			  int uodr)
{
	int i;
	int reg, mask;
	struct bmi270_odr_item bmi270_odr_item;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = BMI270_ACC_CONF_REG;
		mask = BMI270_ACC_CONF_ODR_MSK;
		bmi270_odr_item = bmi270_odr_table[BMI270_ACCEL];
		break;
	case IIO_ANGL_VEL:
		reg = BMI270_GYR_CONF_REG;
		mask = BMI270_GYR_CONF_ODR_MSK;
		bmi270_odr_item = bmi270_odr_table[BMI270_GYRO];
		break;
	default:
		return -EINVAL;
	}

	guard(mutex)(&data->mutex);

	for (i = 0; i < bmi270_odr_item.num; i++) {
		if (bmi270_odr_item.tbl[i].odr != odr ||
		    bmi270_odr_item.tbl[i].uodr != uodr)
			continue;

		return regmap_update_bits(data->regmap, reg, mask,
					  bmi270_odr_item.vals[i]);
	}

	return -EINVAL;
}

static int bmi270_get_odr(struct bmi270_data *data, int chan_type, int *odr,
			  int *uodr)
{
	int i, val, ret;
	struct bmi270_odr_item bmi270_odr_item;

	guard(mutex)(&data->mutex);

	switch (chan_type) {
	case IIO_ACCEL:
		ret = regmap_read(data->regmap, BMI270_ACC_CONF_REG, &val);
		if (ret)
			return ret;

		val = FIELD_GET(BMI270_ACC_CONF_ODR_MSK, val);
		bmi270_odr_item = bmi270_odr_table[BMI270_ACCEL];
		break;
	case IIO_ANGL_VEL:
		ret = regmap_read(data->regmap, BMI270_GYR_CONF_REG, &val);
		if (ret)
			return ret;

		val = FIELD_GET(BMI270_GYR_CONF_ODR_MSK, val);
		bmi270_odr_item = bmi270_odr_table[BMI270_GYRO];
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < bmi270_odr_item.num; i++) {
		if (val != bmi270_odr_item.vals[i])
			continue;

		*odr = bmi270_odr_item.tbl[i].odr;
		*uodr = bmi270_odr_item.tbl[i].uodr;
		return 0;
	}

	return -EINVAL;
}

static irqreturn_t bmi270_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmi270_data *data = iio_priv(indio_dev);
	unsigned int status;
	int ret;

	scoped_guard(mutex, &data->mutex) {
		ret = regmap_read(data->regmap, BMI270_INT_STATUS_1_REG,
				  &status);
		if (ret)
			return IRQ_NONE;
	}

	if (FIELD_GET(BMI270_INT_STATUS_1_ACC_GYR_DRDY_MSK, status))
		iio_trigger_poll_nested(data->trig);

	return IRQ_HANDLED;
}

static int bmi270_data_rdy_trigger_set_state(struct iio_trigger *trig,
					     bool state)
{
	struct bmi270_data *data = iio_trigger_get_drvdata(trig);
	unsigned int field_value = 0;
	unsigned int mask;

	guard(mutex)(&data->mutex);

	switch (data->irq_pin) {
	case BMI270_IRQ_INT1:
		mask = BMI270_INT_MAP_DATA_DRDY_INT1_MSK;
		set_mask_bits(&field_value, BMI270_INT_MAP_DATA_DRDY_INT1_MSK,
			      FIELD_PREP(BMI270_INT_MAP_DATA_DRDY_INT1_MSK,
					 state));
		break;
	case BMI270_IRQ_INT2:
		mask = BMI270_INT_MAP_DATA_DRDY_INT2_MSK;
		set_mask_bits(&field_value, BMI270_INT_MAP_DATA_DRDY_INT2_MSK,
			      FIELD_PREP(BMI270_INT_MAP_DATA_DRDY_INT2_MSK,
					 state));
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(data->regmap, BMI270_INT_MAP_DATA_REG, mask,
				  field_value);
}

static const struct iio_trigger_ops bmi270_trigger_ops = {
	.set_trigger_state = &bmi270_data_rdy_trigger_set_state,
};

static irqreturn_t bmi270_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmi270_data *data = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&data->mutex);

	ret = regmap_bulk_read(data->regmap, BMI270_ACCEL_X_REG,
			       &data->buffer.channels,
			       sizeof(data->buffer.channels));

	if (ret)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
					   pf->timestamp);
done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int bmi270_get_data(struct bmi270_data *data, int chan_type, int axis,
			   int *val)
{
	__le16 sample;
	int reg;
	int ret;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = BMI270_ACCEL_X_REG + (axis - IIO_MOD_X) * 2;
		break;
	case IIO_ANGL_VEL:
		reg = BMI270_ANG_VEL_X_REG + (axis - IIO_MOD_X) * 2;
		break;
	case IIO_TEMP:
		reg = BMI270_TEMPERATURE_0_REG;
		break;
	default:
		return -EINVAL;
	}

	guard(mutex)(&data->mutex);

	ret = regmap_bulk_read(data->regmap, reg, &sample, sizeof(sample));
	if (ret)
		return ret;

	*val = sign_extend32(le16_to_cpu(sample), 15);

	return IIO_VAL_INT;
}

static int bmi270_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct bmi270_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = bmi270_get_data(data, chan->type, chan->channel2, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		ret = bmi270_get_scale(data, chan->type, val, val2);
		return ret ? ret : IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = BMI270_TEMP_OFFSET;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = bmi270_get_odr(data, chan->type, val, val2);
		return ret ? ret : IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int bmi270_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bmi270_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = bmi270_set_scale(data, chan->type, val2);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = bmi270_set_odr(data, chan->type, val, val2);
		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bmi270_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_MICRO;
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*vals = (const int *)bmi270_gyro_scale;
			*length = ARRAY_SIZE(bmi270_gyro_scale) * 2;
			return IIO_AVAIL_LIST;
		case IIO_ACCEL:
			*vals = (const int *)bmi270_accel_scale;
			*length = ARRAY_SIZE(bmi270_accel_scale) * 2;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*vals = (const int *)bmi270_gyro_odr;
			*length = ARRAY_SIZE(bmi270_gyro_odr) * 2;
			return IIO_AVAIL_LIST;
		case IIO_ACCEL:
			*vals = (const int *)bmi270_accel_odr;
			*length = ARRAY_SIZE(bmi270_accel_odr) * 2;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info bmi270_info = {
	.read_raw = bmi270_read_raw,
	.write_raw = bmi270_write_raw,
	.read_avail = bmi270_read_avail,
};

#define BMI270_ACCEL_CHANNEL(_axis) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_type_available =			\
		BIT(IIO_CHAN_INFO_SCALE) |			\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.scan_index = BMI270_SCAN_ACCEL_##_axis,		\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_LE,				\
	},	                                                \
}

#define BMI270_ANG_VEL_CHANNEL(_axis) {				\
	.type = IIO_ANGL_VEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_type_available =			\
		BIT(IIO_CHAN_INFO_SCALE) |			\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.scan_index = BMI270_SCAN_GYRO_##_axis,			\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_LE,				\
	},	                                                \
}

static const struct iio_chan_spec bmi270_channels[] = {
	BMI270_ACCEL_CHANNEL(X),
	BMI270_ACCEL_CHANNEL(Y),
	BMI270_ACCEL_CHANNEL(Z),
	BMI270_ANG_VEL_CHANNEL(X),
	BMI270_ANG_VEL_CHANNEL(Y),
	BMI270_ANG_VEL_CHANNEL(Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = -1, /* No buffer support */
	},
	IIO_CHAN_SOFT_TIMESTAMP(BMI270_SCAN_TIMESTAMP),
};

static int bmi270_int_pin_config(struct bmi270_data *data,
				 enum bmi270_irq_pin irq_pin,
				 bool active_high, bool open_drain, bool latch)
{
	unsigned int reg, field_value;
	int ret;

	ret = regmap_update_bits(data->regmap, BMI270_INT_LATCH_REG,
				 BMI270_INT_LATCH_REG_MSK,
				 FIELD_PREP(BMI270_INT_LATCH_REG_MSK, latch));
	if (ret)
		return ret;

	switch (irq_pin) {
	case BMI270_IRQ_INT1:
		reg = BMI270_INT1_IO_CTRL_REG;
		break;
	case BMI270_IRQ_INT2:
		reg = BMI270_INT2_IO_CTRL_REG;
		break;
	default:
		return -EINVAL;
	}

	field_value = FIELD_PREP(BMI270_INT_IO_CTRL_LVL_MSK, active_high) |
		      FIELD_PREP(BMI270_INT_IO_CTRL_OD_MSK, open_drain) |
		      FIELD_PREP(BMI270_INT_IO_CTRL_OP_MSK, 1);
	return regmap_update_bits(data->regmap, reg,
				  BMI270_INT_IO_LVL_OD_OP_MSK, field_value);
}

static int bmi270_trigger_probe(struct bmi270_data *data,
				struct iio_dev *indio_dev)
{
	bool open_drain, active_high, latch;
	struct fwnode_handle *fwnode;
	enum bmi270_irq_pin irq_pin;
	int ret, irq, irq_type;

	fwnode = dev_fwnode(data->dev);
	if (!fwnode)
		return -ENODEV;

	irq = fwnode_irq_get_byname(fwnode, "INT1");
	if (irq > 0) {
		irq_pin = BMI270_IRQ_INT1;
	} else {
		irq = fwnode_irq_get_byname(fwnode, "INT2");
		if (irq < 0)
			return 0;

		irq_pin = BMI270_IRQ_INT2;
	}

	irq_type = irq_get_trigger_type(irq);
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
		return dev_err_probe(data->dev, -EINVAL,
				     "Invalid interrupt type 0x%x specified\n",
				     irq_type);
	}

	open_drain = fwnode_property_read_bool(fwnode, "drive-open-drain");

	ret = bmi270_int_pin_config(data, irq_pin, active_high, open_drain,
				    latch);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Failed to configure irq line\n");

	data->trig = devm_iio_trigger_alloc(data->dev, "%s-trig-%d",
					    indio_dev->name, irq_pin);
	if (!data->trig)
		return -ENOMEM;

	data->trig->ops = &bmi270_trigger_ops;
	iio_trigger_set_drvdata(data->trig, data);

	ret = devm_request_threaded_irq(data->dev, irq, NULL,
					bmi270_irq_thread_handler,
					IRQF_ONESHOT, "bmi270-int", indio_dev);
	if (ret)
		return dev_err_probe(data->dev, ret, "Failed to request IRQ\n");

	ret = devm_iio_trigger_register(data->dev, data->trig);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Trigger registration failed\n");

	data->irq_pin = irq_pin;

	return 0;
}

static int bmi270_validate_chip_id(struct bmi270_data *data)
{
	int chip_id;
	int ret;
	struct device *dev = data->dev;
	struct regmap *regmap = data->regmap;

	ret = regmap_read(regmap, BMI270_CHIP_ID_REG, &chip_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read chip id");

	/*
	 * Some manufacturers use "BMI0160" for both the BMI160 and
	 * BMI260. If the device is actually a BMI160, the bmi160
	 * driver should handle it and this driver should not.
	 */
	if (chip_id == BMI160_CHIP_ID_VAL)
		return -ENODEV;

	if (chip_id != data->chip_info->chip_id)
		dev_info(dev, "Unexpected chip id 0x%x", chip_id);

	if (chip_id == bmi260_chip_info.chip_id)
		data->chip_info = &bmi260_chip_info;
	else if (chip_id == bmi270_chip_info.chip_id)
		data->chip_info = &bmi270_chip_info;

	return 0;
}

static int bmi270_write_calibration_data(struct bmi270_data *data)
{
	int ret;
	int status = 0;
	const struct firmware *init_data;
	struct device *dev = data->dev;
	struct regmap *regmap = data->regmap;

	ret = regmap_clear_bits(regmap, BMI270_PWR_CONF_REG,
				BMI270_PWR_CONF_ADV_PWR_SAVE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to write power configuration");

	/*
	 * After disabling advanced power save, all registers are accessible
	 * after a 450us delay. This delay is specified in table A of the
	 * datasheet.
	 */
	usleep_range(450, 1000);

	ret = regmap_clear_bits(regmap, BMI270_INIT_CTRL_REG,
				BMI270_INIT_CTRL_LOAD_DONE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to prepare device to load init data");

	ret = request_firmware(&init_data, data->chip_info->fw_name, dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to load init data file");

	ret = regmap_bulk_write(regmap, BMI270_INIT_DATA_REG,
				init_data->data, init_data->size);
	release_firmware(init_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write init data");

	ret = regmap_set_bits(regmap, BMI270_INIT_CTRL_REG,
			      BMI270_INIT_CTRL_LOAD_DONE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to stop device initialization");

	/*
	 * Wait at least 140ms for the device to complete configuration.
	 * This delay is specified in table C of the datasheet.
	 */
	usleep_range(140000, 160000);

	ret = regmap_read(regmap, BMI270_INTERNAL_STATUS_REG, &status);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read internal status");

	if (status != BMI270_INTERNAL_STATUS_MSG_INIT_OK)
		return dev_err_probe(dev, -ENODEV, "Device failed to initialize");

	return 0;
}

static int bmi270_configure_imu(struct bmi270_data *data)
{
	int ret;
	struct device *dev = data->dev;
	struct regmap *regmap = data->regmap;

	ret = regmap_set_bits(regmap, BMI270_PWR_CTRL_REG,
			      BMI270_PWR_CTRL_AUX_EN_MSK |
			      BMI270_PWR_CTRL_GYR_EN_MSK |
			      BMI270_PWR_CTRL_ACCEL_EN_MSK |
			      BMI270_PWR_CTRL_TEMP_EN_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable accelerometer and gyroscope");

	ret = regmap_set_bits(regmap, BMI270_ACC_CONF_REG,
			      FIELD_PREP(BMI270_ACC_CONF_ODR_MSK,
					 BMI270_ACC_CONF_ODR_100HZ) |
			      FIELD_PREP(BMI270_ACC_CONF_BWP_MSK,
					 BMI270_ACC_CONF_BWP_NORMAL_MODE));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure accelerometer");

	ret = regmap_set_bits(regmap, BMI270_GYR_CONF_REG,
			      FIELD_PREP(BMI270_GYR_CONF_ODR_MSK,
					 BMI270_GYR_CONF_ODR_200HZ) |
			      FIELD_PREP(BMI270_GYR_CONF_BWP_MSK,
					 BMI270_GYR_CONF_BWP_NORMAL_MODE));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure gyroscope");

	/* Enable FIFO_WKUP, Disable ADV_PWR_SAVE and FUP_EN */
	ret = regmap_write(regmap, BMI270_PWR_CONF_REG,
			   BMI270_PWR_CONF_FIFO_WKUP_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set power configuration");

	return 0;
}

static int bmi270_chip_init(struct bmi270_data *data)
{
	int ret;

	ret = bmi270_validate_chip_id(data);
	if (ret)
		return ret;

	ret = bmi270_write_calibration_data(data);
	if (ret)
		return ret;

	return bmi270_configure_imu(data);
}

int bmi270_core_probe(struct device *dev, struct regmap *regmap,
		      const struct bmi270_chip_info *chip_info)
{
	int ret;
	struct bmi270_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;
	data->regmap = regmap;
	data->chip_info = chip_info;
	data->irq_pin = BMI270_IRQ_DISABLED;
	mutex_init(&data->mutex);

	ret = bmi270_chip_init(data);
	if (ret)
		return ret;

	indio_dev->channels = bmi270_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmi270_channels);
	indio_dev->name = chip_info->name;
	indio_dev->available_scan_masks = bmi270_avail_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmi270_info;

	ret = bmi270_trigger_probe(data, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      bmi270_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(bmi270_core_probe, "IIO_BMI270");

MODULE_AUTHOR("Alex Lanzano");
MODULE_DESCRIPTION("BMI270 driver");
MODULE_LICENSE("GPL");
