// SPDX-License-Identifier: GPL-2.0+
/*
 * MAX40080 Digital Current-Sense Amplifier driver
 *
 * Copyright 2026 Analog Devices, Inc.
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX40080.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/units.h>

#include <asm/byteorder.h>

#include <linux/iio/iio.h>

#define MAX40080_REG_CFG		0x00
#define  MAX40080_CFG_MODE_MSK		GENMASK(2, 0)
#define  MAX40080_CFG_PEC_EN_MSK	BIT(5)
#define  MAX40080_CFG_RANGE_MSK		BIT(6)
#define  MAX40080_CFG_FILTER_MSK	GENMASK(14, 12)

#define MAX40080_REG_FIFO_CFG		0x0A
#define  MAX40080_FIFO_CFG_STORE_IV_MSK	GENMASK(1, 0)

#define MAX40080_REG_IV			0x10
/* Current is a 13-bit two's-complement value (magnitude + sign bit). */
#define  MAX40080_IV_I_MSK		GENMASK(12, 0)
#define  MAX40080_IV_I_SIGN_BIT		12
#define  MAX40080_IV_V_MAG_MSK		GENMASK(27, 16)
#define  MAX40080_IV_VALID_MSK		BIT(31)

/* CFG.mode field values. */
#define MAX40080_CFG_MODE_STDBY		0x00
#define MAX40080_CFG_MODE_SINGLE	0x02

/* CFG.range field values. */
#define MAX40080_CFG_RANGE_50mV		0
#define MAX40080_CFG_RANGE_10mV		1

/* FIFO_CFG.store_iv field values. */
#define MAX40080_FIFO_CFG_STORE_IV	0x02

#define MAX40080_ADC_RES_BITS		12
#define MAX40080_INTER_VREF_mV		1250
#define MAX40080_V_BUFF_GAIN		30
#define MAX40080_CSA_50mV_GAIN		25
#define MAX40080_CSA_10mV_GAIN		125

/*
 * The RANGE field (CFG bit 6) selects one of two current-sense full-scale
 * ranges (the MAX40080 supports exactly two: +/-50 mV and +/-10 mV). Indexed
 * by the CFG.range field value.
 */
static const int max40080_csa_gain[] = {
	[MAX40080_CFG_RANGE_50mV] = MAX40080_CSA_50mV_GAIN,
	[MAX40080_CFG_RANGE_10mV] = MAX40080_CSA_10mV_GAIN,
};

struct max40080_state {
	struct i2c_client *client;
	/* Serializes read-modify-write access to the CFG register. */
	struct mutex lock;
	u32 shunt_resistor_uOhm;
	/* Cached configuration: the selected RANGE index and oversampling ratio. */
	unsigned int range;
	int oversampling_ratio;
	/*
	 * Precomputed current scale (mA per code) for each RANGE setting, as
	 * {integer, nano} pairs for IIO_VAL_INT_PLUS_NANO. The range is
	 * selected by writing the corresponding scale.
	 */
	int current_scale[2][2];
};

static const int max40080_oversampling_avail[] = { 1, 8, 16, 32, 64, 128 };

static int max40080_update_bits(struct max40080_state *st, u8 reg,
				u16 mask, u16 val)
{
	int tmp;

	tmp = i2c_smbus_read_word_data(st->client, reg);
	if (tmp < 0)
		return tmp;

	tmp = (tmp & ~mask) | (val & mask);

	return i2c_smbus_write_word_data(st->client, reg, tmp);
}

/*
 * In single-measurement mode the device sits idle until it receives an SMBus
 * Quick Command, then performs exactly one current and one voltage conversion
 * and returns to idle. Triggering on demand this way (rather than running the
 * FIFO continuously in active mode) means each read returns a fresh, coherent
 * current/voltage pair instead of the oldest queued FIFO entry.
 */
static int max40080_trigger_measurement(struct max40080_state *st)
{
	return i2c_smbus_xfer(st->client->adapter, st->client->addr,
			      st->client->flags, I2C_SMBUS_WRITE, 0,
			      I2C_SMBUS_QUICK, NULL);
}

