// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/iio/qcom,spmi-vadc.h>

#define ADC5_USR_REVISION1			0x0
#define ADC5_USR_STATUS1			0x8
#define ADC5_USR_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_USR_STATUS1_REQ_STS		BIT(1)
#define ADC5_USR_STATUS1_EOC			BIT(0)
#define ADC5_USR_STATUS1_REQ_STS_EOC_MASK	0x3

#define ADC5_USR_STATUS2			0x9
#define ADC5_USR_STATUS2_CONV_SEQ_MASK		0x70
#define ADC5_USR_STATUS2_CONV_SEQ_MASK_SHIFT	0x5

#define ADC5_USR_IBAT_MEAS			0xf
#define ADC5_USR_IBAT_MEAS_SUPPORTED		BIT(0)

#define ADC5_USR_DIG_PARAM			0x42
#define ADC5_USR_DIG_PARAM_CAL_VAL		BIT(6)
#define ADC5_USR_DIG_PARAM_CAL_VAL_SHIFT	6
#define ADC5_USR_DIG_PARAM_CAL_SEL		0x30
#define ADC5_USR_DIG_PARAM_CAL_SEL_SHIFT	4
#define ADC5_USR_DIG_PARAM_DEC_RATIO_SEL	0xc
#define ADC5_USR_DIG_PARAM_DEC_RATIO_SEL_SHIFT	2

#define ADC5_USR_FAST_AVG_CTL			0x43
#define ADC5_USR_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK	0x7

#define ADC5_USR_CH_SEL_CTL			0x44

#define ADC5_USR_DELAY_CTL			0x45
#define ADC5_USR_HW_SETTLE_DELAY_MASK		0xf

#define ADC5_USR_EN_CTL1			0x46
#define ADC5_USR_EN_CTL1_ADC_EN			BIT(7)

#define ADC5_USR_CONV_REQ			0x47
#define ADC5_USR_CONV_REQ_REQ			BIT(7)

#define ADC5_USR_DATA0				0x50

#define ADC5_USR_DATA1				0x51

#define ADC5_USR_IBAT_DATA0			0x52

#define ADC5_USR_IBAT_DATA1			0x53

#define ADC_CHANNEL_OFFSET			0x8
#define ADC_CHANNEL_MASK			GENMASK(7, 0)

/*
 * Conversion time varies based on the decimation, clock rate, fast average
 * samples and measurements queued across different VADC peripherals.
 * Set the timeout to a max of 100ms.
 */
#define ADC5_CONV_TIME_MIN_US			263
#define ADC5_CONV_TIME_MAX_US			264
#define ADC5_CONV_TIME_RETRY			400
#define ADC5_CONV_TIMEOUT			msecs_to_jiffies(100)

/* Digital version >= 5.3 supports hw_settle_2 */
#define ADC5_HW_SETTLE_DIFF_MINOR		3
#define ADC5_HW_SETTLE_DIFF_MAJOR		5

/* For PMIC7 */
#define ADC_APP_SID				0x40
#define ADC_APP_SID_MASK			GENMASK(3, 0)
#define ADC7_CONV_TIMEOUT			msecs_to_jiffies(10)

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL
};

enum adc5_cal_val {
	ADC5_TIMER_CAL = 0,
	ADC5_NEW_CAL
};

/**
 * struct adc5_channel_prop - ADC channel property.
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @cal_val: calibration value
 * @decimation: sampling rate supported for the channel.
 * @sid: slave id of PMIC owning the channel, for PMIC7.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 * @channel_name: Channel name used in device tree.
 */
struct adc5_channel_prop {
	unsigned int		channel;
	enum adc5_cal_method	cal_method;
	enum adc5_cal_val	cal_val;
	unsigned int		decimation;
	unsigned int		sid;
	unsigned int		prescale;
	unsigned int		hw_settle_time;
	unsigned int		avg_samples;
	enum vadc_scale_fn_type	scale_fn_type;
	const char		*channel_name;
};

