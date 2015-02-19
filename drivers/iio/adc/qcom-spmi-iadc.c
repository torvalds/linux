/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
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
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* IADC register and bit definition */
#define IADC_REVISION2				0x1
#define IADC_REVISION2_SUPPORTED_IADC		1

#define IADC_PERPH_TYPE				0x4
#define IADC_PERPH_TYPE_ADC			8

#define IADC_PERPH_SUBTYPE			0x5
#define IADC_PERPH_SUBTYPE_IADC			3

#define IADC_STATUS1				0x8
#define IADC_STATUS1_OP_MODE			4
#define IADC_STATUS1_REQ_STS			BIT(1)
#define IADC_STATUS1_EOC			BIT(0)
#define IADC_STATUS1_REQ_STS_EOC_MASK		0x3

#define IADC_MODE_CTL				0x40
#define IADC_OP_MODE_SHIFT			3
#define IADC_OP_MODE_NORMAL			0
#define IADC_TRIM_EN				BIT(0)

#define IADC_EN_CTL1				0x46
#define IADC_EN_CTL1_SET			BIT(7)

#define IADC_CH_SEL_CTL				0x48

#define IADC_DIG_PARAM				0x50
#define IADC_DIG_DEC_RATIO_SEL_SHIFT		2

#define IADC_HW_SETTLE_DELAY			0x51

#define IADC_CONV_REQ				0x52
#define IADC_CONV_REQ_SET			BIT(7)

#define IADC_FAST_AVG_CTL			0x5a
#define IADC_FAST_AVG_EN			0x5b
#define IADC_FAST_AVG_EN_SET			BIT(7)

#define IADC_PERH_RESET_CTL3			0xda
#define IADC_FOLLOW_WARM_RB			BIT(2)

#define IADC_DATA				0x60	/* 16 bits */

#define IADC_SEC_ACCESS				0xd0
#define IADC_SEC_ACCESS_DATA			0xa5

#define IADC_NOMINAL_RSENSE			0xf4
#define IADC_NOMINAL_RSENSE_SIGN_MASK		BIT(7)

#define IADC_REF_GAIN_MICRO_VOLTS		17857

#define IADC_INT_RSENSE_DEVIATION		15625	/* nano Ohms per bit */

#define IADC_INT_RSENSE_IDEAL_VALUE		10000	/* micro Ohms */
#define IADC_INT_RSENSE_DEFAULT_VALUE		7800	/* micro Ohms */
#define IADC_INT_RSENSE_DEFAULT_GF		9000	/* micro Ohms */
#define IADC_INT_RSENSE_DEFAULT_SMIC		9700	/* micro Ohms */

#define IADC_CONV_TIME_MIN_US			2000
#define IADC_CONV_TIME_MAX_US			2100

#define IADC_DEF_PRESCALING			0 /* 1:1 */
#define IADC_DEF_DECIMATION			0 /* 512 */
#define IADC_DEF_HW_SETTLE_TIME			0 /* 0 us */
#define IADC_DEF_AVG_SAMPLES			0 /* 1 sample */

/* IADC channel list */
#define IADC_INT_RSENSE				0
#define IADC_EXT_RSENSE				1
#define IADC_GAIN_17P857MV			3
#define IADC_EXT_OFFSET_CSP_CSN			5
#define IADC_INT_OFFSET_CSP2_CSN2		6

/**
 * struct iadc_chip - IADC Current ADC device structure.
 * @regmap: regmap for register read/write.
 * @dev: This device pointer.
 * @base: base offset for the ADC peripheral.
 * @rsense: Values of the internal and external sense resister in micro Ohms.
 * @poll_eoc: Poll for end of conversion instead of waiting for IRQ.
 * @offset: Raw offset values for the internal and external channels.
 * @gain: Raw gain of the channels.
 * @lock: ADC lock for access to the peripheral.
 * @complete: ADC notification after end of conversion interrupt is received.
 */
struct iadc_chip {
	struct regmap	*regmap;
	struct device	*dev;
	u16		base;
	bool		poll_eoc;
	u32		rsense[2];
	u16		offset[2];
	u16		gain;
	struct mutex	lock;
	struct completion complete;
};

static int iadc_read(struct iadc_chip *iadc, u16 offset, u8 *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(iadc->regmap, iadc->base + offset, &val);
	if (ret < 0)
		return ret;

	*data = val;
	return 0;
}

