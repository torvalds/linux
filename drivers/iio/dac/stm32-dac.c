// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 DAC driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Authors: Amelie Delaunay <amelie.delaunay@st.com>
 *	    Fabrice Gasnier <fabrice.gasnier@st.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "stm32-dac-core.h"

#define STM32_DAC_CHANNEL_1		1
#define STM32_DAC_CHANNEL_2		2
#define STM32_DAC_IS_CHAN_1(ch)		((ch) & STM32_DAC_CHANNEL_1)

#define STM32_DAC_AUTO_SUSPEND_DELAY_MS	2000

/**
 * struct stm32_dac - private data of DAC driver
 * @common:		reference to DAC common data
 */
struct stm32_dac {
	struct stm32_dac_common *common;
};

static int stm32_dac_is_enabled(struct iio_dev *indio_dev, int channel)
{
	struct stm32_dac *dac = iio_priv(indio_dev);
	u32 en, val;
	int ret;

	ret = regmap_read(dac->common->regmap, STM32_DAC_CR, &val);
	if (ret < 0)
		return ret;
	if (STM32_DAC_IS_CHAN_1(channel))
		en = FIELD_GET(STM32_DAC_CR_EN1, val);
	else
		en = FIELD_GET(STM32_DAC_CR_EN2, val);

	return !!en;
}

static int stm32_dac_set_enable_state(struct iio_dev *indio_dev, int ch,
				      bool enable)
{
	struct stm32_dac *dac = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	u32 msk = STM32_DAC_IS_CHAN_1(ch) ? STM32_DAC_CR_EN1 : STM32_DAC_CR_EN2;
	u32 en = enable ? msk : 0;
	int ret;

	/* already enabled / disabled ? */
	mutex_lock(&indio_dev->mlock);
	ret = stm32_dac_is_enabled(indio_dev, ch);
	if (ret < 0 || enable == !!ret) {
		mutex_unlock(&indio_dev->mlock);
		return ret < 0 ? ret : 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			pm_runtime_put_noidle(dev);
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
	}

	ret = regmap_update_bits(dac->common->regmap, STM32_DAC_CR, msk, en);
	mutex_unlock(&indio_dev->mlock);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "%s failed\n", en ?
			"Enable" : "Disable");
		goto err_put_pm;
	}

	/*
	 * When HFSEL is set, it is not allowed to write the DHRx register
	 * during 8 clock cycles after the ENx bit is set. It is not allowed
	 * to make software/hardware trigger during this period either.
	 */
	if (en && dac->common->hfsel)
		udelay(1);

	if (!enable) {
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}

	return 0;

err_put_pm:
	if (enable) {
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}

	return ret;
}

static int stm32_dac_get_value(struct stm32_dac *dac, int channel, int *val)
{
	int ret;

	if (STM32_DAC_IS_CHAN_1(channel))
		ret = regmap_read(dac->common->regmap, STM32_DAC_DOR1, val);
	else
		ret = regmap_read(dac->common->regmap, STM32_DAC_DOR2, val);

	return ret ? ret : IIO_VAL_INT;
}

static int stm32_dac_set_value(struct stm32_dac *dac, int channel, int val)
{
	int ret;

	if (STM32_DAC_IS_CHAN_1(channel))
		ret = regmap_write(dac->common->regmap, STM32_DAC_DHR12R1, val);
	else
		ret = regmap_write(dac->common->regmap, STM32_DAC_DHR12R2, val);

	return ret;
}

static int stm32_dac_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct stm32_dac *dac = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return stm32_dac_get_value(dac, chan->channel, val);
	case IIO_CHAN_INFO_SCALE:
		*val = dac->common->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int stm32_dac_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct stm32_dac *dac = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return stm32_dac_set_value(dac, chan->channel, val);
	default:
		return -EINVAL;
	}
}

static int stm32_dac_debugfs_reg_access(struct iio_dev *indio_dev,
					unsigned reg, unsigned writeval,
					unsigned *readval)
{
	struct stm32_dac *dac = iio_priv(indio_dev);

	if (!readval)
		return regmap_write(dac->common->regmap, reg, writeval);
	else
		return regmap_read(dac->common->regmap, reg, readval);
}

static const struct iio_info stm32_dac_iio_info = {
	.read_raw = stm32_dac_read_raw,
	.write_raw = stm32_dac_write_raw,
	.debugfs_reg_access = stm32_dac_debugfs_reg_access,
};

static const char * const stm32_dac_powerdown_modes[] = {
	"three_state",
};

static int stm32_dac_get_powerdown_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	return 0;
}

