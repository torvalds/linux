// SPDX-License-Identifier: GPL-2.0
/*
 * ADC driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/mfd/jz4740-adc.c
 */

#include <dt-bindings/iio/adc/ingenic,adc.h>
#include <linux/clk.h>
#include <linux/iio/iio.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define JZ_ADC_REG_ENABLE		0x00
#define JZ_ADC_REG_CFG			0x04
#define JZ_ADC_REG_CTRL			0x08
#define JZ_ADC_REG_STATUS		0x0c
#define JZ_ADC_REG_ADTCH		0x18
#define JZ_ADC_REG_ADBDAT		0x1c
#define JZ_ADC_REG_ADSDAT		0x20

#define JZ_ADC_REG_CFG_BAT_MD		BIT(4)

#define JZ_ADC_AUX_VREF				3300
#define JZ_ADC_AUX_VREF_BITS			12
#define JZ_ADC_BATTERY_LOW_VREF			2500
#define JZ_ADC_BATTERY_LOW_VREF_BITS		12
#define JZ4725B_ADC_BATTERY_HIGH_VREF		7500
#define JZ4725B_ADC_BATTERY_HIGH_VREF_BITS	10
#define JZ4740_ADC_BATTERY_HIGH_VREF		(7500 * 0.986)
#define JZ4740_ADC_BATTERY_HIGH_VREF_BITS	12

struct ingenic_adc_soc_data {
	unsigned int battery_high_vref;
	unsigned int battery_high_vref_bits;
	const int *battery_raw_avail;
	size_t battery_raw_avail_size;
	const int *battery_scale_avail;
	size_t battery_scale_avail_size;
};

struct ingenic_adc {
	void __iomem *base;
	struct clk *clk;
	struct mutex lock;
	const struct ingenic_adc_soc_data *soc_data;
	bool low_vref_mode;
};

static void ingenic_adc_set_config(struct ingenic_adc *adc,
				   uint32_t mask,
				   uint32_t val)
{
	uint32_t cfg;

	clk_enable(adc->clk);
	mutex_lock(&adc->lock);

	cfg = readl(adc->base + JZ_ADC_REG_CFG) & ~mask;
	cfg |= val;
	writel(cfg, adc->base + JZ_ADC_REG_CFG);

	mutex_unlock(&adc->lock);
	clk_disable(adc->clk);
}

static void ingenic_adc_enable(struct ingenic_adc *adc,
			       int engine,
			       bool enabled)
{
	u8 val;

	mutex_lock(&adc->lock);
	val = readb(adc->base + JZ_ADC_REG_ENABLE);

	if (enabled)
		val |= BIT(engine);
	else
		val &= ~BIT(engine);

	writeb(val, adc->base + JZ_ADC_REG_ENABLE);
	mutex_unlock(&adc->lock);
}

static int ingenic_adc_capture(struct ingenic_adc *adc,
			       int engine)
{
	u8 val;
	int ret;

	ingenic_adc_enable(adc, engine, true);
	ret = readb_poll_timeout(adc->base + JZ_ADC_REG_ENABLE, val,
				 !(val & BIT(engine)), 250, 1000);
	if (ret)
		ingenic_adc_enable(adc, engine, false);

	return ret;
}

static int ingenic_adc_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 int val,
				 int val2,
				 long m)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel) {
		case INGENIC_ADC_BATTERY:
			if (val > JZ_ADC_BATTERY_LOW_VREF) {
				ingenic_adc_set_config(adc,
						       JZ_ADC_REG_CFG_BAT_MD,
						       0);
				adc->low_vref_mode = false;
			} else {
				ingenic_adc_set_config(adc,
						       JZ_ADC_REG_CFG_BAT_MD,
						       JZ_ADC_REG_CFG_BAT_MD);
				adc->low_vref_mode = true;
			}
			return 0;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const int jz4725b_adc_battery_raw_avail[] = {
	0, 1, (1 << JZ_ADC_BATTERY_LOW_VREF_BITS) - 1,
};

static const int jz4725b_adc_battery_scale_avail[] = {
	JZ4725B_ADC_BATTERY_HIGH_VREF, JZ4725B_ADC_BATTERY_HIGH_VREF_BITS,
	JZ_ADC_BATTERY_LOW_VREF, JZ_ADC_BATTERY_LOW_VREF_BITS,
};

static const int jz4740_adc_battery_raw_avail[] = {
	0, 1, (1 << JZ_ADC_BATTERY_LOW_VREF_BITS) - 1,
};

static const int jz4740_adc_battery_scale_avail[] = {
	JZ4740_ADC_BATTERY_HIGH_VREF, JZ4740_ADC_BATTERY_HIGH_VREF_BITS,
	JZ_ADC_BATTERY_LOW_VREF, JZ_ADC_BATTERY_LOW_VREF_BITS,
};

static const struct ingenic_adc_soc_data jz4725b_adc_soc_data = {
	.battery_high_vref = JZ4725B_ADC_BATTERY_HIGH_VREF,
	.battery_high_vref_bits = JZ4725B_ADC_BATTERY_HIGH_VREF_BITS,
	.battery_raw_avail = jz4725b_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4725b_adc_battery_raw_avail),
	.battery_scale_avail = jz4725b_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4725b_adc_battery_scale_avail),
};

