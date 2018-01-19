/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/log2.h>

#include <dt-bindings/iio/qcom,spmi-vadc.h>

#include "qcom-vadc-common.h"

/* VADC register and bit definitions */
#define VADC_REVISION2				0x1
#define VADC_REVISION2_SUPPORTED_VADC		1

#define VADC_PERPH_TYPE				0x4
#define VADC_PERPH_TYPE_ADC			8

#define VADC_PERPH_SUBTYPE			0x5
#define VADC_PERPH_SUBTYPE_VADC			1

#define VADC_STATUS1				0x8
#define VADC_STATUS1_OP_MODE			4
#define VADC_STATUS1_REQ_STS			BIT(1)
#define VADC_STATUS1_EOC			BIT(0)
#define VADC_STATUS1_REQ_STS_EOC_MASK		0x3

#define VADC_MODE_CTL				0x40
#define VADC_OP_MODE_SHIFT			3
#define VADC_OP_MODE_NORMAL			0
#define VADC_AMUX_TRIM_EN			BIT(1)
#define VADC_ADC_TRIM_EN			BIT(0)

#define VADC_EN_CTL1				0x46
#define VADC_EN_CTL1_SET			BIT(7)

#define VADC_ADC_CH_SEL_CTL			0x48

#define VADC_ADC_DIG_PARAM			0x50
#define VADC_ADC_DIG_DEC_RATIO_SEL_SHIFT	2

#define VADC_HW_SETTLE_DELAY			0x51

#define VADC_CONV_REQ				0x52
#define VADC_CONV_REQ_SET			BIT(7)

#define VADC_FAST_AVG_CTL			0x5a
#define VADC_FAST_AVG_EN			0x5b
#define VADC_FAST_AVG_EN_SET			BIT(7)

#define VADC_ACCESS				0xd0
#define VADC_ACCESS_DATA			0xa5

#define VADC_PERH_RESET_CTL3			0xda
#define VADC_FOLLOW_WARM_RB			BIT(2)

#define VADC_DATA				0x60	/* 16 bits */

#define VADC_CHAN_MIN			VADC_USBIN
#define VADC_CHAN_MAX			VADC_LR_MUX3_BUF_PU1_PU2_XO_THERM

/**
 * struct vadc_channel_prop - VADC channel property.
 * @channel: channel number, refer to the channel list.
 * @calibration: calibration type.
 * @decimation: sampling rate supported for the channel.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 */
struct vadc_channel_prop {
	unsigned int channel;
	enum vadc_calibration calibration;
	unsigned int decimation;
	unsigned int prescale;
	unsigned int hw_settle_time;
	unsigned int avg_samples;
	enum vadc_scale_fn_type scale_fn_type;
};

/**
 * struct vadc_priv - VADC private structure.
 * @regmap: pointer to struct regmap.
 * @dev: pointer to struct device.
 * @base: base address for the ADC peripheral.
 * @nchannels: number of VADC channels.
 * @chan_props: array of VADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @are_ref_measured: are reference points measured.
 * @poll_eoc: use polling instead of interrupt.
 * @complete: VADC result notification after interrupt is received.
 * @graph: store parameters for calibration.
 * @lock: ADC lock for access to the peripheral.
 */
struct vadc_priv {
	struct regmap		 *regmap;
	struct device		 *dev;
	u16			 base;
	unsigned int		 nchannels;
	struct vadc_channel_prop *chan_props;
	struct iio_chan_spec	 *iio_chans;
	bool			 are_ref_measured;
	bool			 poll_eoc;
	struct completion	 complete;
	struct vadc_linear_graph graph[2];
	struct mutex		 lock;
};

static const struct vadc_prescale_ratio vadc_prescale_ratios[] = {
	{.num =  1, .den =  1},
	{.num =  1, .den =  3},
	{.num =  1, .den =  4},
	{.num =  1, .den =  6},
	{.num =  1, .den = 20},
	{.num =  1, .den =  8},
	{.num = 10, .den = 81},
	{.num =  1, .den = 10}
};

static int vadc_read(struct vadc_priv *vadc, u16 offset, u8 *data)
{
	return regmap_bulk_read(vadc->regmap, vadc->base + offset, data, 1);
}

static int vadc_write(struct vadc_priv *vadc, u16 offset, u8 data)
{
	return regmap_write(vadc->regmap, vadc->base + offset, data);
}