static int iadc_write(struct iadc_chip *iadc, u16 offset, u8 data)
{
	return regmap_write(iadc->regmap, iadc->base + offset, data);
}

static int iadc_reset(struct iadc_chip *iadc)
{
	u8 data;
	int ret;

	ret = iadc_write(iadc, IADC_SEC_ACCESS, IADC_SEC_ACCESS_DATA);
	if (ret < 0)
		return ret;

	ret = iadc_read(iadc, IADC_PERH_RESET_CTL3, &data);
	if (ret < 0)
		return ret;

	ret = iadc_write(iadc, IADC_SEC_ACCESS, IADC_SEC_ACCESS_DATA);
	if (ret < 0)
		return ret;

	data |= IADC_FOLLOW_WARM_RB;

	return iadc_write(iadc, IADC_PERH_RESET_CTL3, data);
}

static int iadc_set_state(struct iadc_chip *iadc, bool state)
{
	return iadc_write(iadc, IADC_EN_CTL1, state ? IADC_EN_CTL1_SET : 0);
}

static void iadc_status_show(struct iadc_chip *iadc)
{
	u8 mode, sta1, chan, dig, en, req;
	int ret;

	ret = iadc_read(iadc, IADC_MODE_CTL, &mode);
	if (ret < 0)
		return;

	ret = iadc_read(iadc, IADC_DIG_PARAM, &dig);
	if (ret < 0)
		return;

	ret = iadc_read(iadc, IADC_CH_SEL_CTL, &chan);
	if (ret < 0)
		return;

	ret = iadc_read(iadc, IADC_CONV_REQ, &req);
	if (ret < 0)
		return;

	ret = iadc_read(iadc, IADC_STATUS1, &sta1);
	if (ret < 0)
		return;

	ret = iadc_read(iadc, IADC_EN_CTL1, &en);
	if (ret < 0)
		return;

	dev_err(iadc->dev,
		"mode:%02x en:%02x chan:%02x dig:%02x req:%02x sta1:%02x\n",
		mode, en, chan, dig, req, sta1);
}

static int iadc_configure(struct iadc_chip *iadc, int channel)
{
	u8 decim, mode;
	int ret;

	/* Mode selection */
	mode = (IADC_OP_MODE_NORMAL << IADC_OP_MODE_SHIFT) | IADC_TRIM_EN;
	ret = iadc_write(iadc, IADC_MODE_CTL, mode);
	if (ret < 0)
		return ret;

	/* Channel selection */
	ret = iadc_write(iadc, IADC_CH_SEL_CTL, channel);
	if (ret < 0)
		return ret;

	/* Digital parameter setup */
	decim = IADC_DEF_DECIMATION << IADC_DIG_DEC_RATIO_SEL_SHIFT;
	ret = iadc_write(iadc, IADC_DIG_PARAM, decim);
	if (ret < 0)
		return ret;

	/* HW settle time delay */
	ret = iadc_write(iadc, IADC_HW_SETTLE_DELAY, IADC_DEF_HW_SETTLE_TIME);
	if (ret < 0)
		return ret;

	ret = iadc_write(iadc, IADC_FAST_AVG_CTL, IADC_DEF_AVG_SAMPLES);
	if (ret < 0)
		return ret;

	if (IADC_DEF_AVG_SAMPLES)
		ret = iadc_write(iadc, IADC_FAST_AVG_EN, IADC_FAST_AVG_EN_SET);
	else
		ret = iadc_write(iadc, IADC_FAST_AVG_EN, 0);

	if (ret < 0)
		return ret;

	if (!iadc->poll_eoc)
		reinit_completion(&iadc->complete);

	ret = iadc_set_state(iadc, true);
	if (ret < 0)
		return ret;

	/* Request conversion */
	return iadc_write(iadc, IADC_CONV_REQ, IADC_CONV_REQ_SET);
}

static int iadc_poll_wait_eoc(struct iadc_chip *iadc, unsigned int interval_us)
{
	unsigned int count, retry;
	int ret;
	u8 sta1;

	retry = interval_us / IADC_CONV_TIME_MIN_US;

	for (count = 0; count < retry; count++) {
		ret = iadc_read(iadc, IADC_STATUS1, &sta1);
		if (ret < 0)
			return ret;

		sta1 &= IADC_STATUS1_REQ_STS_EOC_MASK;
		if (sta1 == IADC_STATUS1_EOC)
			return 0;

		usleep_range(IADC_CONV_TIME_MIN_US, IADC_CONV_TIME_MAX_US);
	}

	iadc_status_show(iadc);

	return -ETIMEDOUT;
}