/*
 * A single measurement holds the matched current/voltage pair in one 32-bit
 * word (MAX40080_REG_IV). Reading all four bytes in one transaction returns
 * both from the same conversion; reading the separate current (0x0C) and
 * voltage (0x0E) registers would decorrelate the two channels.
 *
 * Unlike the word accesses used elsewhere, this is a plain I2C block read: the
 * SMBus layer does not append or verify a PEC byte for it even when PEC is
 * otherwise enabled for the device, so this transfer is not PEC protected.
 */
static int max40080_read_iv_once(struct max40080_state *st, u32 *iv)
{
	__le32 buf = 0;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(st->client, MAX40080_REG_IV,
					    sizeof(buf), (u8 *)&buf);
	if (ret < 0)
		return ret;

	*iv = le32_to_cpu(buf);

	return 0;
}

static int max40080_read_iv(struct max40080_state *st, u32 *iv)
{
	u32 tmp = 0;
	int ret, io_ret;

	guard(mutex)(&st->lock);

	ret = max40080_trigger_measurement(st);
	if (ret < 0)
		return ret;

	/*
	 * Wait for the conversion to complete by polling the FIFO valid bit
	 * (or bail out on an I2C error). Polling the device's own status makes
	 * this independent of the actual conversion time, which varies with the
	 * oversampling ratio and the bus speed. The timeout is only a safety
	 * ceiling: the worst case is the maximum 128x averaging on both the
	 * current and voltage channels at the slowest 15 ksps base rate plus
	 * the inter-channel switching time, i.e. roughly 20 ms; 50 ms leaves
	 * ample margin.
	 */
	ret = read_poll_timeout(max40080_read_iv_once, io_ret,
				io_ret || (tmp & MAX40080_IV_VALID_MSK),
				500, 50 * USEC_PER_MSEC, false, st, &tmp);
	if (io_ret)
		return io_ret;

	/*
	 * Propagate the last-read value even on timeout so the caller can
	 * inspect it for debugging.
	 */
	*iv = tmp;

	return ret;
}

static int max40080_get_current(struct max40080_state *st, int *val)
{
	u32 iv;
	int ret;

	ret = max40080_read_iv(st, &iv);
	if (ret)
		return ret;

	*val = sign_extend32(FIELD_GET(MAX40080_IV_I_MSK, iv),
			     MAX40080_IV_I_SIGN_BIT);

	return 0;
}

static int max40080_get_voltage(struct max40080_state *st, int *val)
{
	u32 iv;
	int ret;

	ret = max40080_read_iv(st, &iv);
	if (ret)
		return ret;

	*val = FIELD_GET(MAX40080_IV_V_MAG_MSK, iv);

	return 0;
}

static int max40080_set_range(struct max40080_state *st, unsigned int range)
{
	int ret;

	ret = max40080_update_bits(st, MAX40080_REG_CFG, MAX40080_CFG_RANGE_MSK,
				   FIELD_PREP(MAX40080_CFG_RANGE_MSK, range));
	if (ret)
		return ret;

	WRITE_ONCE(st->range, range);

	return 0;
}

/*
 * Precompute the current scale (mA per code) for each RANGE setting as
 * {integer, nano} pairs. The shunt drop for a full-scale code is
 *   Vref[mV] / (BIT(ADC_RES_BITS) * gain)
 * and current = Vshunt / Rshunt, so with Rshunt in micro-ohms the scale in
 * mA/code is
 *   Vref[mV] * NANO * MICRO / (BIT(ADC_RES_BITS) * gain * Rshunt[uOhm])
 * expressed as an integer part plus a nano fractional part.
 */
static void max40080_calc_current_scale(struct max40080_state *st)
{
	u64 numerator, denominator;
	u32 rem;

	for (unsigned int i = 0; i < ARRAY_SIZE(max40080_csa_gain); i++) {
		numerator = (u64)MAX40080_INTER_VREF_mV * NANO * MICRO;
		denominator = BIT_ULL(MAX40080_ADC_RES_BITS) * max40080_csa_gain[i] *
			      st->shunt_resistor_uOhm;
		numerator = div64_u64(numerator, denominator);
		st->current_scale[i][0] = div_u64_rem(numerator, NANO, &rem);
		st->current_scale[i][1] = rem;
	}
}