static int stm32_dac_set_powerdown_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					unsigned int type)
{
	return 0;
}

static ssize_t stm32_dac_read_powerdown(struct iio_dev *indio_dev,
					uintptr_t private,
					const struct iio_chan_spec *chan,
					char *buf)
{
	int ret = stm32_dac_is_enabled(indio_dev, chan->channel);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret ? 0 : 1);
}

static ssize_t stm32_dac_write_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 const char *buf, size_t len)
{
	bool powerdown;
	int ret;

	ret = strtobool(buf, &powerdown);
	if (ret)
		return ret;

	ret = stm32_dac_set_enable_state(indio_dev, chan->channel, !powerdown);
	if (ret)
		return ret;

	return len;
}

static const struct iio_enum stm32_dac_powerdown_mode_en = {
	.items = stm32_dac_powerdown_modes,
	.num_items = ARRAY_SIZE(stm32_dac_powerdown_modes),
	.get = stm32_dac_get_powerdown_mode,
	.set = stm32_dac_set_powerdown_mode,
};

static const struct iio_chan_spec_ext_info stm32_dac_ext_info[] = {
	{
		.name = "powerdown",
		.read = stm32_dac_read_powerdown,
		.write = stm32_dac_write_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &stm32_dac_powerdown_mode_en),
	IIO_ENUM_AVAILABLE("powerdown_mode", &stm32_dac_powerdown_mode_en),
	{},
};

#define STM32_DAC_CHANNEL(chan, name) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.output = 1,					\
	.channel = chan,				\
	.info_mask_separate =				\
		BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_SCALE),		\
	/* scan_index is always 0 as num_channels is 1 */ \
	.scan_type = {					\
		.sign = 'u',				\
		.realbits = 12,				\
		.storagebits = 16,			\
	},						\
	.datasheet_name = name,				\
	.ext_info = stm32_dac_ext_info			\
}

static const struct iio_chan_spec stm32_dac_channels[] = {
	STM32_DAC_CHANNEL(STM32_DAC_CHANNEL_1, "out1"),
	STM32_DAC_CHANNEL(STM32_DAC_CHANNEL_2, "out2"),
};

static int stm32_dac_chan_of_init(struct iio_dev *indio_dev)
{
	struct device_node *np = indio_dev->dev.of_node;
	unsigned int i;
	u32 channel;
	int ret;

	ret = of_property_read_u32(np, "reg", &channel);
	if (ret) {
		dev_err(&indio_dev->dev, "Failed to read reg property\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(stm32_dac_channels); i++) {
		if (stm32_dac_channels[i].channel == channel)
			break;
	}
	if (i >= ARRAY_SIZE(stm32_dac_channels)) {
		dev_err(&indio_dev->dev, "Invalid reg property\n");
		return -EINVAL;
	}

	indio_dev->channels = &stm32_dac_channels[i];
	/*
	 * Expose only one channel here, as they can be used independently,
	 * with separate trigger. Then separate IIO devices are instantiated
	 * to manage this.
	 */
	indio_dev->num_channels = 1;

	return 0;
};

static int stm32_dac_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct stm32_dac *dac;
	int ret;

	if (!np)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*dac));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	dac = iio_priv(indio_dev);
	dac->common = dev_get_drvdata(pdev->dev.parent);
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &stm32_dac_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = stm32_dac_chan_of_init(indio_dev);
	if (ret < 0)
		return ret;

	/* Get stm32-dac-core PM online */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, STM32_DAC_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_pm_put;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_pm_put:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	return ret;
}

static int stm32_dac_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	iio_device_unregister(indio_dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static int __maybe_unused stm32_dac_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	int channel = indio_dev->channels[0].channel;
	int ret;

	/* Ensure DAC is disabled before suspend */
	ret = stm32_dac_is_enabled(indio_dev, channel);
	if (ret)
		return ret < 0 ? ret : -EBUSY;

	return pm_runtime_force_suspend(dev);
}

static const struct dev_pm_ops stm32_dac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_dac_suspend, pm_runtime_force_resume)
};

static const struct of_device_id stm32_dac_of_match[] = {
	{ .compatible = "st,stm32-dac", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_dac_of_match);

static struct platform_driver stm32_dac_driver = {
	.probe = stm32_dac_probe,
	.remove = stm32_dac_remove,
	.driver = {
		.name = "stm32-dac",
		.of_match_table = stm32_dac_of_match,
		.pm = &stm32_dac_pm_ops,
	},
};
module_platform_driver(stm32_dac_driver);

MODULE_ALIAS("platform:stm32-dac");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 DAC driver");
MODULE_LICENSE("GPL v2");
