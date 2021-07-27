// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Linaro Limited
 *
 * Based on original driver:
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/bitfield.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

/*
 * Thermal monitoring block consists of 8 (ADC_TM5_NUM_CHANNELS) channels. Each
 * channel is programmed to use one of ADC channels for voltage comparison.
 * Voltages are programmed using ADC codes, so we have to convert temp to
 * voltage and then to ADC code value.
 *
 * Configuration of TM channels must match configuration of corresponding ADC
 * channels.
 */

#define ADC5_MAX_CHANNEL                        0xc0
#define ADC_TM5_NUM_CHANNELS		8

#define ADC_TM5_STATUS_LOW			0x0a

#define ADC_TM5_STATUS_HIGH			0x0b

#define ADC_TM5_NUM_BTM				0x0f

#define ADC_TM5_ADC_DIG_PARAM			0x42

#define ADC_TM5_FAST_AVG_CTL			(ADC_TM5_ADC_DIG_PARAM + 1)
#define ADC_TM5_FAST_AVG_EN				BIT(7)

#define ADC_TM5_MEAS_INTERVAL_CTL		(ADC_TM5_ADC_DIG_PARAM + 2)
#define ADC_TM5_TIMER1					3 /* 3.9ms */

#define ADC_TM5_MEAS_INTERVAL_CTL2		(ADC_TM5_ADC_DIG_PARAM + 3)
#define ADC_TM5_MEAS_INTERVAL_CTL2_MASK			0xf0
#define ADC_TM5_TIMER2					10 /* 1 second */
#define ADC_TM5_MEAS_INTERVAL_CTL3_MASK			0xf
#define ADC_TM5_TIMER3					4 /* 4 second */

#define ADC_TM_EN_CTL1				0x46
#define ADC_TM_EN					BIT(7)
#define ADC_TM_CONV_REQ				0x47
#define ADC_TM_CONV_REQ_EN				BIT(7)

#define ADC_TM5_M_CHAN_BASE			0x60

#define ADC_TM5_M_ADC_CH_SEL_CTL(n)		(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 0)
#define ADC_TM5_M_LOW_THR0(n)			(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 1)
#define ADC_TM5_M_LOW_THR1(n)			(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 2)
#define ADC_TM5_M_HIGH_THR0(n)			(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 3)
#define ADC_TM5_M_HIGH_THR1(n)			(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 4)
#define ADC_TM5_M_MEAS_INTERVAL_CTL(n)		(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 5)
#define ADC_TM5_M_CTL(n)			(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 6)
#define ADC_TM5_M_CTL_HW_SETTLE_DELAY_MASK		0xf
#define ADC_TM5_M_CTL_CAL_SEL_MASK			0x30
#define ADC_TM5_M_CTL_CAL_VAL				0x40
#define ADC_TM5_M_EN(n)				(ADC_TM5_M_CHAN_BASE + ((n) * 8) + 7)
#define ADC_TM5_M_MEAS_EN				BIT(7)
#define ADC_TM5_M_HIGH_THR_INT_EN			BIT(1)
#define ADC_TM5_M_LOW_THR_INT_EN			BIT(0)

enum adc5_timer_select {
	ADC5_TIMER_SEL_1 = 0,
	ADC5_TIMER_SEL_2,
	ADC5_TIMER_SEL_3,
	ADC5_TIMER_SEL_NONE,
};

struct adc_tm5_data {
	const u32	full_scale_code_volt;
	unsigned int	*decimation;
	unsigned int	*hw_settle;
};

enum adc_tm5_cal_method {
	ADC_TM5_NO_CAL = 0,
	ADC_TM5_RATIOMETRIC_CAL,
	ADC_TM5_ABSOLUTE_CAL
};

struct adc_tm5_chip;

/**
 * struct adc_tm5_channel - ADC Thermal Monitoring channel data.
 * @channel: channel number.
 * @adc_channel: corresponding ADC channel number.
 * @cal_method: calibration method.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @iio: IIO channel instance used by this channel.
 * @chip: ADC TM chip instance.
 * @tzd: thermal zone device used by this channel.
 */
