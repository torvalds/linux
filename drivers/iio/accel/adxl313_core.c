// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL313 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Lucas Stankus <lucas.p.stankus@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL313.pdf
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#include "adxl313.h"

#define ADXL313_INT_NONE			U8_MAX
#define ADXL313_INT1				1
#define ADXL313_INT2				2

#define ADXL313_REG_XYZ_BASE			ADXL313_REG_DATA_AXIS(0)

static const struct regmap_range adxl312_readable_reg_range[] = {
	regmap_reg_range(ADXL313_REG_DEVID0, ADXL313_REG_DEVID0),
	regmap_reg_range(ADXL313_REG_OFS_AXIS(0), ADXL313_REG_OFS_AXIS(2)),
	regmap_reg_range(ADXL313_REG_THRESH_ACT, ADXL313_REG_ACT_INACT_CTL),
	regmap_reg_range(ADXL313_REG_BW_RATE, ADXL313_REG_FIFO_STATUS),
};

static const struct regmap_range adxl313_readable_reg_range[] = {
	regmap_reg_range(ADXL313_REG_DEVID0, ADXL313_REG_XID),
	regmap_reg_range(ADXL313_REG_SOFT_RESET, ADXL313_REG_SOFT_RESET),
	regmap_reg_range(ADXL313_REG_OFS_AXIS(0), ADXL313_REG_OFS_AXIS(2)),
	regmap_reg_range(ADXL313_REG_THRESH_ACT, ADXL313_REG_ACT_INACT_CTL),
	regmap_reg_range(ADXL313_REG_BW_RATE, ADXL313_REG_FIFO_STATUS),
};

const struct regmap_access_table adxl312_readable_regs_table = {
	.yes_ranges = adxl312_readable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl312_readable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl312_readable_regs_table, "IIO_ADXL313");

const struct regmap_access_table adxl313_readable_regs_table = {
	.yes_ranges = adxl313_readable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl313_readable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl313_readable_regs_table, "IIO_ADXL313");

const struct regmap_access_table adxl314_readable_regs_table = {
	.yes_ranges = adxl312_readable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl312_readable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl314_readable_regs_table, "IIO_ADXL313");

bool adxl313_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADXL313_REG_DATA_AXIS(0):
	case ADXL313_REG_DATA_AXIS(1):
	case ADXL313_REG_DATA_AXIS(2):
	case ADXL313_REG_DATA_AXIS(3):
	case ADXL313_REG_DATA_AXIS(4):
	case ADXL313_REG_DATA_AXIS(5):
	case ADXL313_REG_FIFO_STATUS:
	case ADXL313_REG_INT_SOURCE:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(adxl313_is_volatile_reg, "IIO_ADXL313");

static int adxl313_set_measure_en(struct adxl313_data *data, bool en)
{
	return regmap_assign_bits(data->regmap, ADXL313_REG_POWER_CTL,
				  ADXL313_POWER_CTL_MSK, en);
}

static int adxl312_check_id(struct device *dev,
			    struct adxl313_data *data)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, ADXL313_REG_DEVID0, &regval);
	if (ret)
		return ret;

	if (regval != ADXL313_DEVID0_ADXL312_314)
		dev_warn(dev, "Invalid manufacturer ID: %#02x\n", regval);

	return 0;
}

static int adxl313_check_id(struct device *dev,
			    struct adxl313_data *data)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, ADXL313_REG_DEVID0, &regval);
	if (ret)
		return ret;

	if (regval != ADXL313_DEVID0)
		dev_warn(dev, "Invalid manufacturer ID: 0x%02x\n", regval);

	/* Check DEVID1 and PARTID */
	if (regval == ADXL313_DEVID0) {
		ret = regmap_read(data->regmap, ADXL313_REG_DEVID1, &regval);
		if (ret)
			return ret;

		if (regval != ADXL313_DEVID1)
			dev_warn(dev, "Invalid mems ID: 0x%02x\n", regval);

		ret = regmap_read(data->regmap, ADXL313_REG_PARTID, &regval);
		if (ret)
			return ret;

		if (regval != ADXL313_PARTID)
			dev_warn(dev, "Invalid device ID: 0x%02x\n", regval);
	}

	return 0;
}

