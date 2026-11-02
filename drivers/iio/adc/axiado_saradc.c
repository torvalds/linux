// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2026 Axiado Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/iio.h>

/* Register offsets */
#define AX_SARADC_GLOBAL_CTRL_REG	0x0004
#define AX_SARADC_MANUAL_CTRL_REG	0x0008
#define AX_SARADC_DOUT_REG	0x001C

/* GLOBAL_CTRL register fields */
#define AX_SARADC_GLOBAL_CTRL_CH_EN_MASK	GENMASK(31, 16)
#define AX_SARADC_GLOBAL_CTRL_SAMPLE_MASK	GENMASK(6, 5)
#define AX_SARADC_GLOBAL_CTRL_MODE_MASK		GENMASK(4, 3)
#define AX_SARADC_GLOBAL_CTRL_PD		BIT(2)
#define AX_SARADC_GLOBAL_CTRL_ENABLE		BIT(0)

/* GLOBAL_CTRL SAMPLE_MASK field value: 0 selects 16 samples. */
#define AX_SARADC_GLOBAL_CTRL_SAMPLE_16	0

/* GLOBAL_CTRL MODE_MASK field value: 1 selects manual mode. */
#define AX_SARADC_GLOBAL_CTRL_MODE_MANUAL	1

/* MANUAL_CTRL register fields */
#define AX_SARADC_MANUAL_CTRL_ENABLE	BIT(0)
#define AX_SARADC_MANUAL_CTRL_CH_SEL_MASK	GENMASK(4, 1)

#define AX_RESOLUTION_BITS	10
#define AX_SARADC_CONV_CYCLES	13
#define AX_SARADC_CONV_DELAY_MARGIN_US	10

struct axiado_saradc {
	struct regmap *regmap;
	struct mutex lock; /* Serializes ADC conversions. */
	unsigned long clk_rate;
	int vref_uV;
};

/*
 * Registers contain transient control, status, and conversion data,
 * so accesses must always go directly to hardware.
 */
static const struct regmap_config axiado_saradc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = AX_SARADC_DOUT_REG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int axiado_saradc_conversion(struct axiado_saradc *info,
				    struct iio_chan_spec const *chan, int *val)
{
	unsigned long usecs;
	unsigned int regval;
	int ret;

	guard(mutex)(&info->lock);

	/* Select the channel to be used and trigger conversion. */
	ret = regmap_write(info->regmap, AX_SARADC_MANUAL_CTRL_REG,
			   AX_SARADC_MANUAL_CTRL_ENABLE |
			   FIELD_PREP(AX_SARADC_MANUAL_CTRL_CH_SEL_MASK, chan->channel));
	if (ret)
		return ret;

	/* Hardware requires 13 conversion cycles at clk_rate. */
	usecs = DIV_ROUND_UP(AX_SARADC_CONV_CYCLES * USEC_PER_SEC, info->clk_rate);
	fsleep(usecs + AX_SARADC_CONV_DELAY_MARGIN_US);

	ret = regmap_read(info->regmap, AX_SARADC_DOUT_REG, &regval);

	/* Best effort to stop manual conversion. */
	regmap_write(info->regmap, AX_SARADC_MANUAL_CTRL_REG, 0);

	if (ret)
		return ret;

	*val = regval & GENMASK(AX_RESOLUTION_BITS - 1, 0);

	return 0;
}

static int axiado_saradc_read_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int *val, int *val2, long mask)
{
	struct axiado_saradc *info = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = axiado_saradc_conversion(info, chan, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_uV / (MICRO / MILLI);
		*val2 = AX_RESOLUTION_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static const struct iio_info axiado_saradc_iio_info = {
	.read_raw = axiado_saradc_read_raw,
};

struct axiado_saradc_soc_data {
	const char *name;
	unsigned int num_channels;
};

static const struct axiado_saradc_soc_data ax3000_saradc_data = {
	.name = "ax3000_saradc",
	.num_channels = 16,
};

static const struct axiado_saradc_soc_data ax3005_saradc_data = {
	.name = "ax3005_saradc",
	.num_channels = 8,
};

#define AX_SARADC_CH(_index)                                            \
	{                                                               \
		.type = IIO_VOLTAGE,                                    \
		.indexed = 1,                                           \
		.channel = (_index),                                    \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),           \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),   \
		.datasheet_name = "adc" #_index,                        \
	}