static int vadc_reset(struct vadc_priv *vadc)
{
	u8 data;
	int ret;

	ret = vadc_write(vadc, VADC_ACCESS, VADC_ACCESS_DATA);
	if (ret)
		return ret;

	ret = vadc_read(vadc, VADC_PERH_RESET_CTL3, &data);
	if (ret)
		return ret;

	ret = vadc_write(vadc, VADC_ACCESS, VADC_ACCESS_DATA);
	if (ret)
		return ret;

	data |= VADC_FOLLOW_WARM_RB;

	return vadc_write(vadc, VADC_PERH_RESET_CTL3, data);
}

static int vadc_set_state(struct vadc_priv *vadc, bool state)
{
	return vadc_write(vadc, VADC_EN_CTL1, state ? VADC_EN_CTL1_SET : 0);
}

static void vadc_show_status(struct vadc_priv *vadc)
{
	u8 mode, sta1, chan, dig, en, req;
	int ret;

	ret = vadc_read(vadc, VADC_MODE_CTL, &mode);
	if (ret)
		return;

	ret = vadc_read(vadc, VADC_ADC_DIG_PARAM, &dig);
	if (ret)
		return;

	ret = vadc_read(vadc, VADC_ADC_CH_SEL_CTL, &chan);
	if (ret)
		return;

	ret = vadc_read(vadc, VADC_CONV_REQ, &req);
	if (ret)
		return;

	ret = vadc_read(vadc, VADC_STATUS1, &sta1);
	if (ret)
		return;

	ret = vadc_read(vadc, VADC_EN_CTL1, &en);
	if (ret)
		return;

	dev_err(vadc->dev,
		"mode:%02x en:%02x chan:%02x dig:%02x req:%02x sta1:%02x\n",
		mode, en, chan, dig, req, sta1);
}

static int vadc_configure(struct vadc_priv *vadc,
			  struct vadc_channel_prop *prop)
{
	u8 decimation, mode_ctrl;
	int ret;

	/* Mode selection */
	mode_ctrl = (VADC_OP_MODE_NORMAL << VADC_OP_MODE_SHIFT) |
		     VADC_ADC_TRIM_EN | VADC_AMUX_TRIM_EN;
	ret = vadc_write(vadc, VADC_MODE_CTL, mode_ctrl);
	if (ret)
		return ret;

	/* Channel selection */
	ret = vadc_write(vadc, VADC_ADC_CH_SEL_CTL, prop->channel);
	if (ret)
		return ret;

	/* Digital parameter setup */
	decimation = prop->decimation << VADC_ADC_DIG_DEC_RATIO_SEL_SHIFT;
	ret = vadc_write(vadc, VADC_ADC_DIG_PARAM, decimation);
	if (ret)
		return ret;

	/* HW settle time delay */
	ret = vadc_write(vadc, VADC_HW_SETTLE_DELAY, prop->hw_settle_time);
	if (ret)
		return ret;

	ret = vadc_write(vadc, VADC_FAST_AVG_CTL, prop->avg_samples);
	if (ret)
		return ret;

	if (prop->avg_samples)
		ret = vadc_write(vadc, VADC_FAST_AVG_EN, VADC_FAST_AVG_EN_SET);
	else
		ret = vadc_write(vadc, VADC_FAST_AVG_EN, 0);

	return ret;
}

static int vadc_poll_wait_eoc(struct vadc_priv *vadc, unsigned int interval_us)
{
	unsigned int count, retry;
	u8 sta1;
	int ret;

	retry = interval_us / VADC_CONV_TIME_MIN_US;

	for (count = 0; count < retry; count++) {
		ret = vadc_read(vadc, VADC_STATUS1, &sta1);
		if (ret)
			return ret;

		sta1 &= VADC_STATUS1_REQ_STS_EOC_MASK;
		if (sta1 == VADC_STATUS1_EOC)
			return 0;

		usleep_range(VADC_CONV_TIME_MIN_US, VADC_CONV_TIME_MAX_US);
	}

	vadc_show_status(vadc);

	return -ETIMEDOUT;
}

static int vadc_read_result(struct vadc_priv *vadc, u16 *data)
{
	int ret;

	ret = regmap_bulk_read(vadc->regmap, vadc->base + VADC_DATA, data, 2);
	if (ret)
		return ret;

	*data = clamp_t(u16, *data, VADC_MIN_ADC_CODE, VADC_MAX_ADC_CODE);

	return 0;
}

