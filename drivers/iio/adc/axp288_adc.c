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

#define AXP288_ADC_EN_MASK		0xF1
#define AXP288_ADC_TS_PIN_GPADC		0xF2
#define AXP288_ADC_TS_PIN_ON		0xF3

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
};

static const struct iio_chan_spec const axp288_adc_channels[] = {
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

#define AXP288_ADC_MAP(_adc_channel_label, _consumer_dev_name,	\
		_consumer_channel)				\
	{							\
		.adc_channel_label = _adc_channel_label,	\
		.consumer_dev_name = _consumer_dev_name,	\
		.consumer_channel = _consumer_channel,		\
	}

/* for consumer drivers */
static struct iio_map axp288_adc_default_maps[] = {
	AXP288_ADC_MAP("TS_PIN", "axp288-batt", "axp288-batt-temp"),
	AXP288_ADC_MAP("PMIC_TEMP", "axp288-pmic", "axp288-pmic-temp"),
	AXP288_ADC_MAP("GPADC", "axp288-gpadc", "axp288-system-temp"),
	AXP288_ADC_MAP("BATT_CHG_I", "axp288-chrg", "axp288-chrg-curr"),
	AXP288_ADC_MAP("BATT_DISCHRG_I", "axp288-chrg", "axp288-chrg-d-curr"),
	AXP288_ADC_MAP("BATT_V", "axp288-batt", "axp288-batt-volt"),
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

static int axp288_adc_set_ts(struct regmap *regmap, unsigned int mode,
				unsigned long address)
{
	/* channels other than GPADC do not need to switch TS pin */
	if (address != AXP288_GP_ADC_H)
		return 0;

	return regmap_write(regmap, AXP288_ADC_TS_PIN_CTRL, mode);
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
		if (axp288_adc_set_ts(info->regmap, AXP288_ADC_TS_PIN_GPADC,
					chan->address)) {
			dev_err(&indio_dev->dev, "GPADC mode\n");
			ret = -EINVAL;
			break;
		}
		ret = axp288_adc_read_channel(val, chan->address, info->regmap);
		if (axp288_adc_set_ts(info->regmap, AXP288_ADC_TS_PIN_ON,
						chan->address))
			dev_err(&indio_dev->dev, "TS pin restore\n");
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int axp288_adc_set_state(struct regmap *regmap)
{
	/* ADC should be always enabled for internal FG to function */
	if (regmap_write(regmap, AXP288_ADC_TS_PIN_CTRL, AXP288_ADC_TS_PIN_ON))
		return -EIO;

	return regmap_write(regmap, AXP20X_ADC_EN1, AXP288_ADC_EN_MASK);
}

static const struct iio_info axp288_adc_iio_info = {
	.read_raw = &axp288_adc_read_raw,
	.driver_module = THIS_MODULE,
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
	ret = axp288_adc_set_state(axp20x->regmap);
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

static struct platform_device_id axp288_adc_id_table[] = {
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