static int iadc_read_result(struct iadc_chip *iadc, u16 *data)
{
	return regmap_bulk_read(iadc->regmap, iadc->base + IADC_DATA, data, 2);
}

static int iadc_do_conversion(struct iadc_chip *iadc, int chan, u16 *data)
{
	unsigned int wait;
	int ret;

	ret = iadc_configure(iadc, chan);
	if (ret < 0)
		goto exit;

	wait = BIT(IADC_DEF_AVG_SAMPLES) * IADC_CONV_TIME_MIN_US * 2;

	if (iadc->poll_eoc) {
		ret = iadc_poll_wait_eoc(iadc, wait);
	} else {
		ret = wait_for_completion_timeout(&iadc->complete, wait);
		if (!ret)
			ret = -ETIMEDOUT;
		else
			/* double check conversion status */
			ret = iadc_poll_wait_eoc(iadc, IADC_CONV_TIME_MIN_US);
	}

	if (!ret)
		ret = iadc_read_result(iadc, data);
exit:
	iadc_set_state(iadc, false);
	if (ret < 0)
		dev_err(iadc->dev, "conversion failed\n");

	return ret;
}

static int iadc_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int *val, int *val2, long mask)
{
	struct iadc_chip *iadc = iio_priv(indio_dev);
	s32 isense_ua, vsense_uv;
	u16 adc_raw, vsense_raw;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&iadc->lock);
		ret = iadc_do_conversion(iadc, chan->channel, &adc_raw);
		mutex_unlock(&iadc->lock);
		if (ret < 0)
			return ret;

		vsense_raw = adc_raw - iadc->offset[chan->channel];

		vsense_uv = vsense_raw * IADC_REF_GAIN_MICRO_VOLTS;
		vsense_uv /= (s32)iadc->gain - iadc->offset[chan->channel];

		isense_ua = vsense_uv / iadc->rsense[chan->channel];

		dev_dbg(iadc->dev, "off %d gain %d adc %d %duV I %duA\n",
			iadc->offset[chan->channel], iadc->gain,
			adc_raw, vsense_uv, isense_ua);

		*val = isense_ua;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static const struct iio_info iadc_info = {
	.read_raw = iadc_read_raw,
	.driver_module = THIS_MODULE,
};

static irqreturn_t iadc_isr(int irq, void *dev_id)
{
	struct iadc_chip *iadc = dev_id;

	complete(&iadc->complete);

	return IRQ_HANDLED;
}

static int iadc_update_offset(struct iadc_chip *iadc)
{
	int ret;

	ret = iadc_do_conversion(iadc, IADC_GAIN_17P857MV, &iadc->gain);
	if (ret < 0)
		return ret;

	ret = iadc_do_conversion(iadc, IADC_INT_OFFSET_CSP2_CSN2,
				 &iadc->offset[IADC_INT_RSENSE]);
	if (ret < 0)
		return ret;

	if (iadc->gain == iadc->offset[IADC_INT_RSENSE]) {
		dev_err(iadc->dev, "error: internal offset == gain %d\n",
			iadc->gain);
		return -EINVAL;
	}

	ret = iadc_do_conversion(iadc, IADC_EXT_OFFSET_CSP_CSN,
				 &iadc->offset[IADC_EXT_RSENSE]);
	if (ret < 0)
		return ret;

	if (iadc->gain == iadc->offset[IADC_EXT_RSENSE]) {
		dev_err(iadc->dev, "error: external offset == gain %d\n",
			iadc->gain);
		return -EINVAL;
	}

	return 0;
}

static int iadc_version_check(struct iadc_chip *iadc)
{
	u8 val;
	int ret;

	ret = iadc_read(iadc, IADC_PERPH_TYPE, &val);
	if (ret < 0)
		return ret;

	if (val < IADC_PERPH_TYPE_ADC) {
		dev_err(iadc->dev, "%d is not ADC\n", val);
		return -EINVAL;
	}

	ret = iadc_read(iadc, IADC_PERPH_SUBTYPE, &val);
	if (ret < 0)
		return ret;

	if (val < IADC_PERPH_SUBTYPE_IADC) {
		dev_err(iadc->dev, "%d is not IADC\n", val);
		return -EINVAL;
	}

	ret = iadc_read(iadc, IADC_REVISION2, &val);
	if (ret < 0)
		return ret;

	if (val < IADC_REVISION2_SUPPORTED_IADC) {
		dev_err(iadc->dev, "revision %d not supported\n", val);
		return -EINVAL;
	}

	return 0;
}

