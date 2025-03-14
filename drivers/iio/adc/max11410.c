// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX11410 SPI ADC driver
 *
 * Copyright 2022 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/unaligned.h>

#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define MAX11410_REG_CONV_START	0x01
#define		MAX11410_CONV_TYPE_SINGLE	0x00
#define		MAX11410_CONV_TYPE_CONTINUOUS	0x01
#define MAX11410_REG_CAL_START	0x03
#define		MAX11410_CAL_START_SELF		0x00
#define		MAX11410_CAL_START_PGA		0x01
#define MAX11410_REG_GPIO_CTRL(ch)		((ch) ? 0x05 : 0x04)
#define		MAX11410_GPIO_INTRB		0xC1
#define MAX11410_REG_FILTER	0x08
#define		MAX11410_FILTER_RATE_MASK	GENMASK(3, 0)
#define		MAX11410_FILTER_RATE_MAX	0x0F
#define		MAX11410_FILTER_LINEF_MASK	GENMASK(5, 4)
#define		MAX11410_FILTER_50HZ		BIT(5)
#define		MAX11410_FILTER_60HZ		BIT(4)
#define MAX11410_REG_CTRL	0x09
#define		MAX11410_CTRL_REFSEL_MASK	GENMASK(2, 0)
#define		MAX11410_CTRL_VREFN_BUF_BIT	BIT(3)
#define		MAX11410_CTRL_VREFP_BUF_BIT	BIT(4)
#define		MAX11410_CTRL_FORMAT_BIT	BIT(5)
#define		MAX11410_CTRL_UNIPOLAR_BIT	BIT(6)
#define MAX11410_REG_MUX_CTRL0	0x0B
#define MAX11410_REG_PGA	0x0E
#define		MAX11410_PGA_GAIN_MASK		GENMASK(2, 0)
#define		MAX11410_PGA_SIG_PATH_MASK	GENMASK(5, 4)
#define		MAX11410_PGA_SIG_PATH_BUFFERED	0x00
#define		MAX11410_PGA_SIG_PATH_BYPASS	0x01
#define		MAX11410_PGA_SIG_PATH_PGA	0x02
#define MAX11410_REG_DATA0	0x30
#define MAX11410_REG_STATUS	0x38
#define		MAX11410_STATUS_CONV_READY_BIT	BIT(0)
#define		MAX11410_STATUS_CAL_READY_BIT	BIT(2)

#define MAX11410_REFSEL_AVDD_AGND	0x03
#define MAX11410_REFSEL_MAX		0x06
#define MAX11410_SIG_PATH_MAX		0x02
#define MAX11410_CHANNEL_INDEX_MAX	0x0A
#define MAX11410_AINP_AVDD	0x0A
#define MAX11410_AINN_GND	0x0A

#define MAX11410_CONVERSION_TIMEOUT_MS	2000
#define MAX11410_CALIB_TIMEOUT_MS	2000

#define MAX11410_SCALE_AVAIL_SIZE	8

enum max11410_filter {
	MAX11410_FILTER_FIR5060,
	MAX11410_FILTER_FIR50,
	MAX11410_FILTER_FIR60,
	MAX11410_FILTER_SINC4,
};

static const u8 max11410_sampling_len[] = {
	[MAX11410_FILTER_FIR5060] = 5,
	[MAX11410_FILTER_FIR50] = 6,
	[MAX11410_FILTER_FIR60] = 6,
	[MAX11410_FILTER_SINC4] = 10,
};

static const int max11410_sampling_rates[4][10][2] = {
	[MAX11410_FILTER_FIR5060] = {
		{ 1, 100000 },
		{ 2, 100000 },
		{ 4, 200000 },
		{ 8, 400000 },
		{ 16, 800000 }
	},
	[MAX11410_FILTER_FIR50] = {
		{ 1, 300000 },
		{ 2, 700000 },
		{ 5, 300000 },
		{ 10, 700000 },
		{ 21, 300000 },
		{ 40 }
	},
	[MAX11410_FILTER_FIR60] = {
		{ 1, 300000 },
		{ 2, 700000 },
		{ 5, 300000 },
		{ 10, 700000 },
		{ 21, 300000 },
		{ 40 }
	},
	[MAX11410_FILTER_SINC4] = {
		{ 4 },
		{ 10 },
		{ 20 },
		{ 40 },
		{ 60 },
		{ 120 },
		{ 240 },
		{ 480 },
		{ 960 },
		{ 1920 }
	}
};

