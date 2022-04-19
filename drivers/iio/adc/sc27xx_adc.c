// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/hwspinlock.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

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
#define SC27XX_ADC_SCALE_MASK		GENMASK(10, 9)
#define SC27XX_ADC_SCALE_SHIFT		9

/* Bits definitions for SC27XX_ADC_INT_EN registers */
#define SC27XX_ADC_IRQ_EN		BIT(0)

/* Bits definitions for SC27XX_ADC_INT_CLR registers */
#define SC27XX_ADC_IRQ_CLR		BIT(0)

/* Bits definitions for SC27XX_ADC_INT_RAW registers */
#define SC27XX_ADC_IRQ_RAW		BIT(0)

/* Mask definition for SC27XX_ADC_DATA register */
#define SC27XX_ADC_DATA_MASK		GENMASK(11, 0)

/* Timeout (ms) for the trylock of hardware spinlocks */
#define SC27XX_ADC_HWLOCK_TIMEOUT	5000

/* Timeout (us) for ADC data conversion according to ADC datasheet */
#define SC27XX_ADC_RDY_TIMEOUT		1000000
#define SC27XX_ADC_POLL_RAW_STATUS	500

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
	int channel_scale[SC27XX_ADC_CHANNEL_MAX];
	u32 base;
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
static struct sc27xx_adc_linear_graph big_scale_graph = {
	4200, 3310,
	3600, 2832,
};

static struct sc27xx_adc_linear_graph small_scale_graph = {
	1000, 3413,
	100, 341,
};

static const struct sc27xx_adc_linear_graph big_scale_graph_calib = {
	4200, 856,
	3600, 733,
};

static const struct sc27xx_adc_linear_graph small_scale_graph_calib = {
	1000, 833,
	100, 80,
};

static int sc27xx_adc_get_calib_data(u32 calib_data, int calib_adc)
{
	return ((calib_data & 0xff) + calib_adc - 128) * 4;
}

static int sc27xx_adc_scale_calibration(struct sc27xx_adc_data *data,
					bool big_scale)
{
	const struct sc27xx_adc_linear_graph *calib_graph;
	struct sc27xx_adc_linear_graph *graph;
	struct nvmem_cell *cell;
	const char *cell_name;
	u32 calib_data = 0;
	void *buf;
	size_t len;

	if (big_scale) {
		calib_graph = &big_scale_graph_calib;
		graph = &big_scale_graph;
		cell_name = "big_scale_calib";
	} else {
		calib_graph = &small_scale_graph_calib;
		graph = &small_scale_graph;
		cell_name = "small_scale_calib";
	}

	cell = nvmem_cell_get(data->dev, cell_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(&calib_data, buf, min(len, sizeof(u32)));

	/* Only need to calibrate the adc values in the linear graph. */
	graph->adc0 = sc27xx_adc_get_calib_data(calib_data, calib_graph->adc0);
	graph->adc1 = sc27xx_adc_get_calib_data(calib_data >> 8,
						calib_graph->adc1);

	kfree(buf);
	return 0;
}

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
	u32 tmp, value, status;

	ret = hwspin_lock_timeout_raw(data->hwlock, SC27XX_ADC_HWLOCK_TIMEOUT);
	if (ret) {
		dev_err(data->dev, "timeout to get the hwspinlock\n");
		return ret;
	}

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
				 SC27XX_ADC_EN, SC27XX_ADC_EN);
	if (ret)
		goto unlock_adc;

	ret = regmap_update_bits(data->regmap, data->base + SC27XX_ADC_INT_CLR,
				 SC27XX_ADC_IRQ_CLR, SC27XX_ADC_IRQ_CLR);
	if (ret)
		goto disable_adc;

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

	ret = regmap_read_poll_timeout(data->regmap,
				       data->base + SC27XX_ADC_INT_RAW,
				       status, (status & SC27XX_ADC_IRQ_RAW),
				       SC27XX_ADC_POLL_RAW_STATUS,
				       SC27XX_ADC_RDY_TIMEOUT);
	if (ret) {
		dev_err(data->dev, "read adc timeout, status = 0x%x\n", status);
		goto disable_adc;
	}

	ret = regmap_read(data->regmap, data->base + SC27XX_ADC_DATA, &value);
	if (ret)
		goto disable_adc;

	value &= SC27XX_ADC_DATA_MASK;

disable_adc:
	regmap_update_bits(data->regmap, data->base + SC27XX_ADC_CTL,
			   SC27XX_ADC_EN, 0);
