/*
 * STM32 Low-Power Timer Encoder and Counter driver
 *
 * Copyright (C) STMicroelectronics 2017
 *
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>
 *
 * Inspired by 104-quad-8 and stm32-timer-trigger drivers.
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/mfd/stm32-lptimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct stm32_lptim_cnt {
	struct device *dev;
	struct regmap *regmap;
	struct clk *clk;
	u32 preset;
	u32 polarity;
	u32 quadrature_mode;
};

static int stm32_lptim_is_enabled(struct stm32_lptim_cnt *priv)
{
	u32 val;
	int ret;

	ret = regmap_read(priv->regmap, STM32_LPTIM_CR, &val);
	if (ret)
		return ret;

	return FIELD_GET(STM32_LPTIM_ENABLE, val);
}

static int stm32_lptim_set_enable_state(struct stm32_lptim_cnt *priv,
					int enable)
{
	int ret;
	u32 val;

	val = FIELD_PREP(STM32_LPTIM_ENABLE, enable);
	ret = regmap_write(priv->regmap, STM32_LPTIM_CR, val);
	if (ret)
		return ret;

	if (!enable) {
		clk_disable(priv->clk);
		return 0;
	}

	/* LP timer must be enabled before writing CMP & ARR */
	ret = regmap_write(priv->regmap, STM32_LPTIM_ARR, priv->preset);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, STM32_LPTIM_CMP, 0);
	if (ret)
		return ret;

	/* ensure CMP & ARR registers are properly written */
	ret = regmap_read_poll_timeout(priv->regmap, STM32_LPTIM_ISR, val,
				       (val & STM32_LPTIM_CMPOK_ARROK),
				       100, 1000);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, STM32_LPTIM_ICR,
			   STM32_LPTIM_CMPOKCF_ARROKCF);
	if (ret)
		return ret;

	ret = clk_enable(priv->clk);
	if (ret) {
		regmap_write(priv->regmap, STM32_LPTIM_CR, 0);
		return ret;
	}

	/* Start LP timer in continuous mode */
	return regmap_update_bits(priv->regmap, STM32_LPTIM_CR,
				  STM32_LPTIM_CNTSTRT, STM32_LPTIM_CNTSTRT);
}

static int stm32_lptim_setup(struct stm32_lptim_cnt *priv, int enable)
{
	u32 mask = STM32_LPTIM_ENC | STM32_LPTIM_COUNTMODE |
		   STM32_LPTIM_CKPOL | STM32_LPTIM_PRESC;
	u32 val;

	/* Setup LP timer encoder/counter and polarity, without prescaler */
	if (priv->quadrature_mode)
		val = enable ? STM32_LPTIM_ENC : 0;
	else
		val = enable ? STM32_LPTIM_COUNTMODE : 0;
	val |= FIELD_PREP(STM32_LPTIM_CKPOL, enable ? priv->polarity : 0);

	return regmap_update_bits(priv->regmap, STM32_LPTIM_CFGR, mask, val);
}

static int stm32_lptim_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		if (val < 0 || val > 1)
			return -EINVAL;

		/* Check nobody uses the timer, or already disabled/enabled */
		ret = stm32_lptim_is_enabled(priv);
		if ((ret < 0) || (!ret && !val))
			return ret;
		if (val && ret)
			return -EBUSY;

		ret = stm32_lptim_setup(priv, val);
		if (ret)
			return ret;
		return stm32_lptim_set_enable_state(priv, val);

	default:
		return -EINVAL;
	}
}

static int stm32_lptim_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);
	u32 dat;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(priv->regmap, STM32_LPTIM_CNT, &dat);
		if (ret)
			return ret;
		*val = dat;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_ENABLE:
		ret = stm32_lptim_is_enabled(priv);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		/* Non-quadrature mode: scale = 1 */
		*val = 1;
		*val2 = 0;
		if (priv->quadrature_mode) {
			/*
			 * Quadrature encoder mode:
			 * - both edges, quarter cycle, scale is 0.25
			 * - either rising/falling edge scale is 0.5
			 */
			if (priv->polarity > 1)
				*val2 = 2;
			else
				*val2 = 1;
		}
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static const struct iio_info stm32_lptim_cnt_iio_info = {
	.read_raw = stm32_lptim_read_raw,
	.write_raw = stm32_lptim_write_raw,
	.driver_module = THIS_MODULE,
};

static const char *const stm32_lptim_quadrature_modes[] = {
	"non-quadrature",
	"quadrature",
};

static int stm32_lptim_get_quadrature_mode(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);

	return priv->quadrature_mode;
}

static int stm32_lptim_set_quadrature_mode(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   unsigned int type)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	priv->quadrature_mode = type;

	return 0;
}