struct max11410_channel_config {
	u32 settling_time_us;
	u32 *scale_avail;
	u8 refsel;
	u8 sig_path;
	u8 gain;
	bool bipolar;
	bool buffered_vrefp;
	bool buffered_vrefn;
};

struct max11410_state {
	struct spi_device *spi_dev;
	struct iio_trigger *trig;
	struct completion completion;
	struct mutex lock; /* Prevent changing channel config during sampling */
	struct regmap *regmap;
	struct regulator *avdd;
	struct regulator *vrefp[3];
	struct regulator *vrefn[3];
	struct max11410_channel_config *channels;
	int irq;
	struct {
		u32 data __aligned(IIO_DMA_MINALIGN);
		aligned_s64 ts;
	} scan;
};

static const struct iio_chan_spec chanspec_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_OFFSET),
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		.sign = 's',
		.realbits = 24,
		.storagebits = 32,
		.endianness = IIO_LE,
	},
};

static unsigned int max11410_reg_size(unsigned int reg)
{
	/* Registers from 0x00 to 0x10 are 1 byte, the rest are 3 bytes long. */
	return reg <= 0x10 ? 1 : 3;
}

static int max11410_write_reg(struct max11410_state *st, unsigned int reg,
			      unsigned int val)
{
	/* This driver only needs to write 8-bit registers */
	if (max11410_reg_size(reg) != 1)
		return -EINVAL;

	return regmap_write(st->regmap, reg, val);
}

static int max11410_read_reg(struct max11410_state *st, unsigned int reg,
			     int *val)
{
	int ret;

	if (max11410_reg_size(reg) == 3) {
		ret = regmap_bulk_read(st->regmap, reg, &st->scan.data, 3);
		if (ret)
			return ret;

		*val = get_unaligned_be24(&st->scan.data);
		return 0;
	}

	return regmap_read(st->regmap, reg, val);
}

static struct regulator *max11410_get_vrefp(struct max11410_state *st,
					    u8 refsel)
{
	refsel = refsel % 4;
	if (refsel == 3)
		return st->avdd;

	return st->vrefp[refsel];
}

static struct regulator *max11410_get_vrefn(struct max11410_state *st,
					    u8 refsel)
{
	if (refsel > 2)
		return NULL;

	return st->vrefn[refsel];
}

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x39,
};