unlock_adc:
	hwspin_unlock_raw(data->hwlock);

	if (!ret)
		*val = value;

	return ret;
}

static void sc27xx_adc_volt_ratio(struct sc27xx_adc_data *data,
				  int channel, int scale,
				  u32 *div_numerator, u32 *div_denominator)
{
	u32 ratio = sc27xx_adc_get_ratio(channel, scale);

	*div_numerator = ratio >> SC27XX_RATIO_NUMERATOR_OFFSET;
	*div_denominator = ratio & SC27XX_RATIO_DENOMINATOR_MASK;
}

static int sc27xx_adc_to_volt(struct sc27xx_adc_linear_graph *graph,
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

	return DIV_ROUND_CLOSEST(volt * denominator, numerator);
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
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = sc27xx_adc_read(data, chan->channel, scale, &tmp);
		mutex_unlock(&indio_dev->mlock);

		if (ret)
			return ret;

		*val = tmp;
		return IIO_VAL_INT;

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

#define SC27XX_ADC_CHANNEL(index, mask) {			\
	.type = IIO_VOLTAGE,					\
	.channel = index,					\
	.info_mask_separate = mask | BIT(IIO_CHAN_INFO_SCALE),	\
	.datasheet_name = "CH##index",				\
	.indexed = 1,						\
}

static const struct iio_chan_spec sc27xx_channels[] = {
	SC27XX_ADC_CHANNEL(0, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(1, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(2, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(3, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(4, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(5, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(6, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(7, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(8, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(9, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(10, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(11, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(12, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(13, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(14, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(15, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(16, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(17, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(18, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(19, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(20, BIT(IIO_CHAN_INFO_RAW)),
	SC27XX_ADC_CHANNEL(21, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(22, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(23, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(24, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(25, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(26, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(27, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(28, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(29, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(30, BIT(IIO_CHAN_INFO_PROCESSED)),
	SC27XX_ADC_CHANNEL(31, BIT(IIO_CHAN_INFO_PROCESSED)),
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

	/* ADC channel scales' calibration from nvmem device */
	ret = sc27xx_adc_scale_calibration(data, true);
	if (ret)
		goto disable_clk;

	ret = sc27xx_adc_scale_calibration(data, false);
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

	/* Disable ADC work clock and controller clock */
	regmap_update_bits(data->regmap, SC27XX_ARM_CLK_EN,
			   SC27XX_CLK_ADC_EN | SC27XX_CLK_ADC_CLK_EN, 0);

	regmap_update_bits(data->regmap, SC27XX_MODULE_EN,
			   SC27XX_MODULE_ADC_EN, 0);
}

static int sc27xx_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sc27xx_adc_data *sc27xx_data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*sc27xx_data));
	if (!indio_dev)
		return -ENOMEM;

	sc27xx_data = iio_priv(indio_dev);

	sc27xx_data->regmap = dev_get_regmap(dev->parent, NULL);
	if (!sc27xx_data->regmap) {
		dev_err(dev, "failed to get ADC regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "reg", &sc27xx_data->base);
	if (ret) {
		dev_err(dev, "failed to get ADC base address\n");
		return ret;
	}

	sc27xx_data->irq = platform_get_irq(pdev, 0);
	if (sc27xx_data->irq < 0)
		return sc27xx_data->irq;

	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		dev_err(dev, "failed to get hwspinlock id\n");
		return ret;
	}

	sc27xx_data->hwlock = devm_hwspin_lock_request_specific(dev, ret);
	if (!sc27xx_data->hwlock) {
		dev_err(dev, "failed to request hwspinlock\n");
		return -ENXIO;
	}

	sc27xx_data->dev = dev;

	ret = sc27xx_adc_enable(sc27xx_data);
	if (ret) {
		dev_err(dev, "failed to enable ADC module\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, sc27xx_adc_disable, sc27xx_data);
	if (ret) {
		dev_err(dev, "failed to add ADC disable action\n");
		return ret;
	}

	indio_dev->name = dev_name(dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &sc27xx_info;
	indio_dev->channels = sc27xx_channels;
	indio_dev->num_channels = ARRAY_SIZE(sc27xx_channels);
	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		dev_err(dev, "could not register iio (ADC)");

	return ret;
}

static const struct of_device_id sc27xx_adc_of_match[] = {
	{ .compatible = "sprd,sc2731-adc", },
	{ }
};
MODULE_DEVICE_TABLE(of, sc27xx_adc_of_match);

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
