// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Richtek Technology Corp.
 *
 * ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/util_macros.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define RTQ6056_REG_CONFIG	0x00
#define RTQ6056_REG_SHUNTVOLT	0x01
#define RTQ6056_REG_BUSVOLT	0x02
#define RTQ6056_REG_POWER	0x03
#define RTQ6056_REG_CURRENT	0x04
#define RTQ6056_REG_CALIBRATION	0x05
#define RTQ6056_REG_MASKENABLE	0x06
#define RTQ6056_REG_ALERTLIMIT	0x07
#define RTQ6056_REG_MANUFACTID	0xFE
#define RTQ6056_REG_DIEID	0xFF

#define RTQ6056_VENDOR_ID	0x1214
#define RTQ6056_DEFAULT_CONFIG	0x4127
#define RTQ6056_CONT_ALLON	7

#define RTQ6059_DEFAULT_CONFIG	0x3C47
#define RTQ6059_VBUS_LSB_OFFSET	3
#define RTQ6059_AVG_BASE	8

enum {
	RTQ6056_CH_VSHUNT = 0,
	RTQ6056_CH_VBUS,
	RTQ6056_CH_POWER,
	RTQ6056_CH_CURRENT,
	RTQ6056_MAX_CHANNEL
};

/*
 * The enum is to present the 0x00 CONFIG RG bitfield for the 16bit RG value
 * field value order from LSB to MSB
 * RTQ6053/6 is OPMODE->VSHUNTCT->VBUSCT->AVG->RESET
 * RTQ6059 is OPMODE->SADC->BADC->PGA->RESET
 */
enum {
	F_OPMODE = 0,
	F_VSHUNTCT,
	F_RTQ6059_SADC = F_VSHUNTCT,
	F_VBUSCT,
	F_RTQ6059_BADC = F_VBUSCT,
	F_AVG,
	F_RTQ6059_PGA = F_AVG,
	F_RESET,
	F_MAX_FIELDS
};

struct rtq6056_priv;

struct richtek_dev_data {
	bool fixed_samp_freq;
	u8 vbus_offset;
	int default_conv_time_us;
	unsigned int default_config;
	unsigned int calib_coefficient;
	const int *avg_sample_list;
	int avg_sample_list_length;
	const struct reg_field *reg_fields;
	const struct iio_chan_spec *channels;
	int num_channels;
	int (*read_scale)(struct iio_chan_spec const *ch, int *val, int *val2);
	int (*set_average)(struct rtq6056_priv *priv, int val);
};

struct rtq6056_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_field *rm_fields[F_MAX_FIELDS];
	const struct richtek_dev_data *devdata;
	u32 shunt_resistor_uohm;
	int vshuntct_us;
	int vbusct_us;
	int avg_sample;
};

static const struct reg_field rtq6056_reg_fields[F_MAX_FIELDS] = {
	[F_OPMODE] = REG_FIELD(RTQ6056_REG_CONFIG, 0, 2),
	[F_VSHUNTCT] = REG_FIELD(RTQ6056_REG_CONFIG, 3, 5),
	[F_VBUSCT] = REG_FIELD(RTQ6056_REG_CONFIG, 6, 8),
	[F_AVG]	= REG_FIELD(RTQ6056_REG_CONFIG, 9, 11),
	[F_RESET] = REG_FIELD(RTQ6056_REG_CONFIG, 15, 15),
};

static const struct reg_field rtq6059_reg_fields[F_MAX_FIELDS] = {
	[F_OPMODE] = REG_FIELD(RTQ6056_REG_CONFIG, 0, 2),
	[F_RTQ6059_SADC] = REG_FIELD(RTQ6056_REG_CONFIG, 3, 6),
	[F_RTQ6059_BADC] = REG_FIELD(RTQ6056_REG_CONFIG, 7, 10),
	[F_RTQ6059_PGA]	= REG_FIELD(RTQ6056_REG_CONFIG, 11, 12),
	[F_RESET] = REG_FIELD(RTQ6056_REG_CONFIG, 15, 15),
};

static const struct iio_chan_spec rtq6056_channels[RTQ6056_MAX_CHANNEL + 1] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.address = RTQ6056_REG_SHUNTVOLT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.address = RTQ6056_REG_BUSVOLT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 2,
		.address = RTQ6056_REG_POWER,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 3,
		.address = RTQ6056_REG_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 3,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(RTQ6056_MAX_CHANNEL),
};