const struct adxl313_chip_info adxl31x_chip_info[] = {
	[ADXL312] = {
		.name = "adxl312",
		.type = ADXL312,
		.scale_factor = 28425072,
		.variable_range = true,
		.soft_reset = false,
		.check_id = &adxl312_check_id,
	},
	[ADXL313] = {
		.name = "adxl313",
		.type = ADXL313,
		.scale_factor = 9576806,
		.variable_range = true,
		.soft_reset = true,
		.check_id = &adxl313_check_id,
	},
	[ADXL314] = {
		.name = "adxl314",
		.type = ADXL314,
		.scale_factor = 478858719,
		.variable_range = false,
		.soft_reset = false,
		.check_id = &adxl312_check_id,
	},
};
EXPORT_SYMBOL_NS_GPL(adxl31x_chip_info, "IIO_ADXL313");

static const struct regmap_range adxl312_writable_reg_range[] = {
	regmap_reg_range(ADXL313_REG_OFS_AXIS(0), ADXL313_REG_OFS_AXIS(2)),
	regmap_reg_range(ADXL313_REG_THRESH_ACT, ADXL313_REG_ACT_INACT_CTL),
	regmap_reg_range(ADXL313_REG_BW_RATE, ADXL313_REG_INT_MAP),
	regmap_reg_range(ADXL313_REG_DATA_FORMAT, ADXL313_REG_DATA_FORMAT),
	regmap_reg_range(ADXL313_REG_FIFO_CTL, ADXL313_REG_FIFO_CTL),
};

static const struct regmap_range adxl313_writable_reg_range[] = {
	regmap_reg_range(ADXL313_REG_SOFT_RESET, ADXL313_REG_SOFT_RESET),
	regmap_reg_range(ADXL313_REG_OFS_AXIS(0), ADXL313_REG_OFS_AXIS(2)),
	regmap_reg_range(ADXL313_REG_THRESH_ACT, ADXL313_REG_ACT_INACT_CTL),
	regmap_reg_range(ADXL313_REG_BW_RATE, ADXL313_REG_INT_MAP),
	regmap_reg_range(ADXL313_REG_DATA_FORMAT, ADXL313_REG_DATA_FORMAT),
	regmap_reg_range(ADXL313_REG_FIFO_CTL, ADXL313_REG_FIFO_CTL),
};

const struct regmap_access_table adxl312_writable_regs_table = {
	.yes_ranges = adxl312_writable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl312_writable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl312_writable_regs_table, "IIO_ADXL313");

const struct regmap_access_table adxl313_writable_regs_table = {
	.yes_ranges = adxl313_writable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl313_writable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl313_writable_regs_table, "IIO_ADXL313");

const struct regmap_access_table adxl314_writable_regs_table = {
	.yes_ranges = adxl312_writable_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl312_writable_reg_range),
};
EXPORT_SYMBOL_NS_GPL(adxl314_writable_regs_table, "IIO_ADXL313");

static const int adxl313_odr_freqs[][2] = {
	[0] = { 6, 250000 },
	[1] = { 12, 500000 },
	[2] = { 25, 0 },
	[3] = { 50, 0 },
	[4] = { 100, 0 },
	[5] = { 200, 0 },
	[6] = { 400, 0 },
	[7] = { 800, 0 },
	[8] = { 1600, 0 },
	[9] = { 3200, 0 },
};

#define ADXL313_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.scan_index = (index),						\
	.address = (reg),						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_CALIBBIAS),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				    BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_type_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 13,						\
		.storagebits = 16,					\
		.endianness = IIO_BE,					\
	},								\
}

enum adxl313_chans {
	chan_x, chan_y, chan_z,
};

static const struct iio_chan_spec adxl313_channels[] = {
	ADXL313_ACCEL_CHANNEL(0, chan_x, X),
	ADXL313_ACCEL_CHANNEL(1, chan_y, Y),
	ADXL313_ACCEL_CHANNEL(2, chan_z, Z),
};

static const unsigned long adxl313_scan_masks[] = {
	BIT(chan_x) | BIT(chan_y) | BIT(chan_z),
	0
};