static const struct ingenic_adc_soc_data jz4740_adc_soc_data = {
	.battery_high_vref = JZ4740_ADC_BATTERY_HIGH_VREF,
	.battery_high_vref_bits = JZ4740_ADC_BATTERY_HIGH_VREF_BITS,
	.battery_raw_avail = jz4740_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4740_adc_battery_raw_avail),
	.battery_scale_avail = jz4740_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4740_adc_battery_scale_avail),
};

static int ingenic_adc_read_avail(struct iio_dev *iio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals,
				  int *type,
				  int *length,
				  long m)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		*type = IIO_VAL_INT;
		*length = adc->soc_data->battery_raw_avail_size;
		*vals = adc->soc_data->battery_raw_avail;
		return IIO_AVAIL_RANGE;
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_FRACTIONAL_LOG2;
		*length = adc->soc_data->battery_scale_avail_size;
		*vals = adc->soc_data->battery_scale_avail;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	};
}

static int ingenic_adc_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long m)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		clk_enable(adc->clk);
		ret = ingenic_adc_capture(adc, chan->channel);
		if (ret) {
			clk_disable(adc->clk);
			return ret;
		}

		switch (chan->channel) {
		case INGENIC_ADC_AUX:
			*val = readw(adc->base + JZ_ADC_REG_ADSDAT);
			break;
		case INGENIC_ADC_BATTERY:
			*val = readw(adc->base + JZ_ADC_REG_ADBDAT);
			break;
		}

		clk_disable(adc->clk);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel) {
		case INGENIC_ADC_AUX:
			*val = JZ_ADC_AUX_VREF;
			*val2 = JZ_ADC_AUX_VREF_BITS;
			break;
		case INGENIC_ADC_BATTERY:
			if (adc->low_vref_mode) {
				*val = JZ_ADC_BATTERY_LOW_VREF;
				*val2 = JZ_ADC_BATTERY_LOW_VREF_BITS;
			} else {
				*val = adc->soc_data->battery_high_vref;
				*val2 = adc->soc_data->battery_high_vref_bits;
			}
			break;
		}

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static void ingenic_adc_clk_cleanup(void *data)
{
	clk_unprepare(data);
}

static const struct iio_info ingenic_adc_info = {
	.write_raw = ingenic_adc_write_raw,
	.read_raw = ingenic_adc_read_raw,
	.read_avail = ingenic_adc_read_avail,
};

static const struct iio_chan_spec ingenic_channels[] = {
	{
		.extend_name = "aux",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX,
	},
	{
		.extend_name = "battery",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW) |
						BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_BATTERY,
	},
};

static int ingenic_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *iio_dev;
	struct ingenic_adc *adc;
	struct resource *mem_base;
	const struct ingenic_adc_soc_data *soc_data;
	int ret;

	soc_data = device_get_match_data(dev);
	if (!soc_data)
		return -EINVAL;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!iio_dev)
		return -ENOMEM;

	adc = iio_priv(iio_dev);
	mutex_init(&adc->lock);
	adc->soc_data = soc_data;

	mem_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	adc->base = devm_ioremap_resource(dev, mem_base);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	adc->clk = devm_clk_get(dev, "adc");
	if (IS_ERR(adc->clk)) {
		dev_err(dev, "Unable to get clock\n");
		return PTR_ERR(adc->clk);
	}

	ret = clk_prepare_enable(adc->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}

	/* Put hardware in a known passive state. */
	writeb(0x00, adc->base + JZ_ADC_REG_ENABLE);
	writeb(0xff, adc->base + JZ_ADC_REG_CTRL);
	clk_disable(adc->clk);

	ret = devm_add_action_or_reset(dev, ingenic_adc_clk_cleanup, adc->clk);
	if (ret) {
		dev_err(dev, "Unable to add action\n");
		return ret;
	}

	iio_dev->dev.parent = dev;
	iio_dev->name = "jz-adc";
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->channels = ingenic_channels;
	iio_dev->num_channels = ARRAY_SIZE(ingenic_channels);
	iio_dev->info = &ingenic_adc_info;

	ret = devm_iio_device_register(dev, iio_dev);
	if (ret)
		dev_err(dev, "Unable to register IIO device\n");

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id ingenic_adc_of_match[] = {
	{ .compatible = "ingenic,jz4725b-adc", .data = &jz4725b_adc_soc_data, },
	{ .compatible = "ingenic,jz4740-adc", .data = &jz4740_adc_soc_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, ingenic_adc_of_match);
#endif

static struct platform_driver ingenic_adc_driver = {
	.driver = {
		.name = "ingenic-adc",
		.of_match_table = of_match_ptr(ingenic_adc_of_match),
	},
	.probe = ingenic_adc_probe,
};
module_platform_driver(ingenic_adc_driver);
MODULE_LICENSE("GPL v2");