static struct vadc_channel_prop *vadc_get_channel(struct vadc_priv *vadc,
						  unsigned int num)
{
	unsigned int i;

	for (i = 0; i < vadc->nchannels; i++)
		if (vadc->chan_props[i].channel == num)
			return &vadc->chan_props[i];

	dev_dbg(vadc->dev, "no such channel %02x\n", num);

	return NULL;
}

static int vadc_do_conversion(struct vadc_priv *vadc,
			      struct vadc_channel_prop *prop, u16 *data)
{
	unsigned int timeout;
	int ret;

	mutex_lock(&vadc->lock);

	ret = vadc_configure(vadc, prop);
	if (ret)
		goto unlock;

	if (!vadc->poll_eoc)
		reinit_completion(&vadc->complete);

	ret = vadc_set_state(vadc, true);
	if (ret)
		goto unlock;

	ret = vadc_write(vadc, VADC_CONV_REQ, VADC_CONV_REQ_SET);
	if (ret)
		goto err_disable;

	timeout = BIT(prop->avg_samples) * VADC_CONV_TIME_MIN_US * 2;

	if (vadc->poll_eoc) {
		ret = vadc_poll_wait_eoc(vadc, timeout);
	} else {
		ret = wait_for_completion_timeout(&vadc->complete, timeout);
		if (!ret) {
			ret = -ETIMEDOUT;
			goto err_disable;
		}

		/* Double check conversion status */
		ret = vadc_poll_wait_eoc(vadc, VADC_CONV_TIME_MIN_US);
		if (ret)
			goto err_disable;
	}

	ret = vadc_read_result(vadc, data);

err_disable:
	vadc_set_state(vadc, false);
	if (ret)
		dev_err(vadc->dev, "conversion failed\n");
unlock:
	mutex_unlock(&vadc->lock);
	return ret;
}

static int vadc_measure_ref_points(struct vadc_priv *vadc)
{
	struct vadc_channel_prop *prop;
	u16 read_1, read_2;
	int ret;

	vadc->graph[VADC_CALIB_RATIOMETRIC].dx = VADC_RATIOMETRIC_RANGE;
	vadc->graph[VADC_CALIB_ABSOLUTE].dx = VADC_ABSOLUTE_RANGE_UV;

	prop = vadc_get_channel(vadc, VADC_REF_1250MV);
	ret = vadc_do_conversion(vadc, prop, &read_1);
	if (ret)
		goto err;

	/* Try with buffered 625mV channel first */
	prop = vadc_get_channel(vadc, VADC_SPARE1);
	if (!prop)
		prop = vadc_get_channel(vadc, VADC_REF_625MV);

	ret = vadc_do_conversion(vadc, prop, &read_2);
	if (ret)
		goto err;

	if (read_1 == read_2) {
		ret = -EINVAL;
		goto err;
	}

	vadc->graph[VADC_CALIB_ABSOLUTE].dy = read_1 - read_2;
	vadc->graph[VADC_CALIB_ABSOLUTE].gnd = read_2;

	/* Ratiometric calibration */
	prop = vadc_get_channel(vadc, VADC_VDD_VADC);
	ret = vadc_do_conversion(vadc, prop, &read_1);
	if (ret)
		goto err;

	prop = vadc_get_channel(vadc, VADC_GND_REF);
	ret = vadc_do_conversion(vadc, prop, &read_2);
	if (ret)
		goto err;

	if (read_1 == read_2) {
		ret = -EINVAL;
		goto err;
	}

	vadc->graph[VADC_CALIB_RATIOMETRIC].dy = read_1 - read_2;
	vadc->graph[VADC_CALIB_RATIOMETRIC].gnd = read_2;
err:
	if (ret)
		dev_err(vadc->dev, "measure reference points failed\n");

	return ret;
}

static int vadc_prescaling_from_dt(u32 num, u32 den)
{
	unsigned int pre;

	for (pre = 0; pre < ARRAY_SIZE(vadc_prescale_ratios); pre++)
		if (vadc_prescale_ratios[pre].num == num &&
		    vadc_prescale_ratios[pre].den == den)
			break;

	if (pre == ARRAY_SIZE(vadc_prescale_ratios))
		return -EINVAL;

	return pre;
}

