/*
 * axp288_adc.c - X-Powers AXP288 PMIC ADC Driver
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/mfd/axp20x.h>
#include <linux/platform_device.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

/*
 * This mask enables all ADCs except for the battery temp-sensor (TS), that is
 * left as-is to avoid breaking charging on devices without a temp-sensor.
 */
#define AXP288_ADC_EN_MASK				0xF0
#define AXP288_ADC_TS_ENABLE				0x01

#define AXP288_ADC_TS_CURRENT_ON_OFF_MASK		GENMASK(1, 0)
#define AXP288_ADC_TS_CURRENT_OFF			(0 << 0)
#define AXP288_ADC_TS_CURRENT_ON_WHEN_CHARGING		(1 << 0)
#define AXP288_ADC_TS_CURRENT_ON_ONDEMAND		(2 << 0)
#define AXP288_ADC_TS_CURRENT_ON			(3 << 0)

enum axp288_adc_id {
	AXP288_ADC_TS,
	AXP288_ADC_PMIC,
	AXP288_ADC_GP,
	AXP288_ADC_BATT_CHRG_I,
	AXP288_ADC_BATT_DISCHRG_I,
	AXP288_ADC_BATT_V,
	AXP288_ADC_NR_CHAN,
};

struct axp288_adc_info {
	int irq;
	struct regmap *regmap;
	bool ts_enabled;
};

