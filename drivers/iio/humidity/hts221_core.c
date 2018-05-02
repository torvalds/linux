/*
 * STMicroelectronics hts221 sensor driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include "hts221.h"

#define HTS221_REG_WHOAMI_ADDR		0x0f
#define HTS221_REG_WHOAMI_VAL		0xbc

#define HTS221_REG_CNTRL1_ADDR		0x20
#define HTS221_REG_CNTRL2_ADDR		0x21

#define HTS221_REG_AVG_ADDR		0x10
#define HTS221_REG_H_OUT_L		0x28
#define HTS221_REG_T_OUT_L		0x2a

#define HTS221_HUMIDITY_AVG_MASK	0x07
#define HTS221_TEMP_AVG_MASK		0x38

#define HTS221_ODR_MASK			0x03
#define HTS221_BDU_MASK			BIT(2)
#define HTS221_ENABLE_MASK		BIT(7)

/* calibration registers */
#define HTS221_REG_0RH_CAL_X_H		0x36
#define HTS221_REG_1RH_CAL_X_H		0x3a
#define HTS221_REG_0RH_CAL_Y_H		0x30
#define HTS221_REG_1RH_CAL_Y_H		0x31
#define HTS221_REG_0T_CAL_X_L		0x3c
#define HTS221_REG_1T_CAL_X_L		0x3e
#define HTS221_REG_0T_CAL_Y_H		0x32
#define HTS221_REG_1T_CAL_Y_H		0x33
#define HTS221_REG_T1_T0_CAL_Y_H	0x35

struct hts221_odr {
	u8 hz;
	u8 val;
};

#define HTS221_AVG_DEPTH		8
struct hts221_avg {
	u8 addr;
	u8 mask;
	u16 avg_avl[HTS221_AVG_DEPTH];
};

static const struct hts221_odr hts221_odr_table[] = {
	{  1, 0x01 },	/* 1Hz */
	{  7, 0x02 },	/* 7Hz */
	{ 13, 0x03 },	/* 12.5Hz */
};

static const struct hts221_avg hts221_avg_list[] = {
	{
		.addr = HTS221_REG_AVG_ADDR,
		.mask = HTS221_HUMIDITY_AVG_MASK,
		.avg_avl = {
			4, /* 0.4 %RH */
			8, /* 0.3 %RH */
			16, /* 0.2 %RH */
			32, /* 0.15 %RH */
			64, /* 0.1 %RH */
			128, /* 0.07 %RH */
			256, /* 0.05 %RH */
			512, /* 0.03 %RH */
		},
	},
	{
		.addr = HTS221_REG_AVG_ADDR,
		.mask = HTS221_TEMP_AVG_MASK,
		.avg_avl = {
			2, /* 0.08 degC */
			4, /* 0.05 degC */
			8, /* 0.04 degC */
			16, /* 0.03 degC */
			32, /* 0.02 degC */
			64, /* 0.015 degC */
			128, /* 0.01 degC */
			256, /* 0.007 degC */
		},
	},
};

static const struct iio_chan_spec hts221_channels[] = {
	{
		.type = IIO_HUMIDITYRELATIVE,
		.address = HTS221_REG_H_OUT_L,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_TEMP,
		.address = HTS221_REG_T_OUT_L,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static int hts221_check_whoami(struct hts221_hw *hw)
{
	int err, data;

	err = regmap_read(hw->regmap, HTS221_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != HTS221_REG_WHOAMI_VAL) {
		dev_err(hw->dev, "wrong whoami {%02x vs %02x}\n",
			data, HTS221_REG_WHOAMI_VAL);
		return -ENODEV;
	}

	return 0;
}

static int hts221_update_odr(struct hts221_hw *hw, u8 odr)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(hts221_odr_table); i++)
		if (hts221_odr_table[i].hz == odr)
			break;

	if (i == ARRAY_SIZE(hts221_odr_table))
		return -EINVAL;

	err = regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
				 HTS221_ODR_MASK,
				 FIELD_PREP(HTS221_ODR_MASK,
					    hts221_odr_table[i].val));
	if (err < 0)
		return err;

	hw->odr = odr;

	return 0;
}

