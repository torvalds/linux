// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Author: ChiaEn Wu <chiaen_wu@richtek.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/units.h>

#include <dt-bindings/iio/adc/mediatek,mt6370_adc.h>

#define MT6370_REG_DEV_INFO		0x100
#define MT6370_REG_CHG_CTRL3		0x113
#define MT6370_REG_CHG_CTRL7		0x117
#define MT6370_REG_CHG_ADC		0x121
#define MT6370_REG_ADC_DATA_H		0x14C

#define MT6370_ADC_START_MASK		BIT(0)
#define MT6370_ADC_IN_SEL_MASK		GENMASK(7, 4)
#define MT6370_AICR_ICHG_MASK		GENMASK(7, 2)
#define MT6370_VENID_MASK		GENMASK(7, 4)

#define MT6370_AICR_100_mA		0x0
#define MT6370_AICR_150_mA		0x1
#define MT6370_AICR_200_mA		0x2
#define MT6370_AICR_250_mA		0x3
#define MT6370_AICR_300_mA		0x4
#define MT6370_AICR_350_mA		0x5

#define MT6370_ICHG_100_mA		0x0
#define MT6370_ICHG_200_mA		0x1
#define MT6370_ICHG_300_mA		0x2
#define MT6370_ICHG_400_mA		0x3
#define MT6370_ICHG_500_mA		0x4
#define MT6370_ICHG_600_mA		0x5
#define MT6370_ICHG_700_mA		0x6
#define MT6370_ICHG_800_mA		0x7

#define ADC_CONV_TIME_MS		35
#define ADC_CONV_POLLING_TIME_US	1000

#define MT6370_VID_RT5081		0x8
#define MT6370_VID_RT5081A		0xA
#define MT6370_VID_MT6370		0xE

struct mt6370_adc_data {
	struct device *dev;
	struct regmap *regmap;
	/*
	 * This mutex lock is for preventing the different ADC channels
	 * from being read at the same time.
	 */
	struct mutex adc_lock;
	unsigned int vid;
};

static int mt6370_adc_read_channel(struct mt6370_adc_data *priv, int chan,
				   unsigned long addr, int *val)
{
	unsigned int reg_val;
	__be16 be_val;
	int ret;

	mutex_lock(&priv->adc_lock);

	reg_val = MT6370_ADC_START_MASK |
		  FIELD_PREP(MT6370_ADC_IN_SEL_MASK, addr);
	ret = regmap_write(priv->regmap, MT6370_REG_CHG_ADC, reg_val);
	if (ret)
		goto adc_unlock;

	msleep(ADC_CONV_TIME_MS);

	ret = regmap_read_poll_timeout(priv->regmap,
				       MT6370_REG_CHG_ADC, reg_val,
				       !(reg_val & MT6370_ADC_START_MASK),
				       ADC_CONV_POLLING_TIME_US,
				       ADC_CONV_TIME_MS * MILLI * 3);
	if (ret) {
		dev_err(priv->dev, "Failed to read ADC register (%d)\n", ret);
		goto adc_unlock;
	}

	ret = regmap_raw_read(priv->regmap, MT6370_REG_ADC_DATA_H,
			      &be_val, sizeof(be_val));
	if (ret)
		goto adc_unlock;

	*val = be16_to_cpu(be_val);
	ret = IIO_VAL_INT;

adc_unlock:
	mutex_unlock(&priv->adc_lock);

	return ret;
}

static int mt6370_adc_get_ibus_scale(struct mt6370_adc_data *priv)
{
	switch (priv->vid) {
	case MT6370_VID_RT5081:
	case MT6370_VID_RT5081A:
	case MT6370_VID_MT6370:
		return 3350;
	default:
		return 3875;
	}
}

static int mt6370_adc_get_ibat_scale(struct mt6370_adc_data *priv)
{
	switch (priv->vid) {
	case MT6370_VID_RT5081:
	case MT6370_VID_RT5081A:
	case MT6370_VID_MT6370:
		return 2680;
	default:
		return 3870;
	}
}