static int vadc_hw_settle_time_from_dt(u32 value)
{
	if ((value <= 1000 && value % 100) || (value > 1000 && value % 2000))
		return -EINVAL;

	if (value <= 1000)
		value /= 100;
	else
		value = value / 2000 + 10;

	return value;
}

static int vadc_avg_samples_from_dt(u32 value)
{
	if (!is_power_of_2(value) || value > VADC_AVG_SAMPLES_MAX)
		return -EINVAL;

	return __ffs64(value);
}

static int vadc_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct vadc_priv *vadc = iio_priv(indio_dev);
	struct vadc_channel_prop *prop;
	u16 adc_code;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		prop = &vadc->chan_props[chan->address];
		ret = vadc_do_conversion(vadc, prop, &adc_code);
		if (ret)
			break;

		ret = qcom_vadc_scale(prop->scale_fn_type,
				&vadc->graph[prop->calibration],
				&vadc_prescale_ratios[prop->prescale],
				(prop->calibration == VADC_CALIB_ABSOLUTE),
				adc_code, val);
		if (ret)
			break;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		prop = &vadc->chan_props[chan->address];
		ret = vadc_do_conversion(vadc, prop, &adc_code);
		if (ret)
			break;

		*val = (int)adc_code;
		return IIO_VAL_INT;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int vadc_of_xlate(struct iio_dev *indio_dev,
			 const struct of_phandle_args *iiospec)
{
	struct vadc_priv *vadc = iio_priv(indio_dev);
	unsigned int i;

	for (i = 0; i < vadc->nchannels; i++)
		if (vadc->iio_chans[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static const struct iio_info vadc_info = {
	.read_raw = vadc_read_raw,
	.of_xlate = vadc_of_xlate,
};

struct vadc_channels {
	const char *datasheet_name;
	unsigned int prescale_index;
	enum iio_chan_type type;
	long info_mask;
	enum vadc_scale_fn_type scale_fn_type;
};

#define VADC_CHAN(_dname, _type, _mask, _pre, _scale)			\
	[VADC_##_dname] = {						\
		.datasheet_name = __stringify(_dname),			\
		.prescale_index = _pre,					\
		.type = _type,						\
		.info_mask = _mask,					\
		.scale_fn_type = _scale					\
	},								\

#define VADC_NO_CHAN(_dname, _type, _mask, _pre)			\
	[VADC_##_dname] = {						\
		.datasheet_name = __stringify(_dname),			\
		.prescale_index = _pre,					\
		.type = _type,						\
		.info_mask = _mask					\
	},

#define VADC_CHAN_TEMP(_dname, _pre, _scale)				\
	VADC_CHAN(_dname, IIO_TEMP,					\
		BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED),	\
		_pre, _scale)						\

#define VADC_CHAN_VOLT(_dname, _pre, _scale)				\
	VADC_CHAN(_dname, IIO_VOLTAGE,					\
		  BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED),\
		  _pre, _scale)						\

#define VADC_CHAN_NO_SCALE(_dname, _pre)				\
	VADC_NO_CHAN(_dname, IIO_VOLTAGE,				\
		  BIT(IIO_CHAN_INFO_RAW),				\
		  _pre)							\

/*
 * The array represents all possible ADC channels found in the supported PMICs.
 * Every index in the array is equal to the channel number per datasheet. The
 * gaps in the array should be treated as reserved channels.
 */
static const struct vadc_channels vadc_chans[] = {
	VADC_CHAN_VOLT(USBIN, 4, SCALE_DEFAULT)
	VADC_CHAN_VOLT(DCIN, 4, SCALE_DEFAULT)
	VADC_CHAN_NO_SCALE(VCHG_SNS, 3)
	VADC_CHAN_NO_SCALE(SPARE1_03, 1)
	VADC_CHAN_NO_SCALE(USB_ID_MV, 1)
	VADC_CHAN_VOLT(VCOIN, 1, SCALE_DEFAULT)
	VADC_CHAN_NO_SCALE(VBAT_SNS, 1)
	VADC_CHAN_VOLT(VSYS, 1, SCALE_DEFAULT)
	VADC_CHAN_TEMP(DIE_TEMP, 0, SCALE_PMIC_THERM)
	VADC_CHAN_VOLT(REF_625MV, 0, SCALE_DEFAULT)
	VADC_CHAN_VOLT(REF_1250MV, 0, SCALE_DEFAULT)
	VADC_CHAN_NO_SCALE(CHG_TEMP, 0)
	VADC_CHAN_NO_SCALE(SPARE1, 0)
	VADC_CHAN_TEMP(SPARE2, 0, SCALE_PMI_CHG_TEMP)
	VADC_CHAN_VOLT(GND_REF, 0, SCALE_DEFAULT)
	VADC_CHAN_VOLT(VDD_VADC, 0, SCALE_DEFAULT)

	VADC_CHAN_NO_SCALE(P_MUX1_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX2_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX3_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX4_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX5_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX6_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX7_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX8_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX9_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX10_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX11_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX12_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX13_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX14_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX15_1_1, 0)
	VADC_CHAN_NO_SCALE(P_MUX16_1_1, 0)

	VADC_CHAN_NO_SCALE(P_MUX1_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX2_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX3_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX4_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX5_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX6_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX7_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX8_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX9_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX10_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX11_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX12_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX13_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX14_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX15_1_3, 1)
	VADC_CHAN_NO_SCALE(P_MUX16_1_3, 1)

	VADC_CHAN_NO_SCALE(LR_MUX1_BAT_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX2_BAT_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_XO_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX4_AMUX_THM1, 0)
	VADC_CHAN_NO_SCALE(LR_MUX5_AMUX_THM2, 0)
	VADC_CHAN_NO_SCALE(LR_MUX6_AMUX_THM3, 0)
	VADC_CHAN_NO_SCALE(LR_MUX7_HW_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX8_AMUX_THM4, 0)
	VADC_CHAN_NO_SCALE(LR_MUX9_AMUX_THM5, 0)
	VADC_CHAN_NO_SCALE(LR_MUX10_USB_ID, 0)
	VADC_CHAN_NO_SCALE(AMUX_PU1, 0)
	VADC_CHAN_NO_SCALE(AMUX_PU2, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_BUF_XO_THERM, 0)

	VADC_CHAN_NO_SCALE(LR_MUX1_PU1_BAT_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX2_PU1_BAT_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_PU1_XO_THERM, 0)
	VADC_CHAN_TEMP(LR_MUX4_PU1_AMUX_THM1, 0, SCALE_THERM_100K_PULLUP)
	VADC_CHAN_TEMP(LR_MUX5_PU1_AMUX_THM2, 0, SCALE_THERM_100K_PULLUP)
	VADC_CHAN_TEMP(LR_MUX6_PU1_AMUX_THM3, 0, SCALE_THERM_100K_PULLUP)
	VADC_CHAN_NO_SCALE(LR_MUX7_PU1_AMUX_HW_ID, 0)
	VADC_CHAN_TEMP(LR_MUX8_PU1_AMUX_THM4, 0, SCALE_THERM_100K_PULLUP)
	VADC_CHAN_TEMP(LR_MUX9_PU1_AMUX_THM5, 0, SCALE_THERM_100K_PULLUP)
	VADC_CHAN_NO_SCALE(LR_MUX10_PU1_AMUX_USB_ID, 0)
	VADC_CHAN_TEMP(LR_MUX3_BUF_PU1_XO_THERM, 0, SCALE_XOTHERM)

	VADC_CHAN_NO_SCALE(LR_MUX1_PU2_BAT_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX2_PU2_BAT_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_PU2_XO_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX4_PU2_AMUX_THM1, 0)
	VADC_CHAN_NO_SCALE(LR_MUX5_PU2_AMUX_THM2, 0)
	VADC_CHAN_NO_SCALE(LR_MUX6_PU2_AMUX_THM3, 0)
	VADC_CHAN_NO_SCALE(LR_MUX7_PU2_AMUX_HW_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX8_PU2_AMUX_THM4, 0)
	VADC_CHAN_NO_SCALE(LR_MUX9_PU2_AMUX_THM5, 0)
	VADC_CHAN_NO_SCALE(LR_MUX10_PU2_AMUX_USB_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_BUF_PU2_XO_THERM, 0)

	VADC_CHAN_NO_SCALE(LR_MUX1_PU1_PU2_BAT_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX2_PU1_PU2_BAT_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_PU1_PU2_XO_THERM, 0)
	VADC_CHAN_NO_SCALE(LR_MUX4_PU1_PU2_AMUX_THM1, 0)
	VADC_CHAN_NO_SCALE(LR_MUX5_PU1_PU2_AMUX_THM2, 0)
	VADC_CHAN_NO_SCALE(LR_MUX6_PU1_PU2_AMUX_THM3, 0)
	VADC_CHAN_NO_SCALE(LR_MUX7_PU1_PU2_AMUX_HW_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX8_PU1_PU2_AMUX_THM4, 0)
	VADC_CHAN_NO_SCALE(LR_MUX9_PU1_PU2_AMUX_THM5, 0)
	VADC_CHAN_NO_SCALE(LR_MUX10_PU1_PU2_AMUX_USB_ID, 0)
	VADC_CHAN_NO_SCALE(LR_MUX3_BUF_PU1_PU2_XO_THERM, 0)
};

static int vadc_get_dt_channel_data(struct device *dev,
				    struct vadc_channel_prop *prop,
				    struct device_node *node)
{
	const char *name = node->name;
	u32 chan, value, varr[2];
	int ret;

	ret = of_property_read_u32(node, "reg", &chan);
	if (ret) {
		dev_err(dev, "invalid channel number %s\n", name);
		return ret;
	}

	if (chan > VADC_CHAN_MAX || chan < VADC_CHAN_MIN) {
		dev_err(dev, "%s invalid channel number %d\n", name, chan);
		return -EINVAL;
	}

	/* the channel has DT description */
	prop->channel = chan;

	ret = of_property_read_u32(node, "qcom,decimation", &value);
	if (!ret) {
		ret = qcom_vadc_decimation_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "%02x invalid decimation %d\n",
				chan, value);
			return ret;
		}
		prop->decimation = ret;
	} else {
		prop->decimation = VADC_DEF_DECIMATION;
	}

	ret = of_property_read_u32_array(node, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = vadc_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0) {
			dev_err(dev, "%02x invalid pre-scaling <%d %d>\n",
				chan, varr[0], varr[1]);
			return ret;
		}
		prop->prescale = ret;
	} else {
		prop->prescale = vadc_chans[prop->channel].prescale_index;
	}

	ret = of_property_read_u32(node, "qcom,hw-settle-time", &value);
	if (!ret) {
		ret = vadc_hw_settle_time_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "%02x invalid hw-settle-time %d us\n",
				chan, value);
			return ret;
		}
		prop->hw_settle_time = ret;
	} else {
		prop->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	}

	ret = of_property_read_u32(node, "qcom,avg-samples", &value);
	if (!ret) {
		ret = vadc_avg_samples_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "%02x invalid avg-samples %d\n",
				chan, value);
			return ret;
		}
		prop->avg_samples = ret;
	} else {
		prop->avg_samples = VADC_DEF_AVG_SAMPLES;
	}

	if (of_property_read_bool(node, "qcom,ratiometric"))
		prop->calibration = VADC_CALIB_RATIOMETRIC;
	else
		prop->calibration = VADC_CALIB_ABSOLUTE;

	dev_dbg(dev, "%02x name %s\n", chan, name);

	return 0;
}