static int hts221_update_avg(struct hts221_hw *hw,
			     enum hts221_sensor_type type,
			     u16 val)
{
	const struct hts221_avg *avg = &hts221_avg_list[type];
	int i, err, data;

	for (i = 0; i < HTS221_AVG_DEPTH; i++)
		if (avg->avg_avl[i] == val)
			break;

	if (i == HTS221_AVG_DEPTH)
		return -EINVAL;

	data = ((i << __ffs(avg->mask)) & avg->mask);
	err = regmap_update_bits(hw->regmap, avg->addr,
				 avg->mask, data);
	if (err < 0)
		return err;

	hw->sensors[type].cur_avg_idx = i;

	return 0;
}

static ssize_t hts221_sysfs_sampling_freq(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	int i;
	ssize_t len = 0;

	for (i = 0; i < ARRAY_SIZE(hts221_odr_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 hts221_odr_table[i].hz);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t
hts221_sysfs_rh_oversampling_avail(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	const struct hts221_avg *avg = &hts221_avg_list[HTS221_SENSOR_H];
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(avg->avg_avl); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 avg->avg_avl[i]);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t
hts221_sysfs_temp_oversampling_avail(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	const struct hts221_avg *avg = &hts221_avg_list[HTS221_SENSOR_T];
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(avg->avg_avl); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 avg->avg_avl[i]);
	buf[len - 1] = '\n';

	return len;
}

int hts221_set_enable(struct hts221_hw *hw, bool enable)
{
	int err;

	err = regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
				 HTS221_ENABLE_MASK,
				 FIELD_PREP(HTS221_ENABLE_MASK, enable));
	if (err < 0)
		return err;

	hw->enabled = enable;

	return 0;
}

static int hts221_parse_temp_caldata(struct hts221_hw *hw)
{
	int err, *slope, *b_gen, cal0, cal1;
	s16 cal_x0, cal_x1, cal_y0, cal_y1;
	__le16 val;

	err = regmap_read(hw->regmap, HTS221_REG_0T_CAL_Y_H, &cal0);
	if (err < 0)
		return err;

	err = regmap_read(hw->regmap, HTS221_REG_T1_T0_CAL_Y_H, &cal1);
	if (err < 0)
		return err;
	cal_y0 = ((cal1 & 0x3) << 8) | cal0;

	err = regmap_read(hw->regmap, HTS221_REG_1T_CAL_Y_H, &cal0);
	if (err < 0)
		return err;
	cal_y1 = (((cal1 & 0xc) >> 2) << 8) | cal0;

	err = regmap_bulk_read(hw->regmap, HTS221_REG_0T_CAL_X_L,
			       &val, sizeof(val));
	if (err < 0)
		return err;
	cal_x0 = le16_to_cpu(val);

	err = regmap_bulk_read(hw->regmap, HTS221_REG_1T_CAL_X_L,
			       &val, sizeof(val));
	if (err < 0)
		return err;
	cal_x1 = le16_to_cpu(val);

	slope = &hw->sensors[HTS221_SENSOR_T].slope;
	b_gen = &hw->sensors[HTS221_SENSOR_T].b_gen;

	*slope = ((cal_y1 - cal_y0) * 8000) / (cal_x1 - cal_x0);
	*b_gen = (((s32)cal_x1 * cal_y0 - (s32)cal_x0 * cal_y1) * 1000) /
		 (cal_x1 - cal_x0);
	*b_gen *= 8;

	return 0;
}

static int hts221_parse_rh_caldata(struct hts221_hw *hw)
{
	int err, *slope, *b_gen, data;
	s16 cal_x0, cal_x1, cal_y0, cal_y1;
	__le16 val;

	err = regmap_read(hw->regmap, HTS221_REG_0RH_CAL_Y_H, &data);
	if (err < 0)
		return err;
	cal_y0 = data;

	err = regmap_read(hw->regmap, HTS221_REG_1RH_CAL_Y_H, &data);
	if (err < 0)
		return err;
	cal_y1 = data;

	err = regmap_bulk_read(hw->regmap, HTS221_REG_0RH_CAL_X_H,
			       &val, sizeof(val));
	if (err < 0)
		return err;
	cal_x0 = le16_to_cpu(val);

	err = regmap_bulk_read(hw->regmap, HTS221_REG_1RH_CAL_X_H,
			       &val, sizeof(val));
	if (err < 0)
		return err;
	cal_x1 = le16_to_cpu(val);

	slope = &hw->sensors[HTS221_SENSOR_H].slope;
	b_gen = &hw->sensors[HTS221_SENSOR_H].b_gen;

	*slope = ((cal_y1 - cal_y0) * 8000) / (cal_x1 - cal_x0);
	*b_gen = (((s32)cal_x1 * cal_y0 - (s32)cal_x0 * cal_y1) * 1000) /
		 (cal_x1 - cal_x0);
	*b_gen *= 8;

	return 0;
}

