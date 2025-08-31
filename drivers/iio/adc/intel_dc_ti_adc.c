// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Dollar Cove TI PMIC GPADC Driver
 *
 * Copyright (C) 2014 Intel Corporation (Ramakrishna Pallala <ramakrishna.pallala@intel.com>)
 * Copyright (C) 2024 - 2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/wait.h>

#include <linux/iio/driver.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>

#define DC_TI_ADC_CNTL_REG			0x50
#define DC_TI_ADC_START				BIT(0)
#define DC_TI_ADC_CH_SEL			GENMASK(2, 1)
#define DC_TI_ADC_EN				BIT(5)
#define DC_TI_ADC_EN_EXT_BPTH_BIAS		BIT(6)

#define DC_TI_VBAT_ZSE_GE_REG			0x53
#define DC_TI_VBAT_GE				GENMASK(3, 0)
#define DC_TI_VBAT_ZSE				GENMASK(7, 4)

/* VBAT GE gain correction is in 0.0015 increments, ZSE is in 1.0 increments */
#define DC_TI_VBAT_GE_STEP			15
#define DC_TI_VBAT_GE_DIV			10000

#define DC_TI_ADC_DATA_REG_CH(x)		(0x54 + 2 * (x))

enum dc_ti_adc_id {
	DC_TI_ADC_VBAT,
	DC_TI_ADC_PMICTEMP,
	DC_TI_ADC_BATTEMP,
	DC_TI_ADC_SYSTEMP0,
};

struct dc_ti_adc_info {
	struct mutex lock; /* Protects against concurrent accesses to the ADC */
	wait_queue_head_t wait;
	struct device *dev;
	struct regmap *regmap;
	int vbat_zse;
	int vbat_ge;
	bool conversion_done;
};

static const struct iio_chan_spec dc_ti_adc_channels[] = {
	{
		.indexed = 1,
		.type = IIO_VOLTAGE,
		.channel = DC_TI_ADC_VBAT,
		.address = DC_TI_ADC_DATA_REG_CH(0),
		.datasheet_name = "CH0",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_PROCESSED),
	}, {
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = DC_TI_ADC_PMICTEMP,
		.address = DC_TI_ADC_DATA_REG_CH(1),
		.datasheet_name = "CH1",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = DC_TI_ADC_BATTEMP,
		.address = DC_TI_ADC_DATA_REG_CH(2),
		.datasheet_name = "CH2",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = DC_TI_ADC_SYSTEMP0,
		.address = DC_TI_ADC_DATA_REG_CH(3),
		.datasheet_name = "CH3",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}
};

static struct iio_map dc_ti_adc_default_maps[] = {
	IIO_MAP("CH0", "chtdc_ti_battery", "VBAT"),
	IIO_MAP("CH1", "chtdc_ti_battery", "PMICTEMP"),
	IIO_MAP("CH2", "chtdc_ti_battery", "BATTEMP"),
	IIO_MAP("CH3", "chtdc_ti_battery", "SYSTEMP0"),
	{ }
};

static irqreturn_t dc_ti_adc_isr(int irq, void *data)
{
	struct dc_ti_adc_info *info = data;

	info->conversion_done = true;
	wake_up(&info->wait);
	return IRQ_HANDLED;
}