static ssize_t max11410_notch_en_show(struct device *dev,
				      struct device_attribute *devattr,
				      char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct max11410_state *state = iio_priv(indio_dev);
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(devattr);
	unsigned int val;
	int ret;

	ret = max11410_read_reg(state, MAX11410_REG_FILTER, &val);
	if (ret)
		return ret;

	switch (iio_attr->address) {
	case 0:
		val = !FIELD_GET(MAX11410_FILTER_50HZ, val);
		break;
	case 1:
		val = !FIELD_GET(MAX11410_FILTER_60HZ, val);
		break;
	case 2:
		val = FIELD_GET(MAX11410_FILTER_LINEF_MASK, val) == 3;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t max11410_notch_en_store(struct device *dev,
				       struct device_attribute *devattr,
				       const char *buf, size_t count)
{
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(devattr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct max11410_state *state = iio_priv(indio_dev);
	unsigned int filter_bits;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	switch (iio_attr->address) {
	case 0:
		filter_bits = MAX11410_FILTER_50HZ;
		break;
	case 1:
		filter_bits = MAX11410_FILTER_60HZ;
		break;
	case 2:
	default:
		filter_bits = MAX11410_FILTER_50HZ | MAX11410_FILTER_60HZ;
		enable = !enable;
		break;
	}

	if (enable)
		ret = regmap_clear_bits(state->regmap, MAX11410_REG_FILTER,
					filter_bits);
	else
		ret = regmap_set_bits(state->regmap, MAX11410_REG_FILTER,
				      filter_bits);

	if (ret)
		return ret;

	return count;
}

static ssize_t in_voltage_filter2_notch_center_show(struct device *dev,
						    struct device_attribute *devattr,
						    char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct max11410_state *state = iio_priv(indio_dev);
	int ret, reg, rate, filter;

	ret = regmap_read(state->regmap, MAX11410_REG_FILTER, &reg);
	if (ret)
		return ret;

	rate = FIELD_GET(MAX11410_FILTER_RATE_MASK, reg);
	rate = clamp_val(rate, 0,
			 max11410_sampling_len[MAX11410_FILTER_SINC4] - 1);
	filter = max11410_sampling_rates[MAX11410_FILTER_SINC4][rate][0];

	return sysfs_emit(buf, "%d\n", filter);
}

static IIO_CONST_ATTR(in_voltage_filter0_notch_center, "50");
static IIO_CONST_ATTR(in_voltage_filter1_notch_center, "60");
static IIO_DEVICE_ATTR_RO(in_voltage_filter2_notch_center, 2);

static IIO_DEVICE_ATTR(in_voltage_filter0_notch_en, 0644,
		       max11410_notch_en_show, max11410_notch_en_store, 0);
static IIO_DEVICE_ATTR(in_voltage_filter1_notch_en, 0644,
		       max11410_notch_en_show, max11410_notch_en_store, 1);
static IIO_DEVICE_ATTR(in_voltage_filter2_notch_en, 0644,
		       max11410_notch_en_show, max11410_notch_en_store, 2);

static struct attribute *max11410_attributes[] = {
	&iio_const_attr_in_voltage_filter0_notch_center.dev_attr.attr,
	&iio_const_attr_in_voltage_filter1_notch_center.dev_attr.attr,
	&iio_dev_attr_in_voltage_filter2_notch_center.dev_attr.attr,
	&iio_dev_attr_in_voltage_filter0_notch_en.dev_attr.attr,
	&iio_dev_attr_in_voltage_filter1_notch_en.dev_attr.attr,
	&iio_dev_attr_in_voltage_filter2_notch_en.dev_attr.attr,
	NULL
};

static const struct attribute_group max11410_attribute_group = {
	.attrs = max11410_attributes,
};

static int max11410_set_input_mux(struct max11410_state *st, u8 ainp, u8 ainn)
{
	if (ainp > MAX11410_CHANNEL_INDEX_MAX ||
	    ainn > MAX11410_CHANNEL_INDEX_MAX)
		return -EINVAL;

	return max11410_write_reg(st, MAX11410_REG_MUX_CTRL0,
				  (ainp << 4) | ainn);
}

static int max11410_configure_channel(struct max11410_state *st,
				      struct iio_chan_spec const *chan)
{
	struct max11410_channel_config cfg = st->channels[chan->address];
	unsigned int regval;
	int ret;

	if (chan->differential)
		ret = max11410_set_input_mux(st, chan->channel, chan->channel2);
	else
		ret = max11410_set_input_mux(st, chan->channel,
					     MAX11410_AINN_GND);

	if (ret)
		return ret;

	regval = FIELD_PREP(MAX11410_CTRL_VREFP_BUF_BIT, cfg.buffered_vrefp) |
		 FIELD_PREP(MAX11410_CTRL_VREFN_BUF_BIT, cfg.buffered_vrefn) |
		 FIELD_PREP(MAX11410_CTRL_REFSEL_MASK, cfg.refsel) |
		 FIELD_PREP(MAX11410_CTRL_UNIPOLAR_BIT, cfg.bipolar ? 0 : 1);
	ret = regmap_update_bits(st->regmap, MAX11410_REG_CTRL,
				 MAX11410_CTRL_REFSEL_MASK |
				 MAX11410_CTRL_VREFP_BUF_BIT |
				 MAX11410_CTRL_VREFN_BUF_BIT |
				 MAX11410_CTRL_UNIPOLAR_BIT, regval);
	if (ret)
		return ret;

	regval = FIELD_PREP(MAX11410_PGA_SIG_PATH_MASK, cfg.sig_path) |
		 FIELD_PREP(MAX11410_PGA_GAIN_MASK, cfg.gain);
	ret = regmap_write(st->regmap, MAX11410_REG_PGA, regval);
	if (ret)
		return ret;

	if (cfg.settling_time_us)
		fsleep(cfg.settling_time_us);

	return 0;
}

static int max11410_sample(struct max11410_state *st, int *sample_raw,
			   struct iio_chan_spec const *chan)
{
	int val, ret;

	ret = max11410_configure_channel(st, chan);
	if (ret)
		return ret;

	if (st->irq > 0)
		reinit_completion(&st->completion);

	/* Start Conversion */
	ret = max11410_write_reg(st, MAX11410_REG_CONV_START,
				 MAX11410_CONV_TYPE_SINGLE);
	if (ret)
		return ret;

	if (st->irq > 0) {
		/* Wait for an interrupt. */
		ret = wait_for_completion_timeout(&st->completion,
						  msecs_to_jiffies(MAX11410_CONVERSION_TIMEOUT_MS));
		if (!ret)
			return -ETIMEDOUT;
	} else {
		int ret2;

		/* Wait for status register Conversion Ready flag */
		ret = read_poll_timeout(max11410_read_reg, ret2,
					ret2 || (val & MAX11410_STATUS_CONV_READY_BIT),
					5000, MAX11410_CONVERSION_TIMEOUT_MS * 1000,
					true, st, MAX11410_REG_STATUS, &val);
		if (ret)
			return ret;
		if (ret2)
			return ret2;
	}

	/* Read ADC Data */
	return max11410_read_reg(st, MAX11410_REG_DATA0, sample_raw);
}

static int max11410_get_scale(struct max11410_state *state,
			      struct max11410_channel_config cfg)
{
	struct regulator *vrefp, *vrefn;
	int scale;

	vrefp = max11410_get_vrefp(state, cfg.refsel);

	scale = regulator_get_voltage(vrefp) / 1000;
	vrefn = max11410_get_vrefn(state, cfg.refsel);
	if (vrefn)
		scale -= regulator_get_voltage(vrefn) / 1000;

	if (cfg.bipolar)
		scale *= 2;

	return scale >> cfg.gain;
}

static int max11410_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long info)
{
	struct max11410_state *state = iio_priv(indio_dev);
	struct max11410_channel_config cfg = state->channels[chan->address];
	int ret, reg_val, filter, rate;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*val = max11410_get_scale(state, cfg);
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		if (cfg.bipolar)
			*val = -BIT(chan->scan_type.realbits - 1);
		else
			*val = 0;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		mutex_lock(&state->lock);

		ret = max11410_sample(state, &reg_val, chan);

		mutex_unlock(&state->lock);

		iio_device_release_direct(indio_dev);

		if (ret)
			return ret;

		*val = reg_val;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(state->regmap, MAX11410_REG_FILTER, &reg_val);
		if (ret)
			return ret;

		filter = FIELD_GET(MAX11410_FILTER_LINEF_MASK, reg_val);
		rate = reg_val & MAX11410_FILTER_RATE_MASK;
		if (rate >= max11410_sampling_len[filter])
			rate = max11410_sampling_len[filter] - 1;

		*val = max11410_sampling_rates[filter][rate][0];
		*val2 = max11410_sampling_rates[filter][rate][1];

		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int __max11410_write_samp_freq(struct max11410_state *st,
				      int val, int val2)
{
	int ret, i, reg_val, filter;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, MAX11410_REG_FILTER, &reg_val);
	if (ret)
		return ret;

	filter = FIELD_GET(MAX11410_FILTER_LINEF_MASK, reg_val);

	for (i = 0; i < max11410_sampling_len[filter]; ++i) {
		if (val == max11410_sampling_rates[filter][i][0] &&
		    val2 == max11410_sampling_rates[filter][i][1])
			break;
	}
	if (i == max11410_sampling_len[filter])
		return -EINVAL;

	return regmap_write_bits(st->regmap, MAX11410_REG_FILTER,
				 MAX11410_FILTER_RATE_MASK, i);
}

static int max11410_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct max11410_state *st = iio_priv(indio_dev);
	int ret, gain;
	u32 *scale_avail;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		scale_avail = st->channels[chan->address].scale_avail;
		if (!scale_avail)
			return -EOPNOTSUPP;

		/* Accept values in range 0.000001 <= scale < 1.000000 */
		if (val != 0 || val2 == 0)
			return -EINVAL;

		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		/* Convert from INT_PLUS_MICRO to FRACTIONAL_LOG2 */
		val2 = val2 * DIV_ROUND_CLOSEST(BIT(24), 1000000);
		val2 = DIV_ROUND_CLOSEST(scale_avail[0], val2);
		gain = order_base_2(val2);

		st->channels[chan->address].gain = clamp_val(gain, 0, 7);

		iio_device_release_direct(indio_dev);

		return 0;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = __max11410_write_samp_freq(st, val, val2);
		iio_device_release_direct(indio_dev);

		return ret;
	default:
		return -EINVAL;
	}
}

