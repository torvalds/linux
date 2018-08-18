// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/hwspinlock.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* PMIC global registers definition */
#define SC27XX_MODULE_EN		0xc08
#define SC27XX_MODULE_ADC_EN		BIT(5)
#define SC27XX_ARM_CLK_EN		0xc10
#define SC27XX_CLK_ADC_EN		BIT(5)
#define SC27XX_CLK_ADC_CLK_EN		BIT(6)

/* ADC controller registers definition */
#define SC27XX_ADC_CTL			0x0
#define SC27XX_ADC_CH_CFG		0x4
#define SC27XX_ADC_DATA			0x4c
#define SC27XX_ADC_INT_EN		0x50
#define SC27XX_ADC_INT_CLR		0x54
#define SC27XX_ADC_INT_STS		0x58
#define SC27XX_ADC_INT_RAW		0x5c

/* Bits and mask definition for SC27XX_ADC_CTL register */
#define SC27XX_ADC_EN			BIT(0)
#define SC27XX_ADC_CHN_RUN		BIT(1)
#define SC27XX_ADC_12BIT_MODE		BIT(2)
#define SC27XX_ADC_RUN_NUM_MASK		GENMASK(7, 4)
#define SC27XX_ADC_RUN_NUM_SHIFT	4

/* Bits and mask definition for SC27XX_ADC_CH_CFG register */
#define SC27XX_ADC_CHN_ID_MASK		GENMASK(4, 0)
#define SC27XX_ADC_SCALE_MASK		GENMASK(10, 8)
#define SC27XX_ADC_SCALE_SHIFT		8

/* Bits definitions for SC27XX_ADC_INT_EN registers */
#define SC27XX_ADC_IRQ_EN		BIT(0)

/* Bits definitions for SC27XX_ADC_INT_CLR registers */
#define SC27XX_ADC_IRQ_CLR		BIT(0)

/* Mask definition for SC27XX_ADC_DATA register */
#define SC27XX_ADC_DATA_MASK		GENMASK(11, 0)

/* Timeout (ms) for the trylock of hardware spinlocks */
#define SC27XX_ADC_HWLOCK_TIMEOUT	5000

/* Maximum ADC channel number */
#define SC27XX_ADC_CHANNEL_MAX		32

/* ADC voltage ratio definition */
#define SC27XX_VOLT_RATIO(n, d)		\
	(((n) << SC27XX_RATIO_NUMERATOR_OFFSET) | (d))
#define SC27XX_RATIO_NUMERATOR_OFFSET	16
#define SC27XX_RATIO_DENOMINATOR_MASK	GENMASK(15, 0)

struct sc27xx_adc_data {
	struct device *dev;
	struct regmap *regmap;
	/*
	 * One hardware spinlock to synchronize between the multiple
	 * subsystems which will access the unique ADC controller.
	 */
	struct hwspinlock *hwlock;
	struct completion completion;
	int channel_scale[SC27XX_ADC_CHANNEL_MAX];
	u32 base;
	int value;
	int irq;
};

struct sc27xx_adc_linear_graph {
	int volt0;
	int adc0;
	int volt1;
	int adc1;
};

/*
 * According to the datasheet, we can convert one ADC value to one voltage value
 * through 2 points in the linear graph. If the voltage is less than 1.2v, we
 * should use the small-scale graph, and if more than 1.2v, we should use the
 * big-scale graph.
 */
static const struct sc27xx_adc_linear_graph big_scale_graph = {
	4200, 3310,
	3600, 2832,
};

static const struct sc27xx_adc_linear_graph small_scale_graph = {
	1000, 3413,
	100, 341,
};

static int sc27xx_adc_get_ratio(int channel, int scale)
{
	switch (channel) {
	case 1:
	case 2:
	case 3:
	case 4:
		return scale ? SC27XX_VOLT_RATIO(400, 1025) :
			SC27XX_VOLT_RATIO(1, 1);
	case 5:
		return SC27XX_VOLT_RATIO(7, 29);
	case 6:
		return SC27XX_VOLT_RATIO(375, 9000);
	case 7:
	case 8:
		return scale ? SC27XX_VOLT_RATIO(100, 125) :
			SC27XX_VOLT_RATIO(1, 1);
	case 19:
		return SC27XX_VOLT_RATIO(1, 3);
	default:
		return SC27XX_VOLT_RATIO(1, 1);
	}
	return SC27XX_VOLT_RATIO(1, 1);
}