struct adc_tm5_channel {
	unsigned int		channel;
	unsigned int		adc_channel;
	enum adc_tm5_cal_method	cal_method;
	unsigned int		prescale;
	unsigned int		hw_settle_time;
	struct iio_channel	*iio;
	struct adc_tm5_chip	*chip;
	struct thermal_zone_device *tzd;
};

/**
 * struct adc_tm5_chip - ADC Thermal Monitoring properties
 * @regmap: SPMI ADC5 Thermal Monitoring  peripheral register map field.
 * @dev: SPMI ADC5 device.
 * @data: software configuration data.
 * @channels: array of ADC TM channel data.
 * @nchannels: amount of channels defined/allocated
 * @decimation: sampling rate supported for the channel.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @base: base address of TM registers.
 */
struct adc_tm5_chip {
	struct regmap		*regmap;
	struct device		*dev;
	const struct adc_tm5_data	*data;
	struct adc_tm5_channel	*channels;
	unsigned int		nchannels;
	unsigned int		decimation;
	unsigned int		avg_samples;
	u16			base;
};

static const struct adc_tm5_data adc_tm5_data_pmic = {
	.full_scale_code_volt = 0x70e4,
	.decimation = (unsigned int []) { 250, 420, 840 },
	.hw_settle = (unsigned int []) { 15, 100, 200, 300, 400, 500, 600, 700,
					 1000, 2000, 4000, 8000, 16000, 32000,
					 64000, 128000 },
};

static int adc_tm5_read(struct adc_tm5_chip *adc_tm, u16 offset, u8 *data, int len)
{
	return regmap_bulk_read(adc_tm->regmap, adc_tm->base + offset, data, len);
}

static int adc_tm5_write(struct adc_tm5_chip *adc_tm, u16 offset, u8 *data, int len)
{
	return regmap_bulk_write(adc_tm->regmap, adc_tm->base + offset, data, len);
}

static int adc_tm5_reg_update(struct adc_tm5_chip *adc_tm, u16 offset, u8 mask, u8 val)
{
	return regmap_write_bits(adc_tm->regmap, adc_tm->base + offset, mask, val);
}

static irqreturn_t adc_tm5_isr(int irq, void *data)
{
	struct adc_tm5_chip *chip = data;
	u8 status_low, status_high, ctl;
	int ret, i;

	ret = adc_tm5_read(chip, ADC_TM5_STATUS_LOW, &status_low, sizeof(status_low));
	if (unlikely(ret)) {
		dev_err(chip->dev, "read status low failed: %d\n", ret);
		return IRQ_HANDLED;
	}

	ret = adc_tm5_read(chip, ADC_TM5_STATUS_HIGH, &status_high, sizeof(status_high));
	if (unlikely(ret)) {
		dev_err(chip->dev, "read status high failed: %d\n", ret);
		return IRQ_HANDLED;
	}

	for (i = 0; i < chip->nchannels; i++) {
		bool upper_set = false, lower_set = false;
		unsigned int ch = chip->channels[i].channel;

		/* No TZD, we warned at the boot time */
		if (!chip->channels[i].tzd)
			continue;

		ret = adc_tm5_read(chip, ADC_TM5_M_EN(ch), &ctl, sizeof(ctl));
		if (unlikely(ret)) {
			dev_err(chip->dev, "ctl read failed: %d, channel %d\n", ret, i);
			continue;
		}

		if (!(ctl & ADC_TM5_M_MEAS_EN))
			continue;

		lower_set = (status_low & BIT(ch)) &&
			(ctl & ADC_TM5_M_LOW_THR_INT_EN);

		upper_set = (status_high & BIT(ch)) &&
			(ctl & ADC_TM5_M_HIGH_THR_INT_EN);

		if (upper_set || lower_set)
			thermal_zone_device_update(chip->channels[i].tzd,
						   THERMAL_EVENT_UNSPECIFIED);
	}

	return IRQ_HANDLED;
}

static int adc_tm5_get_temp(void *data, int *temp)
{
	struct adc_tm5_channel *channel = data;
	int ret;

	if (!channel || !channel->iio)
		return -EINVAL;

	ret = iio_read_channel_processed(channel->iio, temp);
	if (ret < 0)
		return ret;

	if (ret != IIO_VAL_INT)
		return -EINVAL;

	return 0;
}