static int adxl313_set_odr(struct adxl313_data *data,
			   unsigned int freq1, unsigned int freq2)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(adxl313_odr_freqs); i++) {
		if (adxl313_odr_freqs[i][0] == freq1 &&
		    adxl313_odr_freqs[i][1] == freq2)
			break;
	}

	if (i == ARRAY_SIZE(adxl313_odr_freqs))
		return -EINVAL;

	return regmap_update_bits(data->regmap, ADXL313_REG_BW_RATE,
				  ADXL313_RATE_MSK,
				  FIELD_PREP(ADXL313_RATE_MSK, ADXL313_RATE_BASE + i));
}

static int adxl313_read_axis(struct adxl313_data *data,
			     struct iio_chan_spec const *chan)
{
	int ret;

	mutex_lock(&data->lock);

	ret = regmap_bulk_read(data->regmap,
			       ADXL313_REG_DATA_AXIS(chan->address),
			       &data->transf_buf, sizeof(data->transf_buf));
	if (ret)
		goto unlock_ret;

	ret = le16_to_cpu(data->transf_buf);

unlock_ret:
	mutex_unlock(&data->lock);
	return ret;
}

static int adxl313_read_freq_avail(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   const int **vals, int *type, int *length,
				   long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)adxl313_odr_freqs;
		*length = ARRAY_SIZE(adxl313_odr_freqs) * 2;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int adxl313_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct adxl313_data *data = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = adxl313_read_axis(data, chan);
		if (ret < 0)
			return ret;

		*val = sign_extend32(ret, chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;

		*val2 = data->chip_info->scale_factor;

		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = regmap_read(data->regmap,
				  ADXL313_REG_OFS_AXIS(chan->address), &regval);
		if (ret)
			return ret;

		/*
		 * 8-bit resolution at minimum range, that is 4x accel data scale
		 * factor at full resolution
		 */
		*val = sign_extend32(regval, 7) * 4;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(data->regmap, ADXL313_REG_BW_RATE, &regval);
		if (ret)
			return ret;

		ret = FIELD_GET(ADXL313_RATE_MSK, regval) - ADXL313_RATE_BASE;
		*val = adxl313_odr_freqs[ret][0];
		*val2 = adxl313_odr_freqs[ret][1];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int adxl313_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct adxl313_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		/*
		 * 8-bit resolution at minimum range, that is 4x accel data scale
		 * factor at full resolution
		 */
		if (clamp_val(val, -128 * 4, 127 * 4) != val)
			return -EINVAL;

		return regmap_write(data->regmap,
				    ADXL313_REG_OFS_AXIS(chan->address),
				    val / 4);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return adxl313_set_odr(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int adxl313_set_watermark(struct iio_dev *indio_dev, unsigned int value)
{
	struct adxl313_data *data = iio_priv(indio_dev);
	int ret;

	value = min(value, ADXL313_FIFO_SIZE - 1);

	ret = adxl313_set_measure_en(data, false);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, ADXL313_REG_FIFO_CTL,
				 ADXL313_REG_FIFO_CTL_MODE_MSK, value);
	if (ret)
		return ret;

	data->watermark = value;

	ret = regmap_set_bits(data->regmap, ADXL313_REG_INT_ENABLE,
			      ADXL313_INT_WATERMARK);
	if (ret)
		return ret;

	return adxl313_set_measure_en(data, true);
}

static int adxl313_get_samples(struct adxl313_data *data)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, ADXL313_REG_FIFO_STATUS, &regval);
	if (ret)
		return ret;

	return FIELD_GET(ADXL313_REG_FIFO_STATUS_ENTRIES_MSK, regval);
}