/**
 * struct adc5_chip - ADC private structure.
 * @regmap: SPMI ADC5 peripheral register map field.
 * @dev: SPMI ADC5 device.
 * @base: base address for the ADC peripheral.
 * @nchannels: number of ADC channels.
 * @chan_props: array of ADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @poll_eoc: use polling instead of interrupt.
 * @complete: ADC result notification after interrupt is received.
 * @lock: ADC lock for access to the peripheral.
 * @data: software configuration data.
 */
struct adc5_chip {
	struct regmap		*regmap;
	struct device		*dev;
	u16			base;
	unsigned int		nchannels;
	struct adc5_channel_prop	*chan_props;
	struct iio_chan_spec	*iio_chans;
	bool			poll_eoc;
	struct completion	complete;
	struct mutex		lock;
	const struct adc5_data	*data;
};

static int adc5_read(struct adc5_chip *adc, u16 offset, u8 *data, int len)
{
	return regmap_bulk_read(adc->regmap, adc->base + offset, data, len);
}

static int adc5_write(struct adc5_chip *adc, u16 offset, u8 *data, int len)
{
	return regmap_bulk_write(adc->regmap, adc->base + offset, data, len);
}

static int adc5_masked_write(struct adc5_chip *adc, u16 offset, u8 mask, u8 val)
{
	return regmap_update_bits(adc->regmap, adc->base + offset, mask, val);
}

static int adc5_read_voltage_data(struct adc5_chip *adc, u16 *data)
{
	int ret;
	u8 rslt_lsb, rslt_msb;

	ret = adc5_read(adc, ADC5_USR_DATA0, &rslt_lsb, sizeof(rslt_lsb));
	if (ret)
		return ret;

	ret = adc5_read(adc, ADC5_USR_DATA1, &rslt_msb, sizeof(rslt_lsb));
	if (ret)
		return ret;

	*data = (rslt_msb << 8) | rslt_lsb;

	if (*data == ADC5_USR_DATA_CHECK) {
		dev_err(adc->dev, "Invalid data:0x%x\n", *data);
		return -EINVAL;
	}

	dev_dbg(adc->dev, "voltage raw code:0x%x\n", *data);

	return 0;
}

static int adc5_poll_wait_eoc(struct adc5_chip *adc)
{
	unsigned int count, retry = ADC5_CONV_TIME_RETRY;
	u8 status1;
	int ret;

	for (count = 0; count < retry; count++) {
		ret = adc5_read(adc, ADC5_USR_STATUS1, &status1,
							sizeof(status1));
		if (ret)
			return ret;

		status1 &= ADC5_USR_STATUS1_REQ_STS_EOC_MASK;
		if (status1 == ADC5_USR_STATUS1_EOC)
			return 0;

		usleep_range(ADC5_CONV_TIME_MIN_US, ADC5_CONV_TIME_MAX_US);
	}

	return -ETIMEDOUT;
}

static void adc5_update_dig_param(struct adc5_chip *adc,
			struct adc5_channel_prop *prop, u8 *data)
{
	/* Update calibration value */
	*data &= ~ADC5_USR_DIG_PARAM_CAL_VAL;
	*data |= (prop->cal_val << ADC5_USR_DIG_PARAM_CAL_VAL_SHIFT);

	/* Update calibration select */
	*data &= ~ADC5_USR_DIG_PARAM_CAL_SEL;
	*data |= (prop->cal_method << ADC5_USR_DIG_PARAM_CAL_SEL_SHIFT);

	/* Update decimation ratio select */
	*data &= ~ADC5_USR_DIG_PARAM_DEC_RATIO_SEL;
	*data |= (prop->decimation << ADC5_USR_DIG_PARAM_DEC_RATIO_SEL_SHIFT);
}

static int adc5_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 buf[6];

	/* Read registers 0x42 through 0x46 */
	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Digital param selection */
	adc5_update_dig_param(adc, prop, &buf[0]);

	/* Update fast average sample value */
	buf[1] &= (u8) ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples;

	/* Select ADC channel */
	buf[2] = prop->channel;

	/* Select HW settle delay for channel */
	buf[3] &= (u8) ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;

	/* Select ADC enable */
	buf[4] |= ADC5_USR_EN_CTL1_ADC_EN;

	/* Select CONV request */
	buf[5] |= ADC5_USR_CONV_REQ_REQ;

	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);

	return adc5_write(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
}

