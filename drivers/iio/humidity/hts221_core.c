// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics hts221 sensor driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/bitfield.h>

#include "hts221.h"

#define HTS221_REG_WHOAMI_ADDR		0x0f
#define HTS221_REG_WHOAMI_VAL		0xbc

#define HTS221_REG_CNTRL1_ADDR		0x20
#define HTS221_REG_CNTRL2_ADDR		0x21

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
	int avg_avl[HTS221_AVG_DEPTH];
};

static const struct hts221_odr hts221_odr_table[] = {
	{  1, 0x01 },	/* 1Hz */
	{  7, 0x02 },	/* 7Hz */
	{ 13, 0x03 },	/* 12.5Hz */
};

static const int hts221_odr_avail[] = { 1, 7, 13 };

static const struct hts221_avg hts221_avg_list[] = {
	{
		.addr = 0x10,
		.mask = 0x07,
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
		.addr = 0x10,
		.mask = 0x38,
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
		.address = 0x28,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available =
				BIT(IIO_CHAN_INFO_SAMP_FREQ),
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
		.address = 0x2a,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available =
				BIT(IIO_CHAN_INFO_SAMP_FREQ),
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
	struct device *dev = hw->dev;
	int err, data;

	err = regmap_read(hw->regmap, HTS221_REG_WHOAMI_ADDR, &data);
	if (err < 0)
		return dev_err_probe(dev, err, "failed to read whoami register\n");

	if (data != HTS221_REG_WHOAMI_VAL)
		dev_info(dev, "unexpected whoami 0x%02x, continuing\n", data);

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

static int hts221_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			*vals = hts221_avg_list[HTS221_SENSOR_H].avg_avl;
			*length = ARRAY_SIZE(hts221_avg_list[HTS221_SENSOR_H].avg_avl);
			break;
		case IIO_TEMP:
			*vals = hts221_avg_list[HTS221_SENSOR_T].avg_avl;
			*length = ARRAY_SIZE(hts221_avg_list[HTS221_SENSOR_T].avg_avl);
			break;
		default:
			return -EINVAL;
		}
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = hts221_odr_avail;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(hts221_odr_avail);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
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
	struct device *dev = hw->dev;
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

	if (cal_x1 == cal_x0)
		return dev_err_probe(dev, -EINVAL,
				     "invalid temperature calibration points (x0 %d, x1 %d)\n",
				     cal_x0, cal_x1);

	slope = &hw->sensors[HTS221_SENSOR_T].slope;
	b_gen = &hw->sensors[HTS221_SENSOR_T].b_gen;

	*slope = ((cal_y1 - cal_y0) * 8000) / (cal_x1 - cal_x0);
	if (!*slope)
		return dev_err_probe(dev, -EINVAL,
				     "invalid temperature calibration slope (y0 %d, y1 %d)\n",
				     cal_y0, cal_y1);

	*b_gen = (((s32)cal_x1 * cal_y0 - (s32)cal_x0 * cal_y1) * 1000) /
		 (cal_x1 - cal_x0);
	*b_gen *= 8;

	return 0;
}

static int hts221_parse_rh_caldata(struct hts221_hw *hw)
{
	struct device *dev = hw->dev;
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

	if (cal_x1 == cal_x0)
		return dev_err_probe(dev, -EINVAL,
				     "invalid rh calibration points (x0 %d, x1 %d)\n",
				     cal_x0, cal_x1);

	slope = &hw->sensors[HTS221_SENSOR_H].slope;
	b_gen = &hw->sensors[HTS221_SENSOR_H].b_gen;

	*slope = ((cal_y1 - cal_y0) * 8000) / (cal_x1 - cal_x0);
	if (!*slope)
		return dev_err_probe(dev, -EINVAL,
				     "invalid rh calibration slope (y0 %d, y1 %d)\n",
				     cal_y0, cal_y1);

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

static int __hts221_read_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *ch,
			     int *val, int *val2, long mask)
{
	struct hts221_hw *hw = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return hts221_read_oneshot(hw, ch->address, val);
	case IIO_CHAN_INFO_SCALE:
		return hts221_get_sensor_scale(hw, ch->type, val, val2);
	case IIO_CHAN_INFO_OFFSET:
		return hts221_get_sensor_offset(hw, ch->type, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = hw->odr;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		u8 idx;
		const struct hts221_avg *avg;

		switch (ch->type) {
		case IIO_HUMIDITYRELATIVE:
			avg = &hts221_avg_list[HTS221_SENSOR_H];
			idx = hw->sensors[HTS221_SENSOR_H].cur_avg_idx;
			*val = avg->avg_avl[idx];
			return IIO_VAL_INT;
		case IIO_TEMP:
			avg = &hts221_avg_list[HTS221_SENSOR_T];
			idx = hw->sensors[HTS221_SENSOR_T].cur_avg_idx;
			*val = avg->avg_avl[idx];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	}
	default:
		return -EINVAL;
	}
}