static int vadc_get_dt_data(struct vadc_priv *vadc, struct device_node *node)
{
	const struct vadc_channels *vadc_chan;
	struct iio_chan_spec *iio_chan;
	struct vadc_channel_prop prop;
	struct device_node *child;
	unsigned int index = 0;
	int ret;

	vadc->nchannels = of_get_available_child_count(node);
	if (!vadc->nchannels)
		return -EINVAL;

	vadc->iio_chans = devm_kcalloc(vadc->dev, vadc->nchannels,
				       sizeof(*vadc->iio_chans), GFP_KERNEL);
	if (!vadc->iio_chans)
		return -ENOMEM;

	vadc->chan_props = devm_kcalloc(vadc->dev, vadc->nchannels,
					sizeof(*vadc->chan_props), GFP_KERNEL);
	if (!vadc->chan_props)
		return -ENOMEM;

	iio_chan = vadc->iio_chans;

	for_each_available_child_of_node(node, child) {
		ret = vadc_get_dt_channel_data(vadc->dev, &prop, child);
		if (ret) {
			of_node_put(child);
			return ret;
		}

		prop.scale_fn_type = vadc_chans[prop.channel].scale_fn_type;
		vadc->chan_props[index] = prop;

		vadc_chan = &vadc_chans[prop.channel];

		iio_chan->channel = prop.channel;
		iio_chan->datasheet_name = vadc_chan->datasheet_name;
		iio_chan->info_mask_separate = vadc_chan->info_mask;
		iio_chan->type = vadc_chan->type;
		iio_chan->indexed = 1;
		iio_chan->address = index++;

		iio_chan++;
	}

	/* These channels are mandatory, they are used as reference points */
	if (!vadc_get_channel(vadc, VADC_REF_1250MV)) {
		dev_err(vadc->dev, "Please define 1.25V channel\n");
		return -ENODEV;
	}

	if (!vadc_get_channel(vadc, VADC_REF_625MV)) {
		dev_err(vadc->dev, "Please define 0.625V channel\n");
		return -ENODEV;
	}

	if (!vadc_get_channel(vadc, VADC_VDD_VADC)) {
		dev_err(vadc->dev, "Please define VDD channel\n");
		return -ENODEV;
	}

	if (!vadc_get_channel(vadc, VADC_GND_REF)) {
		dev_err(vadc->dev, "Please define GND channel\n");
		return -ENODEV;
	}

	return 0;
}