static int adxl313_fifo_transfer(struct adxl313_data *data, int samples)
{
	unsigned int i;
	int ret;

	for (i = 0; i < samples; i++) {
		ret = regmap_bulk_read(data->regmap, ADXL313_REG_XYZ_BASE,
				       data->fifo_buf + (i * ADXL313_NUM_AXIS),
				       sizeof(data->fifo_buf[0]) * ADXL313_NUM_AXIS);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * adxl313_fifo_reset() - Reset the FIFO and interrupt status registers.
 * @data: The device data.
 *
 * Reset the FIFO status registers. Reading out status registers clears the
 * FIFO and interrupt configuration. Thus do not evaluate regmap return values.
 * Ignore particular read register content. Register content is not processed
 * any further. Therefore the function returns void.
 */
static void adxl313_fifo_reset(struct adxl313_data *data)
{
	unsigned int regval;
	int samples;

	adxl313_set_measure_en(data, false);

	samples = adxl313_get_samples(data);
	if (samples > 0)
		adxl313_fifo_transfer(data, samples);

	regmap_read(data->regmap, ADXL313_REG_INT_SOURCE, &regval);

	adxl313_set_measure_en(data, true);
}

static int adxl313_buffer_postenable(struct iio_dev *indio_dev)
{
	struct adxl313_data *data = iio_priv(indio_dev);
	int ret;

	/* Set FIFO modes with measurement turned off, according to datasheet */
	ret = adxl313_set_measure_en(data, false);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, ADXL313_REG_FIFO_CTL,
			   FIELD_PREP(ADXL313_REG_FIFO_CTL_SAMPLES_MSK,	data->watermark) |
			   FIELD_PREP(ADXL313_REG_FIFO_CTL_MODE_MSK, ADXL313_FIFO_STREAM));
	if (ret)
		return ret;

	return adxl313_set_measure_en(data, true);
}

static int adxl313_buffer_predisable(struct iio_dev *indio_dev)
{
	struct adxl313_data *data = iio_priv(indio_dev);
	int ret;

	ret = adxl313_set_measure_en(data, false);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, ADXL313_REG_FIFO_CTL,
			   FIELD_PREP(ADXL313_REG_FIFO_CTL_MODE_MSK, ADXL313_FIFO_BYPASS));

	ret = regmap_write(data->regmap, ADXL313_REG_INT_ENABLE, 0);
	if (ret)
		return ret;

	return adxl313_set_measure_en(data, true);
}

static const struct iio_buffer_setup_ops adxl313_buffer_ops = {
	.postenable = adxl313_buffer_postenable,
	.predisable = adxl313_buffer_predisable,
};

static int adxl313_fifo_push(struct iio_dev *indio_dev, int samples)
{
	struct adxl313_data *data = iio_priv(indio_dev);
	unsigned int i;
	int ret;

	ret = adxl313_fifo_transfer(data, samples);
	if (ret)
		return ret;

	for (i = 0; i < ADXL313_NUM_AXIS * samples; i += ADXL313_NUM_AXIS)
		iio_push_to_buffers(indio_dev, &data->fifo_buf[i]);

	return 0;
}

static irqreturn_t adxl313_irq_handler(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct adxl313_data *data = iio_priv(indio_dev);
	int samples, int_stat;

	if (regmap_read(data->regmap, ADXL313_REG_INT_SOURCE, &int_stat))
		return IRQ_NONE;

	if (FIELD_GET(ADXL313_INT_WATERMARK, int_stat)) {
		samples = adxl313_get_samples(data);
		if (samples < 0)
			goto err_reset_fifo;

		if (adxl313_fifo_push(indio_dev, samples))
			goto err_reset_fifo;
	}

	if (FIELD_GET(ADXL313_INT_OVERRUN, int_stat))
		goto err_reset_fifo;

	return IRQ_HANDLED;

err_reset_fifo:
	adxl313_fifo_reset(data);

	return IRQ_HANDLED;
}

static int adxl313_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct adxl313_data *data = iio_priv(indio_dev);

	if (readval)
		return regmap_read(data->regmap, reg, readval);
	return regmap_write(data->regmap, reg, writeval);
}

static const struct iio_info adxl313_info = {
	.read_raw	= adxl313_read_raw,
	.write_raw	= adxl313_write_raw,
	.read_avail	= adxl313_read_freq_avail,
	.hwfifo_set_watermark = adxl313_set_watermark,
	.debugfs_reg_access = &adxl313_reg_access,
};

static int adxl313_setup(struct device *dev, struct adxl313_data *data,
			 int (*setup)(struct device *, struct regmap *))
{
	int ret;

	/*
	 * If sw reset available, ensures the device is in a consistent
	 * state after start up
	 */
	if (data->chip_info->soft_reset) {
		ret = regmap_write(data->regmap, ADXL313_REG_SOFT_RESET,
				   ADXL313_SOFT_RESET);
		if (ret)
			return ret;
	}

	if (setup) {
		ret = setup(dev, data->regmap);
		if (ret)
			return ret;
	}

	ret = data->chip_info->check_id(dev, data);
	if (ret)
		return ret;

	/* Sets the range to maximum, full resolution, if applicable */
	if (data->chip_info->variable_range) {
		ret = regmap_update_bits(data->regmap, ADXL313_REG_DATA_FORMAT,
					 ADXL313_RANGE_MSK,
					 FIELD_PREP(ADXL313_RANGE_MSK, ADXL313_RANGE_MAX));
		if (ret)
			return ret;

		/* Enables full resolution */
		ret = regmap_update_bits(data->regmap, ADXL313_REG_DATA_FORMAT,
					 ADXL313_FULL_RES, ADXL313_FULL_RES);
		if (ret)
			return ret;
	}

	/* Enables measurement mode */
	return adxl313_set_measure_en(data, true);
}