static int hts221_read_raw(struct iio_dev *iio_dev,
			   struct iio_chan_spec const *ch,
			   int *val, int *val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(iio_dev))
		return -EBUSY;

	ret = __hts221_read_raw(iio_dev, ch, val, val2, mask);

	iio_device_release_direct(iio_dev);

	return ret;
}

static int __hts221_write_raw(struct iio_dev *iio_dev,
			      struct iio_chan_spec const *chan,
			      int val, long mask)
{
	struct hts221_hw *hw = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hts221_update_odr(hw, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			return hts221_update_avg(hw, HTS221_SENSOR_H, val);
		case IIO_TEMP:
			return hts221_update_avg(hw, HTS221_SENSOR_T, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int hts221_write_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(iio_dev))
		return -EBUSY;

	ret = __hts221_write_raw(iio_dev, chan, val, mask);

	iio_device_release_direct(iio_dev);

	return ret;
}

static int hts221_validate_trigger(struct iio_dev *iio_dev,
				   struct iio_trigger *trig)
{
	struct hts221_hw *hw = iio_priv(iio_dev);

	return hw->trig == trig ? 0 : -EINVAL;
}

static const struct iio_info hts221_info = {
	.read_raw = hts221_read_raw,
	.write_raw = hts221_write_raw,
	.read_avail = hts221_read_avail,
	.validate_trigger = hts221_validate_trigger,
};

static const unsigned long hts221_scan_masks[] = {0x3, 0x0};

static int hts221_init_regulators(struct device *dev)
{
	int err;

	err = devm_regulator_get_enable(dev, "vdd");
	if (err)
		return dev_err_probe(dev, err, "failed to get vdd regulator\n");

	msleep(50);

	return 0;
}

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

	dev_set_drvdata(dev, iio_dev);

	hw = iio_priv(iio_dev);
	hw->name = name;
	hw->dev = dev;
	hw->irq = irq;
	hw->regmap = regmap;

	err = hts221_init_regulators(dev);
	if (err)
		return err;

	err = hts221_check_whoami(hw);
	if (err < 0)
		return err;

	iio_dev->modes = INDIO_DIRECT_MODE;
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
	if (err < 0)
		return dev_err_probe(dev, err, "failed to get rh calibration data\n");

	data = hts221_avg_list[HTS221_SENSOR_H].avg_avl[3];
	err = hts221_update_avg(hw, HTS221_SENSOR_H, data);
	if (err < 0)
		return dev_err_probe(dev, err, "failed to set rh oversampling ratio\n");

	/* configure temperature sensor */
	err = hts221_parse_temp_caldata(hw);
	if (err < 0)
		return dev_err_probe(dev, err, "failed to get temperature calibration data\n");

	data = hts221_avg_list[HTS221_SENSOR_T].avg_avl[3];
	err = hts221_update_avg(hw, HTS221_SENSOR_T, data);
	if (err < 0)
		return dev_err_probe(dev, err, "failed to set temperature oversampling ratio\n");

	if (hw->irq > 0) {
		err = hts221_allocate_buffers(iio_dev);
		if (err < 0)
			return err;

		err = hts221_allocate_trigger(iio_dev);
		if (err)
			return err;
	}

	return devm_iio_device_register(hw->dev, iio_dev);
}
EXPORT_SYMBOL_NS(hts221_probe, "IIO_HTS221");

static int hts221_suspend(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct hts221_hw *hw = iio_priv(iio_dev);

	return regmap_update_bits(hw->regmap, HTS221_REG_CNTRL1_ADDR,
				  HTS221_ENABLE_MASK,
				  FIELD_PREP(HTS221_ENABLE_MASK, false));
}

static int hts221_resume(struct device *dev)
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

EXPORT_NS_SIMPLE_DEV_PM_OPS(hts221_pm_ops, hts221_suspend, hts221_resume,
			    IIO_HTS221);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 sensor driver");
MODULE_LICENSE("GPL v2");