static int adc_tm5_disable_channel(struct adc_tm5_channel *channel)
{
	struct adc_tm5_chip *chip = channel->chip;
	unsigned int reg = ADC_TM5_M_EN(channel->channel);

	return adc_tm5_reg_update(chip, reg,
				  ADC_TM5_M_MEAS_EN |
				  ADC_TM5_M_HIGH_THR_INT_EN |
				  ADC_TM5_M_LOW_THR_INT_EN,
				  0);
}

static int adc_tm5_enable(struct adc_tm5_chip *chip)
{
	int ret;
	u8 data;

	data = ADC_TM_EN;
	ret = adc_tm5_write(chip, ADC_TM_EN_CTL1, &data, sizeof(data));
	if (ret < 0) {
		dev_err(chip->dev, "adc-tm enable failed\n");
		return ret;
	}

	data = ADC_TM_CONV_REQ_EN;
	ret = adc_tm5_write(chip, ADC_TM_CONV_REQ, &data, sizeof(data));
	if (ret < 0) {
		dev_err(chip->dev, "adc-tm request conversion failed\n");
		return ret;
	}

	return 0;
}

static int adc_tm5_configure(struct adc_tm5_channel *channel, int low, int high)
{
	struct adc_tm5_chip *chip = channel->chip;
	u8 buf[8];
	u16 reg = ADC_TM5_M_ADC_CH_SEL_CTL(channel->channel);
	int ret;

	ret = adc_tm5_read(chip, reg, buf, sizeof(buf));
	if (ret) {
		dev_err(chip->dev, "channel %d params read failed: %d\n", channel->channel, ret);
		return ret;
	}

	buf[0] = channel->adc_channel;

	/* High temperature corresponds to low voltage threshold */
	if (high != INT_MAX) {
		u16 adc_code = qcom_adc_tm5_temp_volt_scale(channel->prescale,
				chip->data->full_scale_code_volt, high);

		buf[1] = adc_code & 0xff;
		buf[2] = adc_code >> 8;
		buf[7] |= ADC_TM5_M_LOW_THR_INT_EN;
	} else {
		buf[7] &= ~ADC_TM5_M_LOW_THR_INT_EN;
	}

	/* Low temperature corresponds to high voltage threshold */
	if (low != -INT_MAX) {
		u16 adc_code = qcom_adc_tm5_temp_volt_scale(channel->prescale,
				chip->data->full_scale_code_volt, low);

		buf[3] = adc_code & 0xff;
		buf[4] = adc_code >> 8;
		buf[7] |= ADC_TM5_M_HIGH_THR_INT_EN;
	} else {
		buf[7] &= ~ADC_TM5_M_HIGH_THR_INT_EN;
	}

	buf[5] = ADC5_TIMER_SEL_2;

	/* Set calibration select, hw_settle delay */
	buf[6] &= ~ADC_TM5_M_CTL_HW_SETTLE_DELAY_MASK;
	buf[6] |= FIELD_PREP(ADC_TM5_M_CTL_HW_SETTLE_DELAY_MASK, channel->hw_settle_time);
	buf[6] &= ~ADC_TM5_M_CTL_CAL_SEL_MASK;
	buf[6] |= FIELD_PREP(ADC_TM5_M_CTL_CAL_SEL_MASK, channel->cal_method);

	buf[7] |= ADC_TM5_M_MEAS_EN;

	ret = adc_tm5_write(chip, reg, buf, sizeof(buf));
	if (ret) {
		dev_err(chip->dev, "channel %d params write failed: %d\n", channel->channel, ret);
		return ret;
	}

	return adc_tm5_enable(chip);
}

static int adc_tm5_set_trips(void *data, int low, int high)
{
	struct adc_tm5_channel *channel = data;
	struct adc_tm5_chip *chip;
	int ret;

	if (!channel)
		return -EINVAL;

	chip = channel->chip;
	dev_dbg(chip->dev, "%d:low(mdegC):%d, high(mdegC):%d\n",
		channel->channel, low, high);

	if (high == INT_MAX && low <= -INT_MAX)
		ret = adc_tm5_disable_channel(channel);
	else
		ret = adc_tm5_configure(channel, low, high);

	return ret;
}

static struct thermal_zone_of_device_ops adc_tm5_ops = {
	.get_temp = adc_tm5_get_temp,
	.set_trips = adc_tm5_set_trips,
};