static int sc27xx_adc_read(struct sc27xx_adc_data *data, int channel,
			   int scale, int *val)
{
	int ret;
	u32 tmp;

	reinit_completion(&data->completion);

	ret = hwspin_lock_timeout_raw(data->hwlock, SC27XX_ADC_HWLOCK_TIMEOUT);
	if (ret) {
		dev_err(data->dev, "timeout to get the hwspinlock\n");
		return ret;
	}

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
				 SC27XX_ADC_EN, SC27XX_ADC_EN);
	if (ret)
		goto unlock_adc;

	/* Configure the channel id and scale */
	tmp = (scale << SC27XX_ADC_SCALE_SHIFT) & SC27XX_ADC_SCALE_MASK;
	tmp |= channel & SC27XX_ADC_CHN_ID_MASK;
	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CH_CFG,
				 SC27XX_ADC_CHN_ID_MASK | SC27XX_ADC_SCALE_MASK,
				 tmp);
	if (ret)
		goto disable_adc;

	/* Select 12bit conversion mode, and only sample 1 time */
	tmp = SC27XX_ADC_12BIT_MODE;
	tmp |= (0 << SC27XX_ADC_RUN_NUM_SHIFT) & SC27XX_ADC_RUN_NUM_MASK;
	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
				 SC27XX_ADC_RUN_NUM_MASK | SC27XX_ADC_12BIT_MODE,
				 tmp);
	if (ret)
		goto disable_adc;

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
				 SC27XX_ADC_CHN_RUN, SC27XX_ADC_CHN_RUN);
	if (ret)
		goto disable_adc;

	wait_for_completion(&data->completion);

disable_adc:
	regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
			   SC27XX_ADC_EN, 0);
unlock_adc:
	hwspin_unlock_raw(data->hwlock);

	if (!ret)
		*val = data->value;

	return ret;
}

static irqreturn_t sc27xx_adc_isr(int irq, void *dev_id)
{
	struct sc27xx_adc_data *data = dev_id;
	int ret;

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_INT_CLR,
				 SC27XX_ADC_IRQ_CLR, SC27XX_ADC_IRQ_CLR);
	if (ret)
		return IRQ_RETVAL(ret);

	ret = regmap_read(data->regmap, data->base + SC27XX_ADC_DATA,
			  &data->value);
	if (ret)
		return IRQ_RETVAL(ret);

	data->value &= SC27XX_ADC_DATA_MASK;
	complete(&data->completion);

	return IRQ_HANDLED;
}

static void sc27xx_adc_volt_ratio(struct sc27xx_adc_data *data,
				  int channel, int scale,
				  u32 *div_numerator, u32 *div_denominator)
{
	u32 ratio = sc27xx_adc_get_ratio(channel, scale);

	*div_numerator = ratio >> SC27XX_RATIO_NUMERATOR_OFFSET;
	*div_denominator = ratio & SC27XX_RATIO_DENOMINATOR_MASK;
}

static int sc27xx_adc_to_volt(const struct sc27xx_adc_linear_graph *graph,
			      int raw_adc)
{
	int tmp;

	tmp = (graph->volt0 - graph->volt1) * (raw_adc - graph->adc1);
	tmp /= (graph->adc0 - graph->adc1);
	tmp += graph->volt1;

	return tmp < 0 ? 0 : tmp;
}

static int sc27xx_adc_convert_volt(struct sc27xx_adc_data *data, int channel,
				   int scale, int raw_adc)
{
	u32 numerator, denominator;
	u32 volt;

	/*
	 * Convert ADC values to voltage values according to the linear graph,
	 * and channel 5 and channel 1 has been calibrated, so we can just
	 * return the voltage values calculated by the linear graph. But other
	 * channels need be calculated to the real voltage values with the
	 * voltage ratio.
	 */
	switch (channel) {
	case 5:
		return sc27xx_adc_to_volt(&big_scale_graph, raw_adc);

	case 1:
		return sc27xx_adc_to_volt(&small_scale_graph, raw_adc);

	default:
		volt = sc27xx_adc_to_volt(&small_scale_graph, raw_adc);
		break;
	}

	sc27xx_adc_volt_ratio(data, channel, scale, &numerator, &denominator);

	return (volt * denominator + numerator / 2) / numerator;
}