static irqreturn_t vadc_isr(int irq, void *dev_id)
{
	struct vadc_priv *vadc = dev_id;

	complete(&vadc->complete);

	return IRQ_HANDLED;
}

static int vadc_check_revision(struct vadc_priv *vadc)
{
	u8 val;
	int ret;

	ret = vadc_read(vadc, VADC_PERPH_TYPE, &val);
	if (ret)
		return ret;

	if (val < VADC_PERPH_TYPE_ADC) {
		dev_err(vadc->dev, "%d is not ADC\n", val);
		return -ENODEV;
	}

	ret = vadc_read(vadc, VADC_PERPH_SUBTYPE, &val);
	if (ret)
		return ret;

	if (val < VADC_PERPH_SUBTYPE_VADC) {
		dev_err(vadc->dev, "%d is not VADC\n", val);
		return -ENODEV;
	}

	ret = vadc_read(vadc, VADC_REVISION2, &val);
	if (ret)
		return ret;

	if (val < VADC_REVISION2_SUPPORTED_VADC) {
		dev_err(vadc->dev, "revision %d not supported\n", val);
		return -ENODEV;
	}

	return 0;
}

static int vadc_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct vadc_priv *vadc;
	struct regmap *regmap;
	int ret, irq_eoc;
	u32 reg;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = of_property_read_u32(node, "reg", &reg);
	if (ret < 0)
		return ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*vadc));
	if (!indio_dev)
		return -ENOMEM;

	vadc = iio_priv(indio_dev);
	vadc->regmap = regmap;
	vadc->dev = dev;
	vadc->base = reg;
	vadc->are_ref_measured = false;
	init_completion(&vadc->complete);
	mutex_init(&vadc->lock);

	ret = vadc_check_revision(vadc);
	if (ret)
		return ret;

	ret = vadc_get_dt_data(vadc, node);
	if (ret)
		return ret;

	irq_eoc = platform_get_irq(pdev, 0);
	if (irq_eoc < 0) {
		if (irq_eoc == -EPROBE_DEFER || irq_eoc == -EINVAL)
			return irq_eoc;
		vadc->poll_eoc = true;
	} else {
		ret = devm_request_irq(dev, irq_eoc, vadc_isr, 0,
				       "spmi-vadc", vadc);
		if (ret)
			return ret;
	}

	ret = vadc_reset(vadc);
	if (ret) {
		dev_err(dev, "reset failed\n");
		return ret;
	}

	ret = vadc_measure_ref_points(vadc);
	if (ret)
		return ret;

	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = node;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &vadc_info;
	indio_dev->channels = vadc->iio_chans;
	indio_dev->num_channels = vadc->nchannels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id vadc_match_table[] = {
	{ .compatible = "qcom,spmi-vadc" },
	{ }
};
MODULE_DEVICE_TABLE(of, vadc_match_table);

static struct platform_driver vadc_driver = {
	.driver = {
		   .name = "qcom-spmi-vadc",
		   .of_match_table = vadc_match_table,
	},
	.probe = vadc_probe,
};
module_platform_driver(vadc_driver);

MODULE_ALIAS("platform:qcom-spmi-vadc");
MODULE_DESCRIPTION("Qualcomm SPMI PMIC voltage ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@mm-sol.com>");
MODULE_AUTHOR("Ivan T. Ivanov <iivanov@mm-sol.com>");