static const struct iio_chan_spec axp288_adc_channels[] = {
	{
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = 0,
		.address = AXP288_TS_ADC_H,
		.datasheet_name = "TS_PIN",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = 1,
		.address = AXP288_PMIC_ADC_H,
		.datasheet_name = "PMIC_TEMP",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_TEMP,
		.channel = 2,
		.address = AXP288_GP_ADC_H,
		.datasheet_name = "GPADC",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_CURRENT,
		.channel = 3,
		.address = AXP20X_BATT_CHRG_I_H,
		.datasheet_name = "BATT_CHG_I",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_CURRENT,
		.channel = 4,
		.address = AXP20X_BATT_DISCHRG_I_H,
		.datasheet_name = "BATT_DISCHRG_I",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.indexed = 1,
		.type = IIO_VOLTAGE,
		.channel = 5,
		.address = AXP20X_BATT_V_H,
		.datasheet_name = "BATT_V",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

/* for consumer drivers */
static struct iio_map axp288_adc_default_maps[] = {
	IIO_MAP("TS_PIN", "axp288-batt", "axp288-batt-temp"),
	IIO_MAP("PMIC_TEMP", "axp288-pmic", "axp288-pmic-temp"),
	IIO_MAP("GPADC", "axp288-gpadc", "axp288-system-temp"),
	IIO_MAP("BATT_CHG_I", "axp288-chrg", "axp288-chrg-curr"),
	IIO_MAP("BATT_DISCHRG_I", "axp288-chrg", "axp288-chrg-d-curr"),
	IIO_MAP("BATT_V", "axp288-batt", "axp288-batt-volt"),
	{},
};

static int axp288_adc_read_channel(int *val, unsigned long address,
				struct regmap *regmap)
{
	u8 buf[2];

	if (regmap_bulk_read(regmap, address, buf, 2))
		return -EIO;
	*val = (buf[0] << 4) + ((buf[1] >> 4) & 0x0F);

	return IIO_VAL_INT;
}

/*
 * The current-source used for the battery temp-sensor (TS) is shared
 * with the GPADC. For proper fuel-gauge and charger operation the TS
 * current-source needs to be permanently on. But to read the GPADC we
 * need to temporary switch the TS current-source to ondemand, so that
 * the GPADC can use it, otherwise we will always read an all 0 value.
 */
static int axp288_adc_set_ts(struct axp288_adc_info *info,
			     unsigned int mode, unsigned long address)
{
	int ret;

	/* No need to switch the current-source if the TS pin is disabled */
	if (!info->ts_enabled)
		return 0;

	/* Channels other than GPADC do not need the current source */
	if (address != AXP288_GP_ADC_H)
		return 0;

	ret = regmap_update_bits(info->regmap, AXP288_ADC_TS_PIN_CTRL,
				 AXP288_ADC_TS_CURRENT_ON_OFF_MASK, mode);
	if (ret)
		return ret;

	/* When switching to the GPADC pin give things some time to settle */
	if (mode == AXP288_ADC_TS_CURRENT_ON_ONDEMAND)
		usleep_range(6000, 10000);

	return 0;
}

static int axp288_adc_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	int ret;
	struct axp288_adc_info *info = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (axp288_adc_set_ts(info, AXP288_ADC_TS_CURRENT_ON_ONDEMAND,
					chan->address)) {
			dev_err(&indio_dev->dev, "GPADC mode\n");
			ret = -EINVAL;
			break;
		}
		ret = axp288_adc_read_channel(val, chan->address, info->regmap);
		if (axp288_adc_set_ts(info, AXP288_ADC_TS_CURRENT_ON,
						chan->address))
			dev_err(&indio_dev->dev, "TS pin restore\n");
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int axp288_adc_initialize(struct axp288_adc_info *info)
{
	int ret, adc_enable_val;

	/*
	 * Determine if the TS pin is enabled and set the TS current-source
	 * accordingly.
	 */
	ret = regmap_read(info->regmap, AXP20X_ADC_EN1, &adc_enable_val);
	if (ret)
		return ret;

	if (adc_enable_val & AXP288_ADC_TS_ENABLE) {
		info->ts_enabled = true;
		ret = regmap_update_bits(info->regmap, AXP288_ADC_TS_PIN_CTRL,
					 AXP288_ADC_TS_CURRENT_ON_OFF_MASK,
					 AXP288_ADC_TS_CURRENT_ON);
	} else {
		info->ts_enabled = false;
		ret = regmap_update_bits(info->regmap, AXP288_ADC_TS_PIN_CTRL,
					 AXP288_ADC_TS_CURRENT_ON_OFF_MASK,
					 AXP288_ADC_TS_CURRENT_OFF);
	}
	if (ret)
		return ret;

	/* Turn on the ADC for all channels except TS, leave TS as is */
	return regmap_update_bits(info->regmap, AXP20X_ADC_EN1,
				  AXP288_ADC_EN_MASK, AXP288_ADC_EN_MASK);
}

static const struct iio_info axp288_adc_iio_info = {
	.read_raw = &axp288_adc_read_raw,
};

static int axp288_adc_probe(struct platform_device *pdev)
{
	int ret;
	struct axp288_adc_info *info;
	struct iio_dev *indio_dev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);
	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return info->irq;
	}
	platform_set_drvdata(pdev, indio_dev);
	info->regmap = axp20x->regmap;
	/*
	 * Set ADC to enabled state at all time, including system suspend.
	 * otherwise internal fuel gauge functionality may be affected.
	 */
	ret = axp288_adc_initialize(info);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable ADC device\n");
		return ret;
	}

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;
	indio_dev->channels = axp288_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(axp288_adc_channels);
	indio_dev->info = &axp288_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	ret = iio_map_array_register(indio_dev, axp288_adc_default_maps);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register iio device\n");
		goto err_array_unregister;
	}
	return 0;

err_array_unregister:
	iio_map_array_unregister(indio_dev);

	return ret;
}

static int axp288_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	iio_map_array_unregister(indio_dev);

	return 0;
}

static const struct platform_device_id axp288_adc_id_table[] = {
	{ .name = "axp288_adc" },
	{},
};

static struct platform_driver axp288_adc_driver = {
	.probe = axp288_adc_probe,
	.remove = axp288_adc_remove,
	.id_table = axp288_adc_id_table,
	.driver = {
		.name = "axp288_adc",
	},
};

MODULE_DEVICE_TABLE(platform, axp288_adc_id_table);

module_platform_driver(axp288_adc_driver);

MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@linux.intel.com>");
MODULE_DESCRIPTION("X-Powers AXP288 ADC Driver");
MODULE_LICENSE("GPL");