static int max11410_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long info)
{
	struct max11410_state *st = iio_priv(indio_dev);
	struct max11410_channel_config cfg;
	int ret, reg_val, filter;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(st->regmap, MAX11410_REG_FILTER, &reg_val);
		if (ret)
			return ret;

		filter = FIELD_GET(MAX11410_FILTER_LINEF_MASK, reg_val);

		*vals = (const int *)max11410_sampling_rates[filter];
		*length = max11410_sampling_len[filter] * 2;
		*type = IIO_VAL_INT_PLUS_MICRO;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		cfg = st->channels[chan->address];

		if (!cfg.scale_avail)
			return -EINVAL;

		*vals = cfg.scale_avail;
		*length = MAX11410_SCALE_AVAIL_SIZE * 2;
		*type = IIO_VAL_FRACTIONAL_LOG2;

		return IIO_AVAIL_LIST;
	}
	return -EINVAL;
}

static const struct iio_info max11410_info = {
	.read_raw = max11410_read_raw,
	.write_raw = max11410_write_raw,
	.read_avail = max11410_read_avail,
	.attrs = &max11410_attribute_group,
};

static irqreturn_t max11410_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max11410_state *st = iio_priv(indio_dev);
	int ret;

	ret = max11410_read_reg(st, MAX11410_REG_DATA0, &st->scan.data);
	if (ret) {
		dev_err(&indio_dev->dev, "cannot read data\n");
		goto out;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &st->scan,
					   iio_get_time_ns(indio_dev));

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int max11410_buffer_postenable(struct iio_dev *indio_dev)
{
	struct max11410_state *st = iio_priv(indio_dev);
	int scan_ch, ret;

	scan_ch = ffs(*indio_dev->active_scan_mask) - 1;

	ret = max11410_configure_channel(st, &indio_dev->channels[scan_ch]);
	if (ret)
		return ret;

	/* Start continuous conversion. */
	return max11410_write_reg(st, MAX11410_REG_CONV_START,
				  MAX11410_CONV_TYPE_CONTINUOUS);
}