static const struct iio_enum stm32_lptim_quadrature_mode_en = {
	.items = stm32_lptim_quadrature_modes,
	.num_items = ARRAY_SIZE(stm32_lptim_quadrature_modes),
	.get = stm32_lptim_get_quadrature_mode,
	.set = stm32_lptim_set_quadrature_mode,
};

static const char * const stm32_lptim_cnt_polarity[] = {
	"rising-edge", "falling-edge", "both-edges",
};

static int stm32_lptim_cnt_get_polarity(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);

	return priv->polarity;
}

static int stm32_lptim_cnt_set_polarity(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					unsigned int type)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	priv->polarity = type;

	return 0;
}

static const struct iio_enum stm32_lptim_cnt_polarity_en = {
	.items = stm32_lptim_cnt_polarity,
	.num_items = ARRAY_SIZE(stm32_lptim_cnt_polarity),
	.get = stm32_lptim_cnt_get_polarity,
	.set = stm32_lptim_cnt_set_polarity,
};

static ssize_t stm32_lptim_cnt_get_preset(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", priv->preset);
}

static ssize_t stm32_lptim_cnt_set_preset(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  const char *buf, size_t len)
{
	struct stm32_lptim_cnt *priv = iio_priv(indio_dev);
	int ret;

	if (stm32_lptim_is_enabled(priv))
		return -EBUSY;

	ret = kstrtouint(buf, 0, &priv->preset);
	if (ret)
		return ret;

	if (priv->preset > STM32_LPTIM_MAX_ARR)
		return -EINVAL;

	return len;
}

/* LP timer with encoder */
static const struct iio_chan_spec_ext_info stm32_lptim_enc_ext_info[] = {
	{
		.name = "preset",
		.shared = IIO_SEPARATE,
		.read = stm32_lptim_cnt_get_preset,
		.write = stm32_lptim_cnt_set_preset,
	},
	IIO_ENUM("polarity", IIO_SEPARATE, &stm32_lptim_cnt_polarity_en),
	IIO_ENUM_AVAILABLE("polarity", &stm32_lptim_cnt_polarity_en),
	IIO_ENUM("quadrature_mode", IIO_SEPARATE,
		 &stm32_lptim_quadrature_mode_en),
	IIO_ENUM_AVAILABLE("quadrature_mode", &stm32_lptim_quadrature_mode_en),
	{}
};

static const struct iio_chan_spec stm32_lptim_enc_channels = {
	.type = IIO_COUNT,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_ENABLE) |
			      BIT(IIO_CHAN_INFO_SCALE),
	.ext_info = stm32_lptim_enc_ext_info,
	.indexed = 1,
};

/* LP timer without encoder (counter only) */
static const struct iio_chan_spec_ext_info stm32_lptim_cnt_ext_info[] = {
	{
		.name = "preset",
		.shared = IIO_SEPARATE,
		.read = stm32_lptim_cnt_get_preset,
		.write = stm32_lptim_cnt_set_preset,
	},
	IIO_ENUM("polarity", IIO_SEPARATE, &stm32_lptim_cnt_polarity_en),
	IIO_ENUM_AVAILABLE("polarity", &stm32_lptim_cnt_polarity_en),
	{}
};

static const struct iio_chan_spec stm32_lptim_cnt_channels = {
	.type = IIO_COUNT,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_ENABLE) |
			      BIT(IIO_CHAN_INFO_SCALE),
	.ext_info = stm32_lptim_cnt_ext_info,
	.indexed = 1,
};

static int stm32_lptim_cnt_probe(struct platform_device *pdev)
{
	struct stm32_lptimer *ddata = dev_get_drvdata(pdev->dev.parent);
	struct stm32_lptim_cnt *priv;
	struct iio_dev *indio_dev;

	if (IS_ERR_OR_NULL(ddata))
		return -EINVAL;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dev = &pdev->dev;
	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->preset = STM32_LPTIM_MAX_ARR;

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &stm32_lptim_cnt_iio_info;
	if (ddata->has_encoder)
		indio_dev->channels = &stm32_lptim_enc_channels;
	else
		indio_dev->channels = &stm32_lptim_cnt_channels;
	indio_dev->num_channels = 1;

	platform_set_drvdata(pdev, priv);

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id stm32_lptim_cnt_of_match[] = {
	{ .compatible = "st,stm32-lptimer-counter", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_lptim_cnt_of_match);

static struct platform_driver stm32_lptim_cnt_driver = {
	.probe = stm32_lptim_cnt_probe,
	.driver = {
		.name = "stm32-lptimer-counter",
		.of_match_table = stm32_lptim_cnt_of_match,
	},
};
module_platform_driver(stm32_lptim_cnt_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_ALIAS("platform:stm32-lptimer-counter");
MODULE_DESCRIPTION("STMicroelectronics STM32 LPTIM counter driver");
MODULE_LICENSE("GPL v2");