static int sc27xx_adc_read_processed(struct sc27xx_adc_data *data,
				     int channel, int scale, int *val)
{
	int ret, raw_adc;

	ret = sc27xx_adc_read(data, channel, scale, &raw_adc);
	if (ret)
		return ret;

	*val = sc27xx_adc_convert_volt(data, channel, scale, raw_adc);
	return 0;
}

static int sc27xx_adc_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct sc27xx_adc_data *data = iio_priv(indio_dev);
	int scale = data->channel_scale[chan->channel];
	int ret, tmp;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&indio_dev->mlock);
		ret = sc27xx_adc_read_processed(data, chan->channel, scale,
						&tmp);
		mutex_unlock(&indio_dev->mlock);

		if (ret)
			return ret;

		*val = tmp;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = scale;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int sc27xx_adc_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct sc27xx_adc_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		data->channel_scale[chan->channel] = val;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static const struct iio_info sc27xx_info = {
	.read_raw = &sc27xx_adc_read_raw,
	.write_raw = &sc27xx_adc_write_raw,
};

#define SC27XX_ADC_CHANNEL(index) {				\
	.type = IIO_VOLTAGE,					\
	.channel = index,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |	\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.datasheet_name = "CH##index",				\
	.indexed = 1,						\
}

static const struct iio_chan_spec sc27xx_channels[] = {
	SC27XX_ADC_CHANNEL(0),
	SC27XX_ADC_CHANNEL(1),
	SC27XX_ADC_CHANNEL(2),
	SC27XX_ADC_CHANNEL(3),
	SC27XX_ADC_CHANNEL(4),
	SC27XX_ADC_CHANNEL(5),
	SC27XX_ADC_CHANNEL(6),
	SC27XX_ADC_CHANNEL(7),
	SC27XX_ADC_CHANNEL(8),
	SC27XX_ADC_CHANNEL(9),
	SC27XX_ADC_CHANNEL(10),
	SC27XX_ADC_CHANNEL(11),
	SC27XX_ADC_CHANNEL(12),
	SC27XX_ADC_CHANNEL(13),
	SC27XX_ADC_CHANNEL(14),
	SC27XX_ADC_CHANNEL(15),
	SC27XX_ADC_CHANNEL(16),
	SC27XX_ADC_CHANNEL(17),
	SC27XX_ADC_CHANNEL(18),
	SC27XX_ADC_CHANNEL(19),
	SC27XX_ADC_CHANNEL(20),
	SC27XX_ADC_CHANNEL(21),
	SC27XX_ADC_CHANNEL(22),
	SC27XX_ADC_CHANNEL(23),
	SC27XX_ADC_CHANNEL(24),
	SC27XX_ADC_CHANNEL(25),
	SC27XX_ADC_CHANNEL(26),
	SC27XX_ADC_CHANNEL(27),
	SC27XX_ADC_CHANNEL(28),
	SC27XX_ADC_CHANNEL(29),
	SC27XX_ADC_CHANNEL(30),
	SC27XX_ADC_CHANNEL(31),
};

static int sc27xx_adc_enable(struct sc27xx_adc_data *data)
{
	int ret;

	ret = regmap_update_bits(data->regmap, SC27XX_MODULE_EN,
				 SC27XX_MODULE_ADC_EN, SC27XX_MODULE_ADC_EN);
	if (ret)
		return ret;

	/* Enable ADC work clock and controller clock */
	ret = regmap_update_bits(data->regmap, SC27XX_ARM_CLK_EN,
				 SC27XX_CLK_ADC_EN | SC27XX_CLK_ADC_CLK_EN,
				 SC27XX_CLK_ADC_EN | SC27XX_CLK_ADC_CLK_EN);
	if (ret)
		goto disable_adc;

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_INT_EN,
				 SC27XX_ADC_IRQ_EN, SC27XX_ADC_IRQ_EN);
	if (ret)
		goto disable_clk;

	return 0;