/*
 * Difference between RTQ6056 and RTQ6059
 * - Fixed sampling conversion time
 * - Average sample numbers
 * - Channel scale
 * - calibration coefficient
 */
static const struct iio_chan_spec rtq6059_channels[RTQ6056_MAX_CHANNEL + 1] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.address = RTQ6056_REG_SHUNTVOLT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.address = RTQ6056_REG_BUSVOLT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_POWER,
		.indexed = 1,
		.channel = 2,
		.address = RTQ6056_REG_POWER,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 3,
		.address = RTQ6056_REG_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 3,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(RTQ6056_MAX_CHANNEL),
};

static int rtq6056_adc_read_channel(struct rtq6056_priv *priv,
				    struct iio_chan_spec const *ch,
				    int *val)
{
	const struct richtek_dev_data *devdata = priv->devdata;
	struct device *dev = priv->dev;
	unsigned int addr = ch->address;
	unsigned int regval;
	int ret;

	pm_runtime_get_sync(dev);
	ret = regmap_read(priv->regmap, addr, &regval);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put(dev);
	if (ret)
		return ret;

	/* Power and VBUS is unsigned 16-bit, others are signed 16-bit */
	switch (addr) {
	case RTQ6056_REG_BUSVOLT:
		regval >>= devdata->vbus_offset;
		*val = regval;
		return IIO_VAL_INT;
	case RTQ6056_REG_POWER:
		*val = regval;
		return IIO_VAL_INT;
	case RTQ6056_REG_SHUNTVOLT:
	case RTQ6056_REG_CURRENT:
		*val = sign_extend32(regval, 16);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int rtq6056_adc_read_scale(struct iio_chan_spec const *ch, int *val,
				  int *val2)
{
	switch (ch->address) {
	case RTQ6056_REG_SHUNTVOLT:
		/* VSHUNT lsb  2.5uV */
		*val = 2500;
		*val2 = 1000000;
		return IIO_VAL_FRACTIONAL;
	case RTQ6056_REG_BUSVOLT:
		/* VBUS lsb 1.25mV */
		*val = 1250;
		*val2 = 1000;
		return IIO_VAL_FRACTIONAL;
	case RTQ6056_REG_POWER:
		/* Power lsb 25mW */
		*val = 25;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int rtq6059_adc_read_scale(struct iio_chan_spec const *ch, int *val,
				  int *val2)
{
	switch (ch->address) {
	case RTQ6056_REG_SHUNTVOLT:
		/* VSHUNT lsb  10uV */
		*val = 10000;
		*val2 = 1000000;
		return IIO_VAL_FRACTIONAL;
	case RTQ6056_REG_BUSVOLT:
		/* VBUS lsb 4mV */
		*val = 4;
		return IIO_VAL_INT;
	case RTQ6056_REG_POWER:
		/* Power lsb 20mW */
		*val = 20;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

/*
 * Sample frequency for channel VSHUNT and VBUS. The indices correspond
 * with the bit value expected by the chip. And it can be found at
 * https://www.richtek.com/assets/product_file/RTQ6056/DSQ6056-00.pdf
 */
static const int rtq6056_samp_freq_list[] = {
	7194, 4926, 3717, 1904, 964, 485, 243, 122,
};

static int rtq6056_adc_set_samp_freq(struct rtq6056_priv *priv,
				     struct iio_chan_spec const *ch, int val)
{
	struct regmap_field *rm_field;
	unsigned int selector;
	int *ct, ret;

	if (val > 7194 || val < 122)
		return -EINVAL;

	if (ch->address == RTQ6056_REG_SHUNTVOLT) {
		rm_field = priv->rm_fields[F_VSHUNTCT];
		ct = &priv->vshuntct_us;
	} else if (ch->address == RTQ6056_REG_BUSVOLT) {
		rm_field = priv->rm_fields[F_VBUSCT];
		ct = &priv->vbusct_us;
	} else
		return -EINVAL;

	selector = find_closest_descending(val, rtq6056_samp_freq_list,
					   ARRAY_SIZE(rtq6056_samp_freq_list));

	ret = regmap_field_write(rm_field, selector);
	if (ret)
		return ret;

	*ct = 1000000 / rtq6056_samp_freq_list[selector];

	return 0;
}

/*
 * Available averaging rate for rtq6056. The indices correspond with the bit
 * value expected by the chip. And it can be found at
 * https://www.richtek.com/assets/product_file/RTQ6056/DSQ6056-00.pdf
 */
static const int rtq6056_avg_sample_list[] = {
	1, 4, 16, 64, 128, 256, 512, 1024,
};

static const int rtq6059_avg_sample_list[] = {
	1, 2, 4, 8, 16, 32, 64, 128,
};

static int rtq6056_adc_set_average(struct rtq6056_priv *priv, int val)
{
	unsigned int selector;
	int ret;

	if (val > 1024 || val < 1)
		return -EINVAL;

	selector = find_closest(val, rtq6056_avg_sample_list,
				ARRAY_SIZE(rtq6056_avg_sample_list));

	ret = regmap_field_write(priv->rm_fields[F_AVG], selector);
	if (ret)
		return ret;

	priv->avg_sample = rtq6056_avg_sample_list[selector];

	return 0;
}

static int rtq6059_adc_set_average(struct rtq6056_priv *priv, int val)
{
	unsigned int selector;
	int ret;

	if (val > 128 || val < 1)
		return -EINVAL;

	/* The supported average sample is 2^x (x from 0 to 7) */
	selector = fls(val) - 1;

	ret = regmap_field_write(priv->rm_fields[F_RTQ6059_BADC],
				 RTQ6059_AVG_BASE + selector);
	if (ret)
		return ret;

	ret = regmap_field_write(priv->rm_fields[F_RTQ6059_SADC],
				 RTQ6059_AVG_BASE + selector);

	priv->avg_sample = BIT(selector);

	return 0;
}

static int rtq6056_adc_get_sample_freq(struct rtq6056_priv *priv,
				       struct iio_chan_spec const *ch, int *val)
{
	int sample_time;

	if (ch->address == RTQ6056_REG_SHUNTVOLT)
		sample_time = priv->vshuntct_us;
	else if (ch->address == RTQ6056_REG_BUSVOLT)
		sample_time = priv->vbusct_us;
	else {
		sample_time = priv->vshuntct_us + priv->vbusct_us;
		sample_time *= priv->avg_sample;
	}

	*val = 1000000 / sample_time;

	return IIO_VAL_INT;
}

static int rtq6056_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int *val,
				int *val2, long mask)
{
	struct rtq6056_priv *priv = iio_priv(indio_dev);
	const struct richtek_dev_data *devdata = priv->devdata;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return rtq6056_adc_read_channel(priv, chan, val);
	case IIO_CHAN_INFO_SCALE:
		return devdata->read_scale(chan, val, val2);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = priv->avg_sample;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return rtq6056_adc_get_sample_freq(priv, chan, val);
	default:
		return -EINVAL;
	}
}

static int rtq6056_adc_read_avail(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals, int *type, int *length,
				  long mask)
{
	struct rtq6056_priv *priv = iio_priv(indio_dev);
	const struct richtek_dev_data *devdata = priv->devdata;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = rtq6056_samp_freq_list;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(rtq6056_samp_freq_list);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = devdata->avg_sample_list;
		*length = devdata->avg_sample_list_length;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int rtq6056_adc_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, int val,
				 int val2, long mask)
{
	struct rtq6056_priv *priv = iio_priv(indio_dev);
	const struct richtek_dev_data *devdata = priv->devdata;

	iio_device_claim_direct_scoped(return -EBUSY, indio_dev) {
		switch (mask) {
		case IIO_CHAN_INFO_SAMP_FREQ:
			if (devdata->fixed_samp_freq)
				return -EINVAL;
			return rtq6056_adc_set_samp_freq(priv, chan, val);
		case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
			return devdata->set_average(priv, val);
		default:
			return -EINVAL;
		}
	}
	unreachable();
}

static const char *rtq6056_channel_labels[RTQ6056_MAX_CHANNEL] = {
	[RTQ6056_CH_VSHUNT] = "Vshunt",
	[RTQ6056_CH_VBUS] = "Vbus",
	[RTQ6056_CH_POWER] = "Power",
	[RTQ6056_CH_CURRENT] = "Current",
};

static int rtq6056_adc_read_label(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  char *label)
{
	return sysfs_emit(label, "%s\n", rtq6056_channel_labels[chan->channel]);
}

static int rtq6056_set_shunt_resistor(struct rtq6056_priv *priv,
				      int resistor_uohm)
{
	const struct richtek_dev_data *devdata = priv->devdata;
	unsigned int calib_val;
	int ret;

	if (resistor_uohm <= 0) {
		dev_err(priv->dev, "Invalid resistor [%d]\n", resistor_uohm);
		return -EINVAL;
	}

	/* calibration = coefficient / (Rshunt (uOhm) * current lsb (1mA)) */
	calib_val = devdata->calib_coefficient / resistor_uohm;
	ret = regmap_write(priv->regmap, RTQ6056_REG_CALIBRATION, calib_val);
	if (ret)
		return ret;

	priv->shunt_resistor_uohm = resistor_uohm;

	return 0;
}

static ssize_t shunt_resistor_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct rtq6056_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int vals[2] = { priv->shunt_resistor_uohm, 1000000 };

	return iio_format_value(buf, IIO_VAL_FRACTIONAL, 1, vals);
}

static ssize_t shunt_resistor_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct rtq6056_priv *priv = iio_priv(indio_dev);
	int val, val_fract, ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	ret = iio_str_to_fixpoint(buf, 100000, &val, &val_fract);
	if (ret)
		goto out_store;

	ret = rtq6056_set_shunt_resistor(priv, val * 1000000 + val_fract);

out_store:
	iio_device_release_direct_mode(indio_dev);

	return ret ?: len;
}

static IIO_DEVICE_ATTR_RW(shunt_resistor, 0);

static struct attribute *rtq6056_attributes[] = {
	&iio_dev_attr_shunt_resistor.dev_attr.attr,
	NULL
};

static const struct attribute_group rtq6056_attribute_group = {
	.attrs = rtq6056_attributes,
};

static const struct iio_info rtq6056_info = {
	.attrs = &rtq6056_attribute_group,
	.read_raw = rtq6056_adc_read_raw,
	.read_avail = rtq6056_adc_read_avail,
	.write_raw = rtq6056_adc_write_raw,
	.read_label = rtq6056_adc_read_label,
};

static irqreturn_t rtq6056_buffer_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct rtq6056_priv *priv = iio_priv(indio_dev);
	const struct richtek_dev_data *devdata = priv->devdata;
	struct device *dev = priv->dev;
	struct {
		u16 vals[RTQ6056_MAX_CHANNEL];
		aligned_s64 timestamp;
	} data;
	unsigned int raw;
	int i = 0, bit, ret;

	memset(&data, 0, sizeof(data));

	pm_runtime_get_sync(dev);

	iio_for_each_active_channel(indio_dev, bit) {
		unsigned int addr = rtq6056_channels[bit].address;

		ret = regmap_read(priv->regmap, addr, &raw);
		if (ret)
			goto out;

		if (addr == RTQ6056_REG_BUSVOLT)
			raw >>= devdata->vbus_offset;

		data.vals[i++] = raw;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data, iio_get_time_ns(indio_dev));

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put(dev);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void rtq6056_enter_shutdown_state(void *dev)
{
	struct rtq6056_priv *priv = dev_get_drvdata(dev);

	/* Enter shutdown state */
	regmap_field_write(priv->rm_fields[F_OPMODE], 0);
}

static bool rtq6056_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RTQ6056_REG_CONFIG ... RTQ6056_REG_ALERTLIMIT:
	case RTQ6056_REG_MANUFACTID ... RTQ6056_REG_DIEID:
		return true;
	default:
		return false;
	}
}