static int max11410_buffer_predisable(struct iio_dev *indio_dev)
{
	struct max11410_state *st = iio_priv(indio_dev);

	/* Stop continuous conversion. */
	return max11410_write_reg(st, MAX11410_REG_CONV_START,
				  MAX11410_CONV_TYPE_SINGLE);
}

static const struct iio_buffer_setup_ops max11410_buffer_ops = {
	.postenable = &max11410_buffer_postenable,
	.predisable = &max11410_buffer_predisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static const struct iio_trigger_ops max11410_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static irqreturn_t max11410_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct max11410_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev))
		iio_trigger_poll_nested(st->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
};

static int max11410_parse_channels(struct max11410_state *st,
				   struct iio_dev *indio_dev)
{
	struct iio_chan_spec chanspec = chanspec_template;
	struct device *dev = &st->spi_dev->dev;
	struct max11410_channel_config *cfg;
	struct iio_chan_spec *channels;
	u32 reference, sig_path;
	const char *node_name;
	u32 inputs[2], scale;
	unsigned int num_ch;
	int chan_idx = 0;
	int ret, i;

	num_ch = device_get_child_node_count(dev);
	if (num_ch == 0)
		return dev_err_probe(&indio_dev->dev, -ENODEV,
				     "FW has no channels defined\n");