static int hts221_get_sensor_scale(struct hts221_hw *hw,
				   enum iio_chan_type ch_type,
				   int *val, int *val2)
{
	s64 tmp;
	s32 rem, div, data;

	switch (ch_type) {
	case IIO_HUMIDITYRELATIVE:
		data = hw->sensors[HTS221_SENSOR_H].slope;
		div = (1 << 4) * 1000;
		break;
	case IIO_TEMP:
		data = hw->sensors[HTS221_SENSOR_T].slope;
		div = (1 << 6) * 1000;
		break;
	default:
		return -EINVAL;
	}

	tmp = div_s64(data * 1000000000LL, div);
	tmp = div_s64_rem(tmp, 1000000000LL, &rem);

	*val = tmp;
	*val2 = rem;

	return IIO_VAL_INT_PLUS_NANO;
}

static int hts221_get_sensor_offset(struct hts221_hw *hw,
				    enum iio_chan_type ch_type,
				    int *val, int *val2)
{
	s64 tmp;
	s32 rem, div, data;

	switch (ch_type) {
	case IIO_HUMIDITYRELATIVE:
		data = hw->sensors[HTS221_SENSOR_H].b_gen;
		div = hw->sensors[HTS221_SENSOR_H].slope;
		break;
	case IIO_TEMP:
		data = hw->sensors[HTS221_SENSOR_T].b_gen;
		div = hw->sensors[HTS221_SENSOR_T].slope;
		break;
	default:
		return -EINVAL;
	}

	tmp = div_s64(data * 1000000000LL, div);
	tmp = div_s64_rem(tmp, 1000000000LL, &rem);

	*val = tmp;
	*val2 = rem;

	return IIO_VAL_INT_PLUS_NANO;
}