static bool rtq6056_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RTQ6056_REG_CONFIG:
	case RTQ6056_REG_CALIBRATION ... RTQ6056_REG_ALERTLIMIT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rtq6056_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = RTQ6056_REG_DIEID,
	.readable_reg = rtq6056_is_readable_reg,
	.writeable_reg = rtq6056_is_writeable_reg,
};

static int rtq6056_probe(struct i2c_client *i2c)
{
	struct iio_dev *indio_dev;
	struct rtq6056_priv *priv;
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	const struct richtek_dev_data *devdata;
	unsigned int vendor_id, shunt_resistor_uohm;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	devdata = device_get_match_data(dev);
	if (!devdata)
		return dev_err_probe(dev, -EINVAL, "Invalid dev data\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dev = dev;
	priv->vshuntct_us = priv->vbusct_us = devdata->default_conv_time_us;
	priv->avg_sample = 1;
	priv->devdata = devdata;
	i2c_set_clientdata(i2c, priv);

	regmap = devm_regmap_init_i2c(i2c, &rtq6056_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init regmap\n");

	priv->regmap = regmap;

	ret = regmap_read(regmap, RTQ6056_REG_MANUFACTID, &vendor_id);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get manufacturer info\n");

	if (vendor_id != RTQ6056_VENDOR_ID)
		return dev_err_probe(dev, -ENODEV,
				     "Invalid vendor id 0x%04x\n", vendor_id);

	ret = devm_regmap_field_bulk_alloc(dev, regmap, priv->rm_fields,
					   devdata->reg_fields, F_MAX_FIELDS);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init regmap field\n");

	ret = regmap_write(regmap, RTQ6056_REG_CONFIG, devdata->default_config);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable continuous sensing\n");

	ret = devm_add_action_or_reset(dev, rtq6056_enter_shutdown_state, dev);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_mark_last_busy(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pm_runtime\n");

	/* By default, use 2000 micro-Ohm resistor */
	shunt_resistor_uohm = 2000;
	device_property_read_u32(dev, "shunt-resistor-micro-ohms",
				 &shunt_resistor_uohm);

	ret = rtq6056_set_shunt_resistor(priv, shunt_resistor_uohm);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to init shunt resistor\n");

	indio_dev->name = "rtq6056";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = devdata->channels;
	indio_dev->num_channels = devdata->num_channels;
	indio_dev->info = &rtq6056_info;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      rtq6056_buffer_trigger_handler,
					      NULL);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to allocate iio trigger buffer\n");

	return devm_iio_device_register(dev, indio_dev);
}

static int rtq6056_runtime_suspend(struct device *dev)
{
	struct rtq6056_priv *priv = dev_get_drvdata(dev);

	/* Configure to shutdown mode */
	return regmap_field_write(priv->rm_fields[F_OPMODE], 0);
}

static int rtq6056_runtime_resume(struct device *dev)
{
	struct rtq6056_priv *priv = dev_get_drvdata(dev);
	int sample_rdy_time_us, ret;

	ret = regmap_field_write(priv->rm_fields[F_OPMODE], RTQ6056_CONT_ALLON);
	if (ret)
		return ret;

	sample_rdy_time_us = priv->vbusct_us + priv->vshuntct_us;
	sample_rdy_time_us *= priv->avg_sample;

	usleep_range(sample_rdy_time_us, sample_rdy_time_us + 100);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(rtq6056_pm_ops, rtq6056_runtime_suspend,
				 rtq6056_runtime_resume, NULL);

static const struct richtek_dev_data rtq6056_devdata = {
	.default_conv_time_us = 1037,
	.calib_coefficient = 5120000,
	/*
	 * By default, configure average sample as 1, bus and shunt conversion
	 * time as 1037 microsecond, and operating mode to all on.
	 */
	.default_config = RTQ6056_DEFAULT_CONFIG,
	.avg_sample_list = rtq6056_avg_sample_list,
	.avg_sample_list_length = ARRAY_SIZE(rtq6056_avg_sample_list),
	.reg_fields = rtq6056_reg_fields,
	.channels = rtq6056_channels,
	.num_channels = ARRAY_SIZE(rtq6056_channels),
	.read_scale = rtq6056_adc_read_scale,
	.set_average = rtq6056_adc_set_average,
};

static const struct richtek_dev_data rtq6059_devdata = {
	.fixed_samp_freq = true,
	.vbus_offset = RTQ6059_VBUS_LSB_OFFSET,
	.default_conv_time_us = 532,
	.calib_coefficient = 40960000,
	/*
	 * By default, configure average sample as 1, bus and shunt conversion
	 * time as 532 microsecond, and operating mode to all on.
	 */
	.default_config = RTQ6059_DEFAULT_CONFIG,
	.avg_sample_list = rtq6059_avg_sample_list,
	.avg_sample_list_length = ARRAY_SIZE(rtq6059_avg_sample_list),
	.reg_fields = rtq6059_reg_fields,
	.channels = rtq6059_channels,
	.num_channels = ARRAY_SIZE(rtq6059_channels),
	.read_scale = rtq6059_adc_read_scale,
	.set_average = rtq6059_adc_set_average,
};

static const struct of_device_id rtq6056_device_match[] = {
	{ .compatible = "richtek,rtq6056", .data = &rtq6056_devdata },
	{ .compatible = "richtek,rtq6059", .data = &rtq6059_devdata },
	{ }
};
MODULE_DEVICE_TABLE(of, rtq6056_device_match);

static struct i2c_driver rtq6056_driver = {
	.driver = {
		.name = "rtq6056",
		.of_match_table = rtq6056_device_match,
		.pm = pm_ptr(&rtq6056_pm_ops),
	},
	.probe = rtq6056_probe,
};
module_i2c_driver(rtq6056_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RTQ6056 Driver");
MODULE_LICENSE("GPL v2");