/*
 * max40080_oversampling_avail[] is ordered so that its index is the FILTER
 * field value (index 0 = no averaging, index 1 = 8x, ...). Return that index
 * for an exact match, or -EINVAL for a value that is not on the list.
 */
static int max40080_oversampling_to_filter(int val)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(max40080_oversampling_avail); i++) {
		if (max40080_oversampling_avail[i] == val)
			return i;
	}

	return -EINVAL;
}

static int max40080_set_oversampling_ratio(struct max40080_state *st, int val)
{
	int filter;
	int ret;

	filter = max40080_oversampling_to_filter(val);
	if (filter < 0)
		return filter;

	ret = max40080_update_bits(st, MAX40080_REG_CFG, MAX40080_CFG_FILTER_MSK,
				   FIELD_PREP(MAX40080_CFG_FILTER_MSK, filter));
	if (ret)
		return ret;

	WRITE_ONCE(st->oversampling_ratio, val);

	return 0;
}

static int max40080_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max40080_state *st = iio_priv(indio_dev);
	unsigned int range;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_CURRENT) {
			ret = max40080_get_current(st, val);
			if (ret)
				return ret;
		} else if (chan->type == IIO_VOLTAGE) {
			ret = max40080_get_voltage(st, val);
			if (ret)
				return ret;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_CURRENT) {
			/*
			 * The selectable current-sense range is exposed through
			 * scale: each RANGE setting has its own precomputed
			 * mA-per-code value. Userspace picks the range by
			 * writing the matching scale.
			 *
			 * Use READ_ONCE to ensure the compiler reads st->range
			 * exactly once, so val and val2 come from the same
			 * setting even if a concurrent write changes st->range.
			 */
			range = READ_ONCE(st->range);
			*val = st->current_scale[range][0];
			*val2 = st->current_scale[range][1];
			return IIO_VAL_INT_PLUS_NANO;
		}
		/* voltage[mV] = raw * Vref[mV] * buffer_gain / BIT(ADC_RES_BITS) */
		*val = MAX40080_INTER_VREF_mV * MAX40080_V_BUFF_GAIN;
		*val2 = MAX40080_ADC_RES_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = READ_ONCE(st->oversampling_ratio);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int max40080_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct max40080_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		/* Only the current channel has a selectable range/scale. */
		if (chan->type != IIO_CURRENT)
			return -EINVAL;

		for (unsigned int i = 0; i < ARRAY_SIZE(max40080_csa_gain); i++) {
			if (val == st->current_scale[i][0] &&
			    val2 == st->current_scale[i][1])
				return max40080_set_range(st, i);
		}

		return -EINVAL;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return max40080_set_oversampling_ratio(st, val);
	default:
		return -EINVAL;
	}
}

static int max40080_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT;
	}
}

static int max40080_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long info)
{
	struct max40080_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_CURRENT)
			return -EINVAL;

		*vals = (int *)st->current_scale;
		*length = ARRAY_SIZE(max40080_csa_gain) * 2;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = max40080_oversampling_avail;
		*length = ARRAY_SIZE(max40080_oversampling_avail);
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int max40080_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			       unsigned int write_val, unsigned int *read_val)
{
	struct max40080_state *st = iio_priv(indio_dev);
	int val;

	if (read_val) {
		val = i2c_smbus_read_word_data(st->client, reg);
		if (val < 0)
			return val;

		*read_val = val;

		return 0;
	}

	return i2c_smbus_write_word_data(st->client, reg, write_val);
}

static const struct iio_info max40080_info = {
	.read_raw = max40080_read_raw,
	.write_raw = max40080_write_raw,
	.write_raw_get_fmt = max40080_write_raw_get_fmt,
	.read_avail = max40080_read_avail,
	.debugfs_reg_access = &max40080_reg_access,
};

static const struct iio_chan_spec max40080_channels[] = {
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
};

/*
 * Configure the device from the cached state. The device powers up in standby
 * with PEC enabled (CFG POR = 0x0060), so PEC is kept enabled throughout.
 */