static int hts221_read_oneshot(struct hts221_hw *hw, u8 addr, int *val)
{
	__le16 data;
	int err;

	err = hts221_set_enable(hw, true);
	if (err < 0)
		return err;

	msleep(50);

	err = regmap_bulk_read(hw->regmap, addr, &data, sizeof(data));
	if (err < 0)
		return err;

	hts221_set_enable(hw, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int hts221_read_raw(struct iio_dev *iio_dev,
			   struct iio_chan_spec const *ch,
			   int *val, int *val2, long mask)
{
	struct hts221_hw *hw = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = hts221_read_oneshot(hw, ch->address, val);
		break;
	case IIO_CHAN_INFO_SCALE:
		ret = hts221_get_sensor_scale(hw, ch->type, val, val2);
		break;
	case IIO_CHAN_INFO_OFFSET:
		ret = hts221_get_sensor_offset(hw, ch->type, val, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = hw->odr;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		u8 idx;
		const struct hts221_avg *avg;

		switch (ch->type) {
		case IIO_HUMIDITYRELATIVE:
			avg = &hts221_avg_list[HTS221_SENSOR_H];
			idx = hw->sensors[HTS221_SENSOR_H].cur_avg_idx;
			*val = avg->avg_avl[idx];
			ret = IIO_VAL_INT;
			break;
		case IIO_TEMP:
			avg = &hts221_avg_list[HTS221_SENSOR_T];
			idx = hw->sensors[HTS221_SENSOR_T].cur_avg_idx;
			*val = avg->avg_avl[idx];
			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return ret;
}

static int hts221_write_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct hts221_hw *hw = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hts221_update_odr(hw, val);
		break;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			ret = hts221_update_avg(hw, HTS221_SENSOR_H, val);
			break;
		case IIO_TEMP:
			ret = hts221_update_avg(hw, HTS221_SENSOR_T, val);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return ret;
}

static int hts221_validate_trigger(struct iio_dev *iio_dev,
				   struct iio_trigger *trig)
{
	struct hts221_hw *hw = iio_priv(iio_dev);

	return hw->trig == trig ? 0 : -EINVAL;
}

static IIO_DEVICE_ATTR(in_humidity_oversampling_ratio_available, S_IRUGO,
		       hts221_sysfs_rh_oversampling_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_oversampling_ratio_available, S_IRUGO,
		       hts221_sysfs_temp_oversampling_avail, NULL, 0);
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(hts221_sysfs_sampling_freq);

static struct attribute *hts221_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_humidity_oversampling_ratio_available.dev_attr.attr,
	&iio_dev_attr_in_temp_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group hts221_attribute_group = {
	.attrs = hts221_attributes,
};

static const struct iio_info hts221_info = {
	.attrs = &hts221_attribute_group,
	.read_raw = hts221_read_raw,
	.write_raw = hts221_write_raw,
	.validate_trigger = hts221_validate_trigger,
};

static const unsigned long hts221_scan_masks[] = {0x3, 0x0};

int hts221_probe(struct device *dev, int irq, const char *name,
		 struct regmap *regmap)
{
	struct iio_dev *iio_dev;
	struct hts221_hw *hw;
	int err;
	u8 data;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)iio_dev);

	hw = iio_priv(iio_dev);
	hw->name = name;
	hw->dev = dev;
	hw->irq = irq;
	hw->regmap = regmap;

	err = hts221_check_whoami(hw);
	if (err < 0)
		return err;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;
	iio_dev->available_scan_masks = hts221_scan_masks;
	iio_dev->channels = hts221_channels;
	iio_dev->num_channels = ARRAY_SIZE(hts221_channels);
	iio_dev->name = HTS221_DEV_NAME;
	iio_dev->info = &hts221_info;

	/* enable Block Data Update */
	err = regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
				 HTS221_BDU_MASK,
				 FIELD_PREP(HTS221_BDU_MASK, 1));
	if (err < 0)
		return err;

	err = hts221_update_odr(hw, hts221_odr_table[0].hz);
	if (err < 0)
		return err;

	/* configure humidity sensor */
	err = hts221_parse_rh_caldata(hw);
	if (err < 0) {
		dev_err(hw->dev, "failed to get rh calibration data\n");
		return err;
	}

	data = hts221_avg_list[HTS221_SENSOR_H].avg_avl[3];
	err = hts221_update_avg(hw, HTS221_SENSOR_H, data);
	if (err < 0) {
		dev_err(hw->dev, "failed to set rh oversampling ratio\n");
		return err;
	}

	/* configure temperature sensor */
	err = hts221_parse_temp_caldata(hw);
	if (err < 0) {
		dev_err(hw->dev,
			"failed to get temperature calibration data\n");
		return err;
	}

	data = hts221_avg_list[HTS221_SENSOR_T].avg_avl[3];
	err = hts221_update_avg(hw, HTS221_SENSOR_T, data);
	if (err < 0) {
		dev_err(hw->dev,
			"failed to set temperature oversampling ratio\n");
		return err;
	}

	if (hw->irq > 0) {
		err = hts221_allocate_buffers(hw);
		if (err < 0)
			return err;

		err = hts221_allocate_trigger(hw);
		if (err)
			return err;
	}

	return devm_iio_device_register(hw->dev, iio_dev);
}
EXPORT_SYMBOL(hts221_probe);

static int __maybe_unused hts221_suspend(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct hts221_hw *hw = iio_priv(iio_dev);

	return regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
				  HTS221_ENABLE_MASK,
				  FIELD_PREP(HTS221_ENABLE_MASK, false));
}

static int __maybe_unused hts221_resume(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct hts221_hw *hw = iio_priv(iio_dev);
	int err = 0;

	if (hw->enabled)
		err = regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
					 HTS221_ENABLE_MASK,
					 FIELD_PREP(HTS221_ENABLE_MASK,
						    true));
	return err;
}

const struct dev_pm_ops hts221_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hts221_suspend, hts221_resume)
};
EXPORT_SYMBOL(hts221_pm_ops);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 sensor driver");
MODULE_LICENSE("GPL v2");