static int mt6370_adc_read_scale(struct mt6370_adc_data *priv,
				 int chan, int *val1, int *val2)
{
	unsigned int reg_val;
	int ret;

	switch (chan) {
	case MT6370_CHAN_VBAT:
	case MT6370_CHAN_VSYS:
	case MT6370_CHAN_CHG_VDDP:
		*val1 = 5;
		return IIO_VAL_INT;
	case MT6370_CHAN_IBUS:
		ret = regmap_read(priv->regmap, MT6370_REG_CHG_CTRL3, &reg_val);
		if (ret)
			return ret;

		reg_val = FIELD_GET(MT6370_AICR_ICHG_MASK, reg_val);
		switch (reg_val) {
		case MT6370_AICR_100_mA:
		case MT6370_AICR_150_mA:
		case MT6370_AICR_200_mA:
		case MT6370_AICR_250_mA:
		case MT6370_AICR_300_mA:
		case MT6370_AICR_350_mA:
			*val1 = mt6370_adc_get_ibus_scale(priv);
			break;
		default:
			*val1 = 5000;
			break;
		}

		*val2 = 100;

		return IIO_VAL_FRACTIONAL;
	case MT6370_CHAN_IBAT:
		ret = regmap_read(priv->regmap, MT6370_REG_CHG_CTRL7, &reg_val);
		if (ret)
			return ret;

		reg_val = FIELD_GET(MT6370_AICR_ICHG_MASK, reg_val);
		switch (reg_val) {
		case MT6370_ICHG_100_mA:
		case MT6370_ICHG_200_mA:
		case MT6370_ICHG_300_mA:
		case MT6370_ICHG_400_mA:
			*val1 = 2375;
			break;
		case MT6370_ICHG_500_mA:
		case MT6370_ICHG_600_mA:
		case MT6370_ICHG_700_mA:
		case MT6370_ICHG_800_mA:
			*val1 = mt6370_adc_get_ibat_scale(priv);
			break;
		default:
			*val1 = 5000;
			break;
		}

		*val2 = 100;

		return IIO_VAL_FRACTIONAL;
	case MT6370_CHAN_VBUSDIV5:
		*val1 = 25;
		return IIO_VAL_INT;
	case MT6370_CHAN_VBUSDIV2:
		*val1 = 10;
		return IIO_VAL_INT;
	case MT6370_CHAN_TS_BAT:
		*val1 = 25;
		*val2 = 10000;
		return IIO_VAL_FRACTIONAL;
	case MT6370_CHAN_TEMP_JC:
		*val1 = 2000;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int mt6370_adc_read_offset(struct mt6370_adc_data *priv,
				  int chan, int *val)
{
	*val = -20;

	return IIO_VAL_INT;
}

static int mt6370_adc_read_raw(struct iio_dev *iio_dev,
			       const struct iio_chan_spec *chan,
			       int *val, int *val2, long mask)
{
	struct mt6370_adc_data *priv = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return mt6370_adc_read_channel(priv, chan->channel,
					       chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		return mt6370_adc_read_scale(priv, chan->channel, val, val2);
	case IIO_CHAN_INFO_OFFSET:
		return mt6370_adc_read_offset(priv, chan->channel, val);
	default:
		return -EINVAL;
	}
}

static const char * const mt6370_channel_labels[MT6370_CHAN_MAX] = {
	[MT6370_CHAN_VBUSDIV5] = "vbusdiv5",
	[MT6370_CHAN_VBUSDIV2] = "vbusdiv2",
	[MT6370_CHAN_VSYS] = "vsys",
	[MT6370_CHAN_VBAT] = "vbat",
	[MT6370_CHAN_TS_BAT] = "ts_bat",
	[MT6370_CHAN_IBUS] = "ibus",
	[MT6370_CHAN_IBAT] = "ibat",
	[MT6370_CHAN_CHG_VDDP] = "chg_vddp",
	[MT6370_CHAN_TEMP_JC] = "temp_jc",
};

static int mt6370_adc_read_label(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan, char *label)
{
	return sysfs_emit(label, "%s\n", mt6370_channel_labels[chan->channel]);
}

static const struct iio_info mt6370_adc_iio_info = {
	.read_raw = mt6370_adc_read_raw,
	.read_label = mt6370_adc_read_label,
};

#define MT6370_ADC_CHAN(_idx, _type, _addr, _extra_info) {	\
	.type = _type,						\
	.channel = MT6370_CHAN_##_idx,				\
	.address = _addr,					\
	.scan_index = MT6370_CHAN_##_idx,			\
	.indexed = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE) |	\
			      _extra_info,			\
}

static const struct iio_chan_spec mt6370_adc_channels[] = {
	MT6370_ADC_CHAN(VBUSDIV5, IIO_VOLTAGE, 1, 0),
	MT6370_ADC_CHAN(VBUSDIV2, IIO_VOLTAGE, 2, 0),
	MT6370_ADC_CHAN(VSYS, IIO_VOLTAGE, 3, 0),
	MT6370_ADC_CHAN(VBAT, IIO_VOLTAGE, 4, 0),
	MT6370_ADC_CHAN(TS_BAT, IIO_VOLTAGE, 6, 0),
	MT6370_ADC_CHAN(IBUS, IIO_CURRENT, 8, 0),
	MT6370_ADC_CHAN(IBAT, IIO_CURRENT, 9, 0),
	MT6370_ADC_CHAN(CHG_VDDP, IIO_VOLTAGE, 11, 0),
	MT6370_ADC_CHAN(TEMP_JC, IIO_TEMP, 12, BIT(IIO_CHAN_INFO_OFFSET)),
};

static int mt6370_get_vendor_info(struct mt6370_adc_data *priv)
{
	unsigned int dev_info;
	int ret;

	ret = regmap_read(priv->regmap, MT6370_REG_DEV_INFO, &dev_info);
	if (ret)
		return ret;

	priv->vid = FIELD_GET(MT6370_VENID_MASK, dev_info);

	return 0;
}

static int mt6370_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6370_adc_data *priv;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Failed to get regmap\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dev = dev;
	priv->regmap = regmap;
	mutex_init(&priv->adc_lock);

	ret = mt6370_get_vendor_info(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get vid\n");

	ret = regmap_write(priv->regmap, MT6370_REG_CHG_ADC, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset ADC\n");

	indio_dev->name = "mt6370-adc";
	indio_dev->info = &mt6370_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mt6370_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(mt6370_adc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id mt6370_adc_of_id[] = {
	{ .compatible = "mediatek,mt6370-adc", },
	{}
};
MODULE_DEVICE_TABLE(of, mt6370_adc_of_id);

static struct platform_driver mt6370_adc_driver = {
	.driver = {
		.name = "mt6370-adc",
		.of_match_table = mt6370_adc_of_id,
	},
	.probe = mt6370_adc_probe,
};
module_platform_driver(mt6370_adc_driver);

MODULE_AUTHOR("ChiaEn Wu <chiaen_wu@richtek.com>");
MODULE_DESCRIPTION("MT6370 ADC Driver");
MODULE_LICENSE("GPL v2");