static int adc_tm5_register_tzd(struct adc_tm5_chip *adc_tm)
{
	unsigned int i;
	struct thermal_zone_device *tzd;

	for (i = 0; i < adc_tm->nchannels; i++) {
		adc_tm->channels[i].chip = adc_tm;

		tzd = devm_thermal_zone_of_sensor_register(adc_tm->dev,
							   adc_tm->channels[i].channel,
							   &adc_tm->channels[i],
							   &adc_tm5_ops);
		if (IS_ERR(tzd)) {
			dev_err(adc_tm->dev, "Error registering TZ zone for channel %d: %ld\n",
				adc_tm->channels[i].channel, PTR_ERR(tzd));
			return PTR_ERR(tzd);
		}
		adc_tm->channels[i].tzd = tzd;
	}

	return 0;
}

static int adc_tm5_init(struct adc_tm5_chip *chip)
{
	u8 buf[4], channels_available;
	int ret;
	unsigned int i;

	ret = adc_tm5_read(chip, ADC_TM5_NUM_BTM,
			   &channels_available, sizeof(channels_available));
	if (ret) {
		dev_err(chip->dev, "read failed for BTM channels\n");
		return ret;
	}

	for (i = 0; i < chip->nchannels; i++) {
		if (chip->channels[i].channel >= channels_available) {
			dev_err(chip->dev, "Invalid channel %d\n", chip->channels[i].channel);
			return -EINVAL;
		}
	}

	buf[0] = chip->decimation;
	buf[1] = chip->avg_samples | ADC_TM5_FAST_AVG_EN;
	buf[2] = ADC_TM5_TIMER1;
	buf[3] = FIELD_PREP(ADC_TM5_MEAS_INTERVAL_CTL2_MASK, ADC_TM5_TIMER2) |
		 FIELD_PREP(ADC_TM5_MEAS_INTERVAL_CTL3_MASK, ADC_TM5_TIMER3);

	ret = adc_tm5_write(chip, ADC_TM5_ADC_DIG_PARAM, buf, sizeof(buf));
	if (ret) {
		dev_err(chip->dev, "block write failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int adc_tm5_get_dt_channel_data(struct adc_tm5_chip *adc_tm,
				       struct adc_tm5_channel *channel,
				       struct device_node *node)
{
	const char *name = node->name;
	u32 chan, value, varr[2];
	int ret;
	struct device *dev = adc_tm->dev;
	struct of_phandle_args args;

	ret = of_property_read_u32(node, "reg", &chan);
	if (ret) {
		dev_err(dev, "%s: invalid channel number %d\n", name, ret);
		return ret;
	}

	if (chan >= ADC_TM5_NUM_CHANNELS) {
		dev_err(dev, "%s: channel number too big: %d\n", name, chan);
		return -EINVAL;
	}

	channel->channel = chan;

	/*
	 * We are tied to PMIC's ADC controller, which always use single
	 * argument for channel number.  So don't bother parsing
	 * #io-channel-cells, just enforce cell_count = 1.
	 */
	ret = of_parse_phandle_with_fixed_args(node, "io-channels", 1, 0, &args);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing ADC channel number %d: %d\n", name, chan, ret);
		return ret;
	}
	of_node_put(args.np);

	if (args.args_count != 1 || args.args[0] >= ADC5_MAX_CHANNEL) {
		dev_err(dev, "%s: invalid ADC channel number %d\n", name, chan);
		return -EINVAL;
	}
	channel->adc_channel = args.args[0];

	channel->iio = devm_of_iio_channel_get_by_name(adc_tm->dev, node, NULL);
	if (IS_ERR(channel->iio)) {
		ret = PTR_ERR(channel->iio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: error getting channel: %d\n", name, ret);
		return ret;
	}

	ret = of_property_read_u32_array(node, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = qcom_adc5_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0) {
			dev_err(dev, "%s: invalid pre-scaling <%d %d>\n",
				name, varr[0], varr[1]);
			return ret;
		}
		channel->prescale = ret;
	} else {
		/* 1:1 prescale is index 0 */
		channel->prescale = 0;
	}

	ret = of_property_read_u32(node, "qcom,hw-settle-time-us", &value);
	if (!ret) {
		ret = qcom_adc5_hw_settle_time_from_dt(value, adc_tm->data->hw_settle);
		if (ret < 0) {
			dev_err(dev, "%s invalid hw-settle-time-us %d us\n",
				name, value);
			return ret;
		}
		channel->hw_settle_time = ret;
	} else {
		channel->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	}

	if (of_property_read_bool(node, "qcom,ratiometric"))
		channel->cal_method = ADC_TM5_RATIOMETRIC_CAL;
	else
		channel->cal_method = ADC_TM5_ABSOLUTE_CAL;

	return 0;
}

static int adc_tm5_get_dt_data(struct adc_tm5_chip *adc_tm, struct device_node *node)
{
	struct adc_tm5_channel *channels;
	struct device_node *child;
	u32 value;
	int ret;
	struct device *dev = adc_tm->dev;

	adc_tm->nchannels = of_get_available_child_count(node);
	if (!adc_tm->nchannels)
		return -EINVAL;

	adc_tm->channels = devm_kcalloc(dev, adc_tm->nchannels,
					sizeof(*adc_tm->channels), GFP_KERNEL);
	if (!adc_tm->channels)
		return -ENOMEM;

	channels = adc_tm->channels;

	adc_tm->data = of_device_get_match_data(dev);
	if (!adc_tm->data)
		adc_tm->data = &adc_tm5_data_pmic;

	ret = of_property_read_u32(node, "qcom,decimation", &value);
	if (!ret) {
		ret = qcom_adc5_decimation_from_dt(value, adc_tm->data->decimation);
		if (ret < 0) {
			dev_err(dev, "invalid decimation %d\n", value);
			return ret;
		}
		adc_tm->decimation = ret;
	} else {
		adc_tm->decimation = ADC5_DECIMATION_DEFAULT;
	}

	ret = of_property_read_u32(node, "qcom,avg-samples", &value);
	if (!ret) {
		ret = qcom_adc5_avg_samples_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "invalid avg-samples %d\n", value);
			return ret;
		}
		adc_tm->avg_samples = ret;
	} else {
		adc_tm->avg_samples = VADC_DEF_AVG_SAMPLES;
	}

	for_each_available_child_of_node(node, child) {
		ret = adc_tm5_get_dt_channel_data(adc_tm, channels, child);
		if (ret) {
			of_node_put(child);
			return ret;
		}

		channels++;
	}

	return 0;
}