static const struct iio_chan_spec axiado_saradc_iio_channels[] = {
	AX_SARADC_CH(0),
	AX_SARADC_CH(1),
	AX_SARADC_CH(2),
	AX_SARADC_CH(3),
	AX_SARADC_CH(4),
	AX_SARADC_CH(5),
	AX_SARADC_CH(6),
	AX_SARADC_CH(7),
	AX_SARADC_CH(8),
	AX_SARADC_CH(9),
	AX_SARADC_CH(10),
	AX_SARADC_CH(11),
	AX_SARADC_CH(12),
	AX_SARADC_CH(13),
	AX_SARADC_CH(14),
	AX_SARADC_CH(15),
};

static void axiado_saradc_disable(void *map)
{
	regmap_write(map, AX_SARADC_GLOBAL_CTRL_REG, AX_SARADC_GLOBAL_CTRL_PD);
}

static int axiado_saradc_probe(struct platform_device *pdev)
{
	const struct axiado_saradc_soc_data *soc_data;
	struct device *dev = &pdev->dev;
	struct axiado_saradc *info;
	struct iio_dev *indio_dev;
	void __iomem *regs;
	struct regmap *map;
	struct clk *clk;
	u32 regval;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &axiado_saradc_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);
	info->regmap = map;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	info->clk_rate = clk_get_rate(clk);
	if (!info->clk_rate)
		return dev_err_probe(dev, -EINVAL, "invalid clock rate\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get vref voltage\n");

	info->vref_uV = ret;

	soc_data = device_get_match_data(dev);
	if (!soc_data)
		return dev_err_probe(dev, -ENODATA, "failed to get match data\n");

	ret = devm_mutex_init(dev, &info->lock);
	if (ret)
		return ret;

	regval = FIELD_PREP(AX_SARADC_GLOBAL_CTRL_CH_EN_MASK,
			    GENMASK(soc_data->num_channels - 1, 0)) |
		 FIELD_PREP(AX_SARADC_GLOBAL_CTRL_SAMPLE_MASK,
			    AX_SARADC_GLOBAL_CTRL_SAMPLE_16) |
		 FIELD_PREP(AX_SARADC_GLOBAL_CTRL_MODE_MASK,
			    AX_SARADC_GLOBAL_CTRL_MODE_MANUAL) |
		 AX_SARADC_GLOBAL_CTRL_ENABLE;

	ret = regmap_write(map, AX_SARADC_GLOBAL_CTRL_REG, regval);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, axiado_saradc_disable, map);
	if (ret)
		return ret;

	indio_dev->name = soc_data->name;
	indio_dev->info = &axiado_saradc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = axiado_saradc_iio_channels;
	indio_dev->num_channels = soc_data->num_channels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id axiado_saradc_match[] = {
	{
		.compatible = "axiado,ax3000-saradc",
		.data = &ax3000_saradc_data,
	},
	{
		.compatible = "axiado,ax3005-saradc",
		.data = &ax3005_saradc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, axiado_saradc_match);

static struct platform_driver axiado_saradc_driver = {
	.driver = {
		.name = "axiado-saradc",
		.of_match_table = axiado_saradc_match,
	},
	.probe = axiado_saradc_probe,
};
module_platform_driver(axiado_saradc_driver);

MODULE_AUTHOR("Axiado Corporation");
MODULE_DESCRIPTION("Axiado SARADC driver");
MODULE_LICENSE("GPL");