static int adc7_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 conv_req = 0, buf[4];

	ret = adc5_masked_write(adc, ADC_APP_SID, ADC_APP_SID_MASK, prop->sid);
	if (ret)
		return ret;

	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Digital param selection */
	adc5_update_dig_param(adc, prop, &buf[0]);

	/* Update fast average sample value */
	buf[1] &= ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples;

	/* Select ADC channel */
	buf[2] = prop->channel;

	/* Select HW settle delay for channel */
	buf[3] &= ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;

	/* Select CONV request */
	conv_req = ADC5_USR_CONV_REQ_REQ;

	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);

	ret = adc5_write(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	return adc5_write(adc, ADC5_USR_CONV_REQ, &conv_req, 1);
}

static int adc5_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur)
{
	int ret;

	mutex_lock(&adc->lock);

	ret = adc5_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		goto unlock;
	}

	if (adc->poll_eoc) {
		ret = adc5_poll_wait_eoc(adc);
		if (ret) {
			dev_err(adc->dev, "EOC bit not set\n");
			goto unlock;
		}
	} else {
		ret = wait_for_completion_timeout(&adc->complete,
							ADC5_CONV_TIMEOUT);
		if (!ret) {
			dev_dbg(adc->dev, "Did not get completion timeout.\n");
			ret = adc5_poll_wait_eoc(adc);
			if (ret) {
				dev_err(adc->dev, "EOC bit not set\n");
				goto unlock;
			}
		}
	}

	ret = adc5_read_voltage_data(adc, data_volt);
unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

static int adc7_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur)
{
	int ret;
	u8 status;

	mutex_lock(&adc->lock);

	ret = adc7_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		goto unlock;
	}

	/* No support for polling mode at present */
	wait_for_completion_timeout(&adc->complete, ADC7_CONV_TIMEOUT);

	ret = adc5_read(adc, ADC5_USR_STATUS1, &status, 1);
	if (ret)
		goto unlock;

	if (status & ADC5_USR_STATUS1_CONV_FAULT) {
		dev_err(adc->dev, "Unexpected conversion fault\n");
		ret = -EIO;
		goto unlock;
	}

	ret = adc5_read_voltage_data(adc, data_volt);

unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

typedef int (*adc_do_conversion)(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur);

static irqreturn_t adc5_isr(int irq, void *dev_id)
{
	struct adc5_chip *adc = dev_id;

	complete(&adc->complete);

	return IRQ_HANDLED;
}