static int max40080_init(struct max40080_state *st)
{
	u16 fifo_cfg, cfg;
	int ret, filter;

	filter = max40080_oversampling_to_filter(st->oversampling_ratio);
	if (filter < 0)
		return filter;

	/*
	 * Put the device in standby before (re)configuring the FIFO: the FIFO
	 * configuration register can only be written while the device is not
	 * converting.
	 */
	cfg = FIELD_PREP(MAX40080_CFG_MODE_MSK, MAX40080_CFG_MODE_STDBY) |
	      FIELD_PREP(MAX40080_CFG_PEC_EN_MSK, 1);
	ret = i2c_smbus_write_word_data(st->client, MAX40080_REG_CFG, cfg);
	if (ret)
		return ret;

	/* Store a matched current+voltage pair per conversion. */
	fifo_cfg = FIELD_PREP(MAX40080_FIFO_CFG_STORE_IV_MSK, MAX40080_FIFO_CFG_STORE_IV);
	ret = i2c_smbus_write_word_data(st->client, MAX40080_REG_FIFO_CFG,
					fifo_cfg);
	if (ret)
		return ret;

	/*
	 * Use single-measurement mode: the device stays idle and converts once
	 * per SMBus Quick Command (see max40080_trigger_measurement()), so each
	 * read returns a fresh sample rather than a queued FIFO entry.
	 */
	cfg = FIELD_PREP(MAX40080_CFG_MODE_MSK, MAX40080_CFG_MODE_SINGLE) |
	      FIELD_PREP(MAX40080_CFG_PEC_EN_MSK, 1) |
	      FIELD_PREP(MAX40080_CFG_RANGE_MSK, st->range) |
	      FIELD_PREP(MAX40080_CFG_FILTER_MSK, filter);

	return i2c_smbus_write_word_data(st->client, MAX40080_REG_CFG, cfg);
}

static int max40080_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const char *propname;
	struct iio_dev *indio_dev;
	struct max40080_state *st;
	int ret;

	/*
	 * The device powers up with PEC enabled (CFG POR = 0x0060) and rejects
	 * unprotected transactions, so PEC support is mandatory, along with
	 * word access, the I2C block read used for the current/voltage pair,
	 * and the Quick Command used to trigger a conversion.
	 */
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK |
				     I2C_FUNC_SMBUS_QUICK |
				     I2C_FUNC_SMBUS_PEC))
		return -EOPNOTSUPP;

	client->flags |= I2C_CLIENT_PEC;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->client = client;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	propname = "shunt-resistor-micro-ohms";
	ret = device_property_read_u32(dev, propname, &st->shunt_resistor_uOhm);
	if (ret)
		return dev_err_probe(dev, ret, "can't read %s\n", propname);
	if (!st->shunt_resistor_uOhm)
		return dev_err_probe(dev, -EINVAL, "%s must be non-zero\n", propname);

	max40080_calc_current_scale(st);

	/* Defaults: 50 mV range, no averaging. */
	st->range = MAX40080_CFG_RANGE_50mV;
	st->oversampling_ratio = 1;

	indio_dev->name = "max40080";
	indio_dev->info = &max40080_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max40080_channels;
	indio_dev->num_channels = ARRAY_SIZE(max40080_channels);

	ret = max40080_init(st);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id max40080_i2c_ids[] = {
	{ .name = "max40080" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max40080_i2c_ids);

static const struct of_device_id max40080_of_match[] = {
	{ .compatible = "adi,max40080" },
	{ }
};
MODULE_DEVICE_TABLE(of, max40080_of_match);

static struct i2c_driver max40080_driver = {
	.driver = {
		.name = "max40080",
		.of_match_table = max40080_of_match,
	},
	.probe = max40080_probe,
	.id_table = max40080_i2c_ids,
};
module_i2c_driver(max40080_driver);

MODULE_AUTHOR("Ciprian Hegbeli <ciprian.hegbeli@analog.com>");
MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices MAX40080 current-sense amplifier driver");
MODULE_LICENSE("GPL");