static unsigned int adxl313_get_int_type(struct device *dev, int *irq)
{
	*irq = fwnode_irq_get_byname(dev_fwnode(dev), "INT1");
	if (*irq > 0)
		return ADXL313_INT1;

	*irq = fwnode_irq_get_byname(dev_fwnode(dev), "INT2");
	if (*irq > 0)
		return ADXL313_INT2;

	return ADXL313_INT_NONE;
}

/**
 * adxl313_core_probe() - probe and setup for adxl313 accelerometer
 * @dev:	Driver model representation of the device
 * @regmap:	Register map of the device
 * @chip_info:	Structure containing device specific data
 * @setup:	Setup routine to be executed right before the standard device
 *		setup, can also be set to NULL if not required
 *
 * Return: 0 on success, negative errno on error cases
 */
int adxl313_core_probe(struct device *dev,
		       struct regmap *regmap,
		       const struct adxl313_chip_info *chip_info,
		       int (*setup)(struct device *, struct regmap *))
{
	struct adxl313_data *data;
	struct iio_dev *indio_dev;
	u8 int_line;
	u8 int_map_msk;
	int irq, ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;
	data->chip_info = chip_info;

	mutex_init(&data->lock);

	indio_dev->name = chip_info->name;
	indio_dev->info = &adxl313_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxl313_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl313_channels);
	indio_dev->available_scan_masks = adxl313_scan_masks;

	ret = adxl313_setup(dev, data, setup);
	if (ret) {
		dev_err(dev, "ADXL313 setup failed\n");
		return ret;
	}

	int_line = adxl313_get_int_type(dev, &irq);
	if (int_line == ADXL313_INT_NONE) {
		/*
		 * FIFO_BYPASSED mode
		 *
		 * When no interrupt lines are specified, the driver falls back
		 * to use the sensor in FIFO_BYPASS mode. This means turning off
		 * internal FIFO and interrupt generation (since there is no
		 * line specified). Unmaskable interrupts such as overrun or
		 * data ready won't interfere. Even that a FIFO_STREAM mode w/o
		 * connected interrupt line might allow for obtaining raw
		 * measurements, a fallback to disable interrupts when no
		 * interrupt lines are connected seems to be the cleaner
		 * solution.
		 */
		ret = regmap_write(data->regmap, ADXL313_REG_FIFO_CTL,
				   FIELD_PREP(ADXL313_REG_FIFO_CTL_MODE_MSK,
					      ADXL313_FIFO_BYPASS));
		if (ret)
			return ret;
	} else {
		/* FIFO_STREAM mode */
		int_map_msk = ADXL313_INT_DREADY | ADXL313_INT_ACTIVITY |
			ADXL313_INT_INACTIVITY | ADXL313_INT_WATERMARK |
			ADXL313_INT_OVERRUN;
		ret = regmap_assign_bits(data->regmap, ADXL313_REG_INT_MAP,
					 int_map_msk, int_line == ADXL313_INT2);
		if (ret)
			return ret;

		ret = devm_iio_kfifo_buffer_setup(dev, indio_dev,
						  &adxl313_buffer_ops);
		if (ret)
			return ret;

		ret = devm_request_threaded_irq(dev, irq, NULL,
						&adxl313_irq_handler,
						IRQF_SHARED | IRQF_ONESHOT,
						indio_dev->name, indio_dev);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(adxl313_core_probe, "IIO_ADXL313");

MODULE_AUTHOR("Lucas Stankus <lucas.p.stankus@gmail.com>");
MODULE_DESCRIPTION("ADXL313 3-Axis Digital Accelerometer core driver");
MODULE_LICENSE("GPL v2");