static int adc_tm5_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct adc_tm5_chip *adc_tm;
	struct regmap *regmap;
	int ret, irq;
	u32 reg;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = of_property_read_u32(node, "reg", &reg);
	if (ret)
		return ret;

	adc_tm = devm_kzalloc(&pdev->dev, sizeof(*adc_tm), GFP_KERNEL);
	if (!adc_tm)
		return -ENOMEM;

	adc_tm->regmap = regmap;
	adc_tm->dev = dev;
	adc_tm->base = reg;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "get_irq failed: %d\n", irq);
		return irq;
	}

	ret = adc_tm5_get_dt_data(adc_tm, node);
	if (ret) {
		dev_err(dev, "get dt data failed: %d\n", ret);
		return ret;
	}

	ret = adc_tm5_init(adc_tm);
	if (ret) {
		dev_err(dev, "adc-tm init failed\n");
		return ret;
	}

	ret = adc_tm5_register_tzd(adc_tm);
	if (ret) {
		dev_err(dev, "tzd register failed\n");
		return ret;
	}

	return devm_request_threaded_irq(dev, irq, NULL, adc_tm5_isr,
					 IRQF_ONESHOT, "pm-adc-tm5", adc_tm);
}

static const struct of_device_id adc_tm5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc-tm5",
		.data = &adc_tm5_data_pmic,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc_tm5_match_table);

static struct platform_driver adc_tm5_driver = {
	.driver = {
		.name = "qcom-spmi-adc-tm5",
		.of_match_table = adc_tm5_match_table,
	},
	.probe = adc_tm5_probe,
};
module_platform_driver(adc_tm5_driver);

MODULE_DESCRIPTION("SPMI PMIC Thermal Monitor ADC driver");
MODULE_LICENSE("GPL v2");