	/* Reserve space for soft timestamp channel */
	num_ch++;
	channels = devm_kcalloc(dev, num_ch, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	st->channels = devm_kcalloc(dev, num_ch, sizeof(*st->channels),
				    GFP_KERNEL);
	if (!st->channels)
		return -ENOMEM;

	device_for_each_child_node_scoped(dev, child) {
		node_name = fwnode_get_name(child);
		if (fwnode_property_present(child, "diff-channels")) {
			ret = fwnode_property_read_u32_array(child,
							     "diff-channels",
							     inputs,
							     ARRAY_SIZE(inputs));

			chanspec.differential = 1;
		} else {
			ret = fwnode_property_read_u32(child, "reg", &inputs[0]);

			inputs[1] = 0;
			chanspec.differential = 0;
		}
		if (ret)
			return ret;

		if (inputs[0] > MAX11410_CHANNEL_INDEX_MAX ||
		    inputs[1] > MAX11410_CHANNEL_INDEX_MAX)
			return dev_err_probe(&indio_dev->dev, -EINVAL,
					     "Invalid channel index for %s, should be less than %d\n",
					     node_name,
					     MAX11410_CHANNEL_INDEX_MAX + 1);

		cfg = &st->channels[chan_idx];

		reference = MAX11410_REFSEL_AVDD_AGND;
		fwnode_property_read_u32(child, "adi,reference", &reference);
		if (reference > MAX11410_REFSEL_MAX)
			return dev_err_probe(&indio_dev->dev, -EINVAL,
					     "Invalid adi,reference value for %s, should be less than %d.\n",
					     node_name, MAX11410_REFSEL_MAX + 1);

		if (!max11410_get_vrefp(st, reference) ||
		    (!max11410_get_vrefn(st, reference) && reference <= 2))
			return dev_err_probe(&indio_dev->dev, -EINVAL,
					     "Invalid VREF configuration for %s, either specify corresponding VREF regulators or change adi,reference property.\n",
					     node_name);

		sig_path = MAX11410_PGA_SIG_PATH_BUFFERED;
		fwnode_property_read_u32(child, "adi,input-mode", &sig_path);
		if (sig_path > MAX11410_SIG_PATH_MAX)
			return dev_err_probe(&indio_dev->dev, -EINVAL,
					     "Invalid adi,input-mode value for %s, should be less than %d.\n",
					     node_name, MAX11410_SIG_PATH_MAX + 1);

		fwnode_property_read_u32(child, "settling-time-us",
					 &cfg->settling_time_us);
		cfg->bipolar = fwnode_property_read_bool(child, "bipolar");
		cfg->buffered_vrefp = fwnode_property_read_bool(child, "adi,buffered-vrefp");
		cfg->buffered_vrefn = fwnode_property_read_bool(child, "adi,buffered-vrefn");
		cfg->refsel = reference;
		cfg->sig_path = sig_path;
		cfg->gain = 0;

		/* Enable scale_available property if input mode is PGA */
		if (sig_path == MAX11410_PGA_SIG_PATH_PGA) {
			__set_bit(IIO_CHAN_INFO_SCALE,
				  &chanspec.info_mask_separate_available);
			cfg->scale_avail = devm_kcalloc(dev, MAX11410_SCALE_AVAIL_SIZE * 2,
							sizeof(*cfg->scale_avail),
							GFP_KERNEL);
			if (!cfg->scale_avail)
				return -ENOMEM;

			scale = max11410_get_scale(st, *cfg);
			for (i = 0; i < MAX11410_SCALE_AVAIL_SIZE; i++) {
				cfg->scale_avail[2 * i] = scale >> i;
				cfg->scale_avail[2 * i + 1] = chanspec.scan_type.realbits;
			}
		} else {
			__clear_bit(IIO_CHAN_INFO_SCALE,
				    &chanspec.info_mask_separate_available);
		}

		chanspec.address = chan_idx;
		chanspec.scan_index = chan_idx;
		chanspec.channel = inputs[0];
		chanspec.channel2 = inputs[1];

		channels[chan_idx] = chanspec;
		chan_idx++;
	}

	channels[chan_idx] = (struct iio_chan_spec)IIO_CHAN_SOFT_TIMESTAMP(chan_idx);

	indio_dev->num_channels = chan_idx + 1;
	indio_dev->channels = channels;

	return 0;
}

static void max11410_disable_reg(void *reg)
{
	regulator_disable(reg);
}

static int max11410_init_vref(struct device *dev,
			      struct regulator **vref,
			      const char *id)
{
	struct regulator *reg;
	int ret;

	reg = devm_regulator_get_optional(dev, id);
	if (PTR_ERR(reg) == -ENODEV) {
		*vref = NULL;
		return 0;
	} else if (IS_ERR(reg)) {
		return PTR_ERR(reg);
	}
	ret = regulator_enable(reg);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable regulator %s\n", id);

	*vref = reg;
	return devm_add_action_or_reset(dev, max11410_disable_reg, reg);
}

static int max11410_calibrate(struct max11410_state *st, u32 cal_type)
{
	int ret, ret2, val;

	ret = max11410_write_reg(st, MAX11410_REG_CAL_START, cal_type);
	if (ret)
		return ret;

	/* Wait for status register Calibration Ready flag */
	ret = read_poll_timeout(max11410_read_reg, ret2,
				ret2 || (val & MAX11410_STATUS_CAL_READY_BIT),
				50000, MAX11410_CALIB_TIMEOUT_MS * 1000, true,
				st, MAX11410_REG_STATUS, &val);
	if (ret)
		return ret;

	return ret2;
}

static int max11410_self_calibrate(struct max11410_state *st)
{
	int ret, i;

	ret = regmap_write_bits(st->regmap, MAX11410_REG_FILTER,
				MAX11410_FILTER_RATE_MASK,
				FIELD_PREP(MAX11410_FILTER_RATE_MASK,
					   MAX11410_FILTER_RATE_MAX));
	if (ret)
		return ret;

	ret = max11410_calibrate(st, MAX11410_CAL_START_SELF);
	if (ret)
		return ret;

	ret = regmap_write_bits(st->regmap, MAX11410_REG_PGA,
				MAX11410_PGA_SIG_PATH_MASK,
				FIELD_PREP(MAX11410_PGA_SIG_PATH_MASK,
					   MAX11410_PGA_SIG_PATH_PGA));
	if (ret)
		return ret;

	/* PGA calibrations */
	for (i = 1; i < 8; ++i) {
		ret = regmap_write_bits(st->regmap, MAX11410_REG_PGA,
					MAX11410_PGA_GAIN_MASK, i);
		if (ret)
			return ret;

		ret = max11410_calibrate(st, MAX11410_CAL_START_PGA);
		if (ret)
			return ret;
	}

	/* Cleanup */
	ret = regmap_write_bits(st->regmap, MAX11410_REG_PGA,
				MAX11410_PGA_GAIN_MASK, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(st->regmap, MAX11410_REG_FILTER,
				MAX11410_FILTER_RATE_MASK, 0);
	if (ret)
		return ret;

	return regmap_write_bits(st->regmap, MAX11410_REG_PGA,
				 MAX11410_PGA_SIG_PATH_MASK,
				 FIELD_PREP(MAX11410_PGA_SIG_PATH_MASK,
					    MAX11410_PGA_SIG_PATH_BUFFERED));
}

static int max11410_probe(struct spi_device *spi)
{
	const char *vrefp_regs[] = { "vref0p", "vref1p", "vref2p" };
	const char *vrefn_regs[] = { "vref0n", "vref1n", "vref2n" };
	struct device *dev = &spi->dev;
	struct max11410_state *st;
	struct iio_dev *indio_dev;
	int ret, irqs[2];
	int i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi_dev = spi;
	init_completion(&st->completion);
	mutex_init(&st->lock);

	indio_dev->name = "max11410";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &max11410_info;

	st->regmap = devm_regmap_init_spi(spi, &regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "regmap initialization failed\n");

	ret = max11410_init_vref(dev, &st->avdd, "avdd");
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(vrefp_regs); i++) {
		ret = max11410_init_vref(dev, &st->vrefp[i], vrefp_regs[i]);
		if (ret)
			return ret;

		ret = max11410_init_vref(dev, &st->vrefn[i], vrefn_regs[i]);
		if (ret)
			return ret;
	}

	/*
	 * Regulators must be configured before parsing channels for
	 * validating "adi,reference" property of each channel.
	 */
	ret = max11410_parse_channels(st, indio_dev);
	if (ret)
		return ret;

	irqs[0] = fwnode_irq_get_byname(dev_fwnode(dev), "gpio0");
	irqs[1] = fwnode_irq_get_byname(dev_fwnode(dev), "gpio1");

	if (irqs[0] > 0) {
		st->irq = irqs[0];
		ret = regmap_write(st->regmap, MAX11410_REG_GPIO_CTRL(0),
				   MAX11410_GPIO_INTRB);
	} else if (irqs[1] > 0) {
		st->irq = irqs[1];
		ret = regmap_write(st->regmap, MAX11410_REG_GPIO_CTRL(1),
				   MAX11410_GPIO_INTRB);
	} else if (spi->irq > 0) {
		return dev_err_probe(dev, -ENODEV,
				     "no interrupt name specified");
	}

	if (ret)
		return ret;

	ret = regmap_set_bits(st->regmap, MAX11410_REG_CTRL,
			      MAX11410_CTRL_FORMAT_BIT);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &max11410_trigger_handler,
					      &max11410_buffer_ops);
	if (ret)
		return ret;