static int iadc_rsense_read(struct iadc_chip *iadc, struct device_node *node)
{
	int ret, sign, int_sense;
	u8 deviation;

	ret = of_property_read_u32(node, "qcom,external-resistor-micro-ohms",
				   &iadc->rsense[IADC_EXT_RSENSE]);
	if (ret < 0)
		iadc->rsense[IADC_EXT_RSENSE] = IADC_INT_RSENSE_IDEAL_VALUE;

	if (!iadc->rsense[IADC_EXT_RSENSE]) {
		dev_err(iadc->dev, "external resistor can't be zero Ohms");
		return -EINVAL;
	}

	ret = iadc_read(iadc, IADC_NOMINAL_RSENSE, &deviation);
	if (ret < 0)
		return ret;

	/*
	 * Deviation value stored is an offset from 10 mili Ohms, bit 7 is
	 * the sign, the remaining bits have an LSB of 15625 nano Ohms.
	 */
	sign = (deviation & IADC_NOMINAL_RSENSE_SIGN_MASK) ? -1 : 1;

	deviation &= ~IADC_NOMINAL_RSENSE_SIGN_MASK;

	/* Scale it to nono Ohms */
	int_sense = IADC_INT_RSENSE_IDEAL_VALUE * 1000;
	int_sense += sign * deviation * IADC_INT_RSENSE_DEVIATION;
	int_sense /= 1000; /* micro Ohms */

	iadc->rsense[IADC_INT_RSENSE] = int_sense;
	return 0;
}

static const struct iio_chan_spec iadc_channels[] = {
	{
		.type = IIO_CURRENT,
		.datasheet_name	= "INTERNAL_RSENSE",
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
	},
	{
		.type = IIO_CURRENT,
		.datasheet_name	= "EXTERNAL_RSENSE",
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
	},
};

static int iadc_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct iadc_chip *iadc;
	int ret, irq_eoc;
	u32 res;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*iadc));
	if (!indio_dev)
		return -ENOMEM;

	iadc = iio_priv(indio_dev);
	iadc->dev = dev;

	iadc->regmap = dev_get_regmap(dev->parent, NULL);
	if (!iadc->regmap)
		return -ENODEV;

	init_completion(&iadc->complete);
	mutex_init(&iadc->lock);

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return -ENODEV;

	iadc->base = res;

	ret = iadc_version_check(iadc);
	if (ret < 0)
		return -ENODEV;

	ret = iadc_rsense_read(iadc, node);
	if (ret < 0)
		return -ENODEV;

	dev_dbg(iadc->dev, "sense resistors %d and %d micro Ohm\n",
		iadc->rsense[IADC_INT_RSENSE],
		iadc->rsense[IADC_EXT_RSENSE]);

	irq_eoc = platform_get_irq(pdev, 0);
	if (irq_eoc == -EPROBE_DEFER)
		return irq_eoc;

	if (irq_eoc < 0)
		iadc->poll_eoc = true;

	ret = iadc_reset(iadc);
	if (ret < 0) {
		dev_err(dev, "reset failed\n");
		return ret;
	}

	if (!iadc->poll_eoc) {
		ret = devm_request_irq(dev, irq_eoc, iadc_isr, 0,
					"spmi-iadc", iadc);
		if (!ret)
			enable_irq_wake(irq_eoc);
		else
			return ret;
	} else {
		device_init_wakeup(iadc->dev, 1);
	}

	ret = iadc_update_offset(iadc);
	if (ret < 0) {
		dev_err(dev, "failed offset calibration\n");
		return ret;
	}

	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = node;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &iadc_info;
	indio_dev->channels = iadc_channels;
	indio_dev->num_channels = ARRAY_SIZE(iadc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id iadc_match_table[] = {
	{ .compatible = "qcom,spmi-iadc" },
	{ }
};

MODULE_DEVICE_TABLE(of, iadc_match_table);

static struct platform_driver iadc_driver = {
	.driver = {
		   .name = "qcom-spmi-iadc",
		   .of_match_table = iadc_match_table,
	},
	.probe = iadc_probe,
};

module_platform_driver(iadc_driver);

MODULE_ALIAS("platform:qcom-spmi-iadc");
MODULE_DESCRIPTION("Qualcomm SPMI PMIC current ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ivan T. Ivanov <iivanov@mm-sol.com>");