static int adc5_fwnode_xlate(struct iio_dev *indio_dev,
			     const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i;

	for (i = 0; i < adc->nchannels; i++)
		if (adc->chan_props[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static int adc7_fwnode_xlate(struct iio_dev *indio_dev,
			     const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i, v_channel;

	for (i = 0; i < adc->nchannels; i++) {
		v_channel = (adc->chan_props[i].sid << ADC_CHANNEL_OFFSET) |
			adc->chan_props[i].channel;
		if (v_channel == iiospec->args[0])
			return i;
	}

	return -EINVAL;
}

static int adc_read_raw_common(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask, adc_do_conversion do_conv)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;
	u16 adc_code_volt, adc_code_cur;
	int ret;

	prop = &adc->chan_props[chan->address];

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = do_conv(adc, prop, chan,
					&adc_code_volt, &adc_code_cur);
		if (ret)
			return ret;

		ret = qcom_adc5_hw_scale(prop->scale_fn_type,
			prop->prescale,
			adc->data,
			adc_code_volt, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int adc5_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return adc_read_raw_common(indio_dev, chan, val, val2,
				mask, adc5_do_conversion);
}

static int adc7_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return adc_read_raw_common(indio_dev, chan, val, val2,
				mask, adc7_do_conversion);
}

static const struct iio_info adc5_info = {
	.read_raw = adc5_read_raw,
	.fwnode_xlate = adc5_fwnode_xlate,
};

static const struct iio_info adc7_info = {
	.read_raw = adc7_read_raw,
	.fwnode_xlate = adc7_fwnode_xlate,
};

struct adc5_channels {
	const char *datasheet_name;
	unsigned int prescale_index;
	enum iio_chan_type type;
	long info_mask;
	enum vadc_scale_fn_type scale_fn_type;
};

/* In these definitions, _pre refers to an index into adc5_prescale_ratios. */
#define ADC5_CHAN(_dname, _type, _mask, _pre, _scale)			\
	{								\
		.datasheet_name = _dname,				\
		.prescale_index = _pre,					\
		.type = _type,						\
		.info_mask = _mask,					\
		.scale_fn_type = _scale,				\
	},								\

#define ADC5_CHAN_TEMP(_dname, _pre, _scale)				\
	ADC5_CHAN(_dname, IIO_TEMP,					\
		BIT(IIO_CHAN_INFO_PROCESSED),				\
		_pre, _scale)						\

#define ADC5_CHAN_VOLT(_dname, _pre, _scale)				\
	ADC5_CHAN(_dname, IIO_VOLTAGE,					\
		  BIT(IIO_CHAN_INFO_PROCESSED),				\
		  _pre, _scale)						\

static const struct adc5_channels adc5_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC5_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VCOIN]		= ADC5_CHAN_VOLT("vcoin", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM)
	[ADC5_USB_IN_I]		= ADC5_CHAN_VOLT("usb_in_i_uv", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_USB_IN_V_16]	= ADC5_CHAN_VOLT("usb_in_v_div_16", 8,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_CHG_TEMP]		= ADC5_CHAN_TEMP("chg_temp", 0,
					SCALE_HW_CALIB_PM5_CHG_TEMP)
	/* Charger prescales SBUx and MID_CHG to fit within 1.8V upper unit */
	[ADC5_SBUx]		= ADC5_CHAN_VOLT("chg_sbux", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_MID_CHG_DIV6]	= ADC5_CHAN_VOLT("chg_mid_chg", 3,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_XO_THERM_100K_PU]	= ADC5_CHAN_TEMP("xo_therm", 0,
					SCALE_HW_CALIB_XOTHERM)
	[ADC5_BAT_ID_100K_PU]	= ADC5_CHAN_TEMP("bat_id", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2]	= ADC5_CHAN_TEMP("amux_thm2", 0,
					SCALE_HW_CALIB_PM5_SMB_TEMP)
	[ADC5_GPIO1_100K_PU]	= ADC5_CHAN_TEMP("gpio1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO2_100K_PU]	= ADC5_CHAN_TEMP("gpio2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO3_100K_PU]	= ADC5_CHAN_TEMP("gpio3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO4_100K_PU]	= ADC5_CHAN_TEMP("gpio4_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
};

static const struct adc5_channels adc7_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC7_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 3,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC7_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM4_100K_PU] = ADC5_CHAN_TEMP("amux_thm4_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM5_100K_PU] = ADC5_CHAN_TEMP("amux_thm5_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM6_100K_PU] = ADC5_CHAN_TEMP("amux_thm6_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO1_100K_PU]	= ADC5_CHAN_TEMP("gpio1_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO2_100K_PU]	= ADC5_CHAN_TEMP("gpio2_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO3_100K_PU]	= ADC5_CHAN_TEMP("gpio3_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO4_100K_PU]	= ADC5_CHAN_TEMP("gpio4_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
};