	if (st->irq > 0) {
		st->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						  indio_dev->name,
						  iio_device_id(indio_dev));
		if (!st->trig)
			return -ENOMEM;

		st->trig->ops = &max11410_trigger_ops;
		ret = devm_iio_trigger_register(dev, st->trig);
		if (ret)
			return ret;

		ret = devm_request_threaded_irq(dev, st->irq, NULL,
						&max11410_interrupt,
						IRQF_ONESHOT, "max11410",
						indio_dev);
		if (ret)
			return ret;
	}

	ret = max11410_self_calibrate(st);
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot perform device self calibration\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id max11410_spi_of_id[] = {
	{ .compatible = "adi,max11410" },
	{ }
};
MODULE_DEVICE_TABLE(of, max11410_spi_of_id);

static const struct spi_device_id max11410_id[] = {
	{ "max11410" },
	{ }
};
MODULE_DEVICE_TABLE(spi, max11410_id);

static struct spi_driver max11410_driver = {
	.driver = {
		.name	= "max11410",
		.of_match_table = max11410_spi_of_id,
	},
	.probe		= max11410_probe,
	.id_table	= max11410_id,
};
module_spi_driver(max11410_driver);

MODULE_AUTHOR("David Jung <David.Jung@analog.com>");
MODULE_AUTHOR("Ibrahim Tilki <Ibrahim.Tilki@analog.com>");
MODULE_DESCRIPTION("Analog Devices MAX11410 ADC");
MODULE_LICENSE("GPL");