static int dc_ti_adc_scale(struct dc_ti_adc_info *info,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2)
{
	if (chan->channel != DC_TI_ADC_VBAT)
		return -EINVAL;

	/* Vbat ADC scale is 4.6875 mV / unit */
	*val = 4;
	*val2 = 687500;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int dc_ti_adc_raw_to_processed(struct dc_ti_adc_info *info,
				      struct iio_chan_spec const *chan,
				      int raw, int *val, int *val2)
{
	if (chan->channel != DC_TI_ADC_VBAT)
		return -EINVAL;

	/* Apply calibration */
	raw -= info->vbat_zse;
	raw = raw * (DC_TI_VBAT_GE_DIV - info->vbat_ge * DC_TI_VBAT_GE_STEP) /
	      DC_TI_VBAT_GE_DIV;
	/* Vbat ADC scale is 4.6875 mV / unit */
	raw *= 46875;

	/* raw is now in 10000 units / mV, convert to milli + milli/1e6 */
	*val = raw / 10000;
	*val2 = (raw % 10000) * 100;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int dc_ti_adc_sample(struct dc_ti_adc_info *info,
			    struct iio_chan_spec const *chan, int *val)
{
	int ret, ch = chan->channel;
	__be16 buf;

	info->conversion_done = false;

	/*
	 * As per TI (PMIC Vendor), the ADC enable and ADC start commands should
	 * not be sent together. Hence send the commands separately.
	 */
	ret = regmap_set_bits(info->regmap, DC_TI_ADC_CNTL_REG, DC_TI_ADC_EN);
	if (ret)
		return ret;

	ret = regmap_update_bits(info->regmap, DC_TI_ADC_CNTL_REG,
				 DC_TI_ADC_CH_SEL,
				 FIELD_PREP(DC_TI_ADC_CH_SEL, ch));
	if (ret)
		return ret;

	/*
	 * As per PMIC Vendor, a minimum of 50 ųs delay is required between ADC
	 * Enable and ADC START commands. This is also recommended by Intel
	 * Hardware team after the timing analysis of GPADC signals. Since the
	 * I2C Write transaction to set the channel number also imparts 25 ųs
	 * delay, we need to wait for another 25 ųs before issuing ADC START.
	 */
	fsleep(25);

	ret = regmap_set_bits(info->regmap, DC_TI_ADC_CNTL_REG,
			      DC_TI_ADC_START);
	if (ret)
		return ret;

	/* TI (PMIC Vendor) recommends 5 s timeout for conversion */
	ret = wait_event_timeout(info->wait, info->conversion_done, 5 * HZ);
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto disable_adc;
	}

	ret = regmap_bulk_read(info->regmap, chan->address, &buf, sizeof(buf));
	if (ret)
		goto disable_adc;

	/* The ADC values are 10 bits wide */
	*val = be16_to_cpu(buf) & GENMASK(9, 0);

disable_adc:
	regmap_clear_bits(info->regmap, DC_TI_ADC_CNTL_REG,
			  DC_TI_ADC_START | DC_TI_ADC_EN);
	return ret;
}

static int dc_ti_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct dc_ti_adc_info *info = iio_priv(indio_dev);
	int ret;

	if (mask == IIO_CHAN_INFO_SCALE)
		return dc_ti_adc_scale(info, chan, val, val2);

	guard(mutex)(&info->lock);

	/*
	 * If channel BPTHERM has been selected, first enable the BPTHERM BIAS
	 * which provides the VREF Voltage reference to convert BPTHERM Input
	 * voltage to temperature.
	 */
	if (chan->channel == DC_TI_ADC_BATTEMP) {
		ret = regmap_set_bits(info->regmap, DC_TI_ADC_CNTL_REG,
				      DC_TI_ADC_EN_EXT_BPTH_BIAS);
		if (ret)
			return ret;
		/*
		 * As per PMIC Vendor specifications, BPTHERM BIAS should be
		 * enabled 35 ms before ADC_EN command.
		 */
		msleep(35);
	}

	ret = dc_ti_adc_sample(info, chan, val);

	if (chan->channel == DC_TI_ADC_BATTEMP)
		regmap_clear_bits(info->regmap, DC_TI_ADC_CNTL_REG,
				  DC_TI_ADC_EN_EXT_BPTH_BIAS);

	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		return dc_ti_adc_raw_to_processed(info, chan, *val, val, val2);
	}

	return -EINVAL;
}

static const struct iio_info dc_ti_adc_iio_info = {
	.read_raw = dc_ti_adc_read_raw,
};

static int dc_ti_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct dc_ti_adc_info *info;
	struct iio_dev *indio_dev;
	unsigned int val;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);

	ret = devm_mutex_init(dev, &info->lock);
	if (ret)
		return ret;

	init_waitqueue_head(&info->wait);

	info->dev = dev;
	info->regmap = pmic->regmap;

	indio_dev->name = "dc_ti_adc";
	indio_dev->channels = dc_ti_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(dc_ti_adc_channels);
	indio_dev->info = &dc_ti_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = regmap_read(info->regmap, DC_TI_VBAT_ZSE_GE_REG, &val);
	if (ret)
		return ret;

	info->vbat_zse = sign_extend32(FIELD_GET(DC_TI_VBAT_ZSE, val), 3);
	info->vbat_ge = sign_extend32(FIELD_GET(DC_TI_VBAT_GE, val), 3);

	dev_dbg(dev, "vbat-zse %d vbat-ge %d\n", info->vbat_zse, info->vbat_ge);

	ret = devm_iio_map_array_register(dev, indio_dev, dc_ti_adc_default_maps);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, irq, NULL, dc_ti_adc_isr,
					IRQF_ONESHOT, indio_dev->name, info);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct platform_device_id dc_ti_adc_ids[] = {
	{ .name = "chtdc_ti_adc" },
	{ }
};
MODULE_DEVICE_TABLE(platform, dc_ti_adc_ids);

static struct platform_driver dc_ti_adc_driver = {
	.driver = {
		.name	= "dc_ti_adc",
	},
	.probe		= dc_ti_adc_probe,
	.id_table	= dc_ti_adc_ids,
};
module_platform_driver(dc_ti_adc_driver);

MODULE_AUTHOR("Ramakrishna Pallala (Intel)");
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("Intel Dollar Cove (TI) GPADC Driver");
MODULE_LICENSE("GPL");