static const struct adc5_channels adc5_chans_rev2[ADC5_MAX_CHANNEL] = {
	[ADC5_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VREF_VADC]	= ADC5_CHAN_VOLT("vref_vadc", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VCOIN]		= ADC5_CHAN_VOLT("vcoin", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM)
	[ADC5_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM4_100K_PU] = ADC5_CHAN_TEMP("amux_thm4_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM5_100K_PU] = ADC5_CHAN_TEMP("amux_thm5_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_XO_THERM_100K_PU]	= ADC5_CHAN_TEMP("xo_therm_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
};

static int adc5_get_fw_channel_data(struct adc5_chip *adc,
				    struct adc5_channel_prop *prop,
				    struct fwnode_handle *fwnode,
				    const struct adc5_data *data)
{
	const char *channel_name;
	char *name;
	u32 chan, value, varr[2];
	u32 sid = 0;
	int ret;
	struct device *dev = adc->dev;

	name = devm_kasprintf(dev, GFP_KERNEL, "%pfwP", fwnode);
	if (!name)
		return -ENOMEM;

	/* Cut the address part */
	name[strchrnul(name, '@') - name] = '\0';

	ret = fwnode_property_read_u32(fwnode, "reg", &chan);
	if (ret) {
		dev_err(dev, "invalid channel number %s\n", name);
		return ret;
	}

	/* Value read from "reg" is virtual channel number */

	/* virtual channel number = sid << 8 | channel number */

	if (adc->data->info == &adc7_info) {
		sid = chan >> ADC_CHANNEL_OFFSET;
		chan = chan & ADC_CHANNEL_MASK;
	}

	if (chan > ADC5_PARALLEL_ISENSE_VBAT_IDATA) {
		dev_err(dev, "%s invalid channel number %d\n", name, chan);
		return -EINVAL;
	}

	/* the channel has DT description */
	prop->channel = chan;
	prop->sid = sid;

	ret = fwnode_property_read_string(fwnode, "label", &channel_name);
	if (ret)
		channel_name = data->adc_chans[chan].datasheet_name;

	prop->channel_name = channel_name;

	ret = fwnode_property_read_u32(fwnode, "qcom,decimation", &value);
	if (!ret) {
		ret = qcom_adc5_decimation_from_dt(value, data->decimation);
		if (ret < 0) {
			dev_err(dev, "%02x invalid decimation %d\n",
				chan, value);
			return ret;
		}
		prop->decimation = ret;
	} else {
		prop->decimation = ADC5_DECIMATION_DEFAULT;
	}

	ret = fwnode_property_read_u32_array(fwnode, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = qcom_adc5_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0) {
			dev_err(dev, "%02x invalid pre-scaling <%d %d>\n",
				chan, varr[0], varr[1]);
			return ret;
		}
		prop->prescale = ret;
	} else {
		prop->prescale =
			adc->data->adc_chans[prop->channel].prescale_index;
	}

	ret = fwnode_property_read_u32(fwnode, "qcom,hw-settle-time", &value);
	if (!ret) {
		u8 dig_version[2];

		ret = adc5_read(adc, ADC5_USR_REVISION1, dig_version,
							sizeof(dig_version));
		if (ret) {
			dev_err(dev, "Invalid dig version read %d\n", ret);
			return ret;
		}

		dev_dbg(dev, "dig_ver:minor:%d, major:%d\n", dig_version[0],
						dig_version[1]);
		/* Digital controller >= 5.3 have hw_settle_2 option */
		if ((dig_version[0] >= ADC5_HW_SETTLE_DIFF_MINOR &&
			dig_version[1] >= ADC5_HW_SETTLE_DIFF_MAJOR) ||
			adc->data->info == &adc7_info)
			ret = qcom_adc5_hw_settle_time_from_dt(value, data->hw_settle_2);
		else
			ret = qcom_adc5_hw_settle_time_from_dt(value, data->hw_settle_1);

		if (ret < 0) {
			dev_err(dev, "%02x invalid hw-settle-time %d us\n",
				chan, value);
			return ret;
		}
		prop->hw_settle_time = ret;
	} else {
		prop->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	}

	ret = fwnode_property_read_u32(fwnode, "qcom,avg-samples", &value);
	if (!ret) {
		ret = qcom_adc5_avg_samples_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "%02x invalid avg-samples %d\n",
				chan, value);
			return ret;
		}
		prop->avg_samples = ret;
	} else {
		prop->avg_samples = VADC_DEF_AVG_SAMPLES;
	}

	if (fwnode_property_read_bool(fwnode, "qcom,ratiometric"))
		prop->cal_method = ADC5_RATIOMETRIC_CAL;
	else
		prop->cal_method = ADC5_ABSOLUTE_CAL;

	/*
	 * Default to using timer calibration. Using a fresh calibration value
	 * for every conversion will increase the overall time for a request.
	 */
	prop->cal_val = ADC5_TIMER_CAL;

	dev_dbg(dev, "%02x name %s\n", chan, name);

	return 0;
}

static const struct adc5_data adc5_data_pmic = {
	.full_scale_code_volt = 0x70e4,
	.full_scale_code_cur = 0x2710,
	.adc_chans = adc5_chans_pmic,
	.info = &adc5_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{250, 420, 840},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				800, 900, 1, 2, 4, 6, 8, 10},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1, 2, 4, 8, 16, 32, 64, 128},
};