disable_clk:
	regmap_update_bits(data->regmap, SC27XX_ARM_CLK_EN,
			   SC27XX_CLK_ADC_EN | SC27XX_CLK_ADC_CLK_EN, 0);
disable_adc:
	regmap_update_bits(data->regmap, SC27XX_MODULE_EN,
			   SC27XX_MODULE_ADC_EN, 0);

	return ret;
}

static void sc27xx_adc_disable(void *_data)
{
	struct sc27xx_adc_data *data = _data;

	regmap_update_bits(data->regmap, data->base + SC27XX_ADC_INT_EN,
			   SC27XX_ADC_IRQ_EN, 0);

	/* Disable ADC work clock and controller clock */
	regmap_update_bits(data->regmap, SC27XX_ARM_CLK_EN,
			   SC27XX_CLK_ADC_EN | SC27XX_CLK_ADC_CLK_EN, 0);

	regmap_update_bits(data->regmap, SC27XX_MODULE_EN,
			   SC27XX_MODULE_ADC_EN, 0);
}

static void sc27xx_adc_free_hwlock(void *_data)
{
	struct hwspinlock *hwlock = _data;

	hwspin_lock_free(hwlock);
}

static int sc27xx_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sc27xx_adc_data *sc27xx_data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*sc27xx_data));
	if (!indio_dev)
		return -ENOMEM;

	sc27xx_data = iio_priv(indio_dev);

	sc27xx_data->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!sc27xx_data->regmap) {
		dev_err(&pdev->dev, "failed to get ADC regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &sc27xx_data->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get ADC base address\n");
		return ret;
	}

	sc27xx_data->irq = platform_get_irq(pdev, 0);
	if (sc27xx_data->irq < 0) {
		dev_err(&pdev->dev, "failed to get ADC irq number\n");
		return sc27xx_data->irq;
	}

	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get hwspinlock id\n");
		return ret;
	}

	sc27xx_data->hwlock = hwspin_lock_request_specific(ret);
	if (!sc27xx_data->hwlock) {
		dev_err(&pdev->dev, "failed to request hwspinlock\n");
		return -ENXIO;
	}

	ret = devm_add_action(&pdev->dev, sc27xx_adc_free_hwlock,
			      sc27xx_data->hwlock);
	if (ret) {
		sc27xx_adc_free_hwlock(sc27xx_data->hwlock);
		dev_err(&pdev->dev, "failed to add hwspinlock action\n");
		return ret;
	}

	init_completion(&sc27xx_data->completion);
	sc27xx_data->dev = &pdev->dev;

	ret = sc27xx_adc_enable(sc27xx_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable ADC module\n");
		return ret;
	}

	ret = devm_add_action(&pdev->dev, sc27xx_adc_disable, sc27xx_data);
	if (ret) {
		sc27xx_adc_disable(sc27xx_data);
		dev_err(&pdev->dev, "failed to add ADC disable action\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, sc27xx_data->irq, NULL,
					sc27xx_adc_isr, IRQF_ONESHOT,
					pdev->name, sc27xx_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to request ADC irq\n");
		return ret;
	}

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &sc27xx_info;
	indio_dev->channels = sc27xx_channels;
	indio_dev->num_channels = ARRAY_SIZE(sc27xx_channels);
	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		dev_err(&pdev->dev, "could not register iio (ADC)");

	return ret;
}

static const struct of_device_id sc27xx_adc_of_match[] = {
	{ .compatible = "sprd,sc2731-adc", },
	{ }
};

static struct platform_driver sc27xx_adc_driver = {
	.probe = sc27xx_adc_probe,
	.driver = {
		.name = "sc27xx-adc",
		.of_match_table = sc27xx_adc_of_match,
	},
};

module_platform_driver(sc27xx_adc_driver);

MODULE_AUTHOR("Freeman Liu <freeman.liu@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum SC27XX ADC Driver");
MODULE_LICENSE("GPL v2");