static const struct adc5_data adc7_data_pmic = {
	.full_scale_code_volt = 0x70e4,
	.adc_chans = adc7_chans_pmic,
	.info = &adc7_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct adc5_data adc5_data_pmic_rev2 = {
	.full_scale_code_volt = 0x4000,
	.full_scale_code_cur = 0x1800,
	.adc_chans = adc5_chans_rev2,
	.info = &adc5_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{256, 512, 1024},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{0, 100, 200, 300, 400, 500, 600, 700,
				800, 900, 1, 2, 4, 6, 8, 10},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1, 2, 4, 8, 16, 32, 64, 128},
};

static const struct of_device_id adc5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc5",
		.data = &adc5_data_pmic,
	},
	{
		.compatible = "qcom,spmi-adc7",
		.data = &adc7_data_pmic,
	},
	{
		.compatible = "qcom,spmi-adc-rev2",
		.data = &adc5_data_pmic_rev2,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc5_match_table);

static int adc5_get_fw_data(struct adc5_chip *adc)
{
	const struct adc5_channels *adc_chan;
	struct iio_chan_spec *iio_chan;
	struct adc5_channel_prop prop, *chan_props;
	unsigned int index = 0;
	int ret;

	adc->nchannels = device_get_child_node_count(adc->dev);
	if (!adc->nchannels)
		return dev_err_probe(adc->dev, -EINVAL, "no channels defined\n");

	adc->iio_chans = devm_kcalloc(adc->dev, adc->nchannels,
				       sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	adc->chan_props = devm_kcalloc(adc->dev, adc->nchannels,
					sizeof(*adc->chan_props), GFP_KERNEL);
	if (!adc->chan_props)
		return -ENOMEM;

	chan_props = adc->chan_props;
	iio_chan = adc->iio_chans;
	adc->data = device_get_match_data(adc->dev);
	if (!adc->data)
		adc->data = &adc5_data_pmic;

	device_for_each_child_node_scoped(adc->dev, child) {
		ret = adc5_get_fw_channel_data(adc, &prop, child, adc->data);
		if (ret)
			return ret;

		prop.scale_fn_type =
			adc->data->adc_chans[prop.channel].scale_fn_type;
		*chan_props = prop;
		adc_chan = &adc->data->adc_chans[prop.channel];

		iio_chan->channel = prop.channel;
		iio_chan->datasheet_name = adc_chan->datasheet_name;
		iio_chan->extend_name = prop.channel_name;
		iio_chan->info_mask_separate = adc_chan->info_mask;
		iio_chan->type = adc_chan->type;
		iio_chan->address = index;
		iio_chan++;
		chan_props++;
		index++;
	}

	return 0;
}

static int adc5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct adc5_chip *adc;
	struct regmap *regmap;
	int ret, irq_eoc;
	u32 reg;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = device_property_read_u32(dev, "reg", &reg);
	if (ret < 0)
		return ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->regmap = regmap;
	adc->dev = dev;
	adc->base = reg;

	init_completion(&adc->complete);
	mutex_init(&adc->lock);

	ret = adc5_get_fw_data(adc);
	if (ret)
		return ret;

	irq_eoc = platform_get_irq(pdev, 0);
	if (irq_eoc < 0) {
		if (irq_eoc == -EPROBE_DEFER || irq_eoc == -EINVAL)
			return irq_eoc;
		adc->poll_eoc = true;
	} else {
		ret = devm_request_irq(dev, irq_eoc, adc5_isr, 0,
				       "pm-adc5", adc);
		if (ret)
			return ret;
	}

	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = adc->data->info;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver adc5_driver = {
	.driver = {
		.name = "qcom-spmi-adc5",
		.of_match_table = adc5_match_table,
	},
	.probe = adc5_probe,
};
module_platform_driver(adc5_driver);

MODULE_ALIAS("platform:qcom-spmi-adc5");
MODULE_DESCRIPTION("Qualcomm Technologies Inc. PMIC5 ADC driver");
MODULE_LICENSE("GPL v2");
