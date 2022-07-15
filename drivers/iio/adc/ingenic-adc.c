// SPDX-License-Identifier: GPL-2.0
/*
 * ADC driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/mfd/jz4740-adc.c
 */

#include <dt-bindings/iio/adc/ingenic,adc.h>
#include <linux/clk.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define JZ_ADC_REG_ENABLE		0x00
#define JZ_ADC_REG_CFG			0x04
#define JZ_ADC_REG_CTRL			0x08
#define JZ_ADC_REG_STATUS		0x0c
#define JZ_ADC_REG_ADSAME		0x10
#define JZ_ADC_REG_ADWAIT		0x14
#define JZ_ADC_REG_ADTCH		0x18
#define JZ_ADC_REG_ADBDAT		0x1c
#define JZ_ADC_REG_ADSDAT		0x20
#define JZ_ADC_REG_ADCMD		0x24
#define JZ_ADC_REG_ADCLK		0x28

#define JZ_ADC_REG_ENABLE_PD		BIT(7)
#define JZ_ADC_REG_CFG_AUX_MD		(BIT(0) | BIT(1))
#define JZ_ADC_REG_CFG_BAT_MD		BIT(4)
#define JZ_ADC_REG_CFG_SAMPLE_NUM(n)	((n) << 10)
#define JZ_ADC_REG_CFG_PULL_UP(n)	((n) << 16)
#define JZ_ADC_REG_CFG_CMD_SEL		BIT(22)
#define JZ_ADC_REG_CFG_VBAT_SEL		BIT(30)
#define JZ_ADC_REG_CFG_TOUCH_OPS_MASK	(BIT(31) | GENMASK(23, 10))
#define JZ_ADC_REG_ADCLK_CLKDIV_LSB	0
#define JZ4725B_ADC_REG_ADCLK_CLKDIV10US_LSB	16
#define JZ4770_ADC_REG_ADCLK_CLKDIV10US_LSB	8
#define JZ4770_ADC_REG_ADCLK_CLKDIVMS_LSB	16

#define JZ_ADC_REG_ADCMD_YNADC		BIT(7)
#define JZ_ADC_REG_ADCMD_YPADC		BIT(8)
#define JZ_ADC_REG_ADCMD_XNADC		BIT(9)
#define JZ_ADC_REG_ADCMD_XPADC		BIT(10)
#define JZ_ADC_REG_ADCMD_VREFPYP	BIT(11)
#define JZ_ADC_REG_ADCMD_VREFPXP	BIT(12)
#define JZ_ADC_REG_ADCMD_VREFPXN	BIT(13)
#define JZ_ADC_REG_ADCMD_VREFPAUX	BIT(14)
#define JZ_ADC_REG_ADCMD_VREFPVDD33	BIT(15)
#define JZ_ADC_REG_ADCMD_VREFNYN	BIT(16)
#define JZ_ADC_REG_ADCMD_VREFNXP	BIT(17)
#define JZ_ADC_REG_ADCMD_VREFNXN	BIT(18)
#define JZ_ADC_REG_ADCMD_VREFAUX	BIT(19)
#define JZ_ADC_REG_ADCMD_YNGRU		BIT(20)
#define JZ_ADC_REG_ADCMD_XNGRU		BIT(21)
#define JZ_ADC_REG_ADCMD_XPGRU		BIT(22)
#define JZ_ADC_REG_ADCMD_YPSUP		BIT(23)
#define JZ_ADC_REG_ADCMD_XNSUP		BIT(24)
#define JZ_ADC_REG_ADCMD_XPSUP		BIT(25)

#define JZ_ADC_AUX_VREF				3300
#define JZ_ADC_AUX_VREF_BITS			12
#define JZ_ADC_BATTERY_LOW_VREF			2500
#define JZ_ADC_BATTERY_LOW_VREF_BITS		12
#define JZ4725B_ADC_BATTERY_HIGH_VREF		7500
#define JZ4725B_ADC_BATTERY_HIGH_VREF_BITS	10
#define JZ4740_ADC_BATTERY_HIGH_VREF		(7500 * 0.986)
#define JZ4740_ADC_BATTERY_HIGH_VREF_BITS	12
#define JZ4760_ADC_BATTERY_VREF			2500
#define JZ4770_ADC_BATTERY_VREF			1200
#define JZ4770_ADC_BATTERY_VREF_BITS		12

#define JZ_ADC_IRQ_AUX			BIT(0)
#define JZ_ADC_IRQ_BATTERY		BIT(1)
#define JZ_ADC_IRQ_TOUCH		BIT(2)
#define JZ_ADC_IRQ_PEN_DOWN		BIT(3)
#define JZ_ADC_IRQ_PEN_UP		BIT(4)
#define JZ_ADC_IRQ_PEN_DOWN_SLEEP	BIT(5)
#define JZ_ADC_IRQ_SLEEP		BIT(7)

struct ingenic_adc;

struct ingenic_adc_soc_data {
	unsigned int battery_high_vref;
	unsigned int battery_high_vref_bits;
	const int *battery_raw_avail;
	size_t battery_raw_avail_size;
	const int *battery_scale_avail;
	size_t battery_scale_avail_size;
	unsigned int battery_vref_mode: 1;
	unsigned int has_aux_md: 1;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	int (*init_clk_div)(struct device *dev, struct ingenic_adc *adc);
};

struct ingenic_adc {
	void __iomem *base;
	struct clk *clk;
	struct mutex lock;
	struct mutex aux_lock;
	const struct ingenic_adc_soc_data *soc_data;
	bool low_vref_mode;
};

static void ingenic_adc_set_adcmd(struct iio_dev *iio_dev, unsigned long mask)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);

	mutex_lock(&adc->lock);

	/* Init ADCMD */
	readl(adc->base + JZ_ADC_REG_ADCMD);

	if (mask & 0x3) {
		/* Second channel (INGENIC_ADC_TOUCH_YP): sample YP vs. GND */
		writel(JZ_ADC_REG_ADCMD_XNGRU
		       | JZ_ADC_REG_ADCMD_VREFNXN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_YPADC,
		       adc->base + JZ_ADC_REG_ADCMD);

		/* First channel (INGENIC_ADC_TOUCH_XP): sample XP vs. GND */
		writel(JZ_ADC_REG_ADCMD_YNGRU
		       | JZ_ADC_REG_ADCMD_VREFNYN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_XPADC,
		       adc->base + JZ_ADC_REG_ADCMD);
	}

	if (mask & 0xc) {
		/* Fourth channel (INGENIC_ADC_TOUCH_YN): sample YN vs. GND */
		writel(JZ_ADC_REG_ADCMD_XNGRU
		       | JZ_ADC_REG_ADCMD_VREFNXN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_YNADC,
		       adc->base + JZ_ADC_REG_ADCMD);

		/* Third channel (INGENIC_ADC_TOUCH_XN): sample XN vs. GND */
		writel(JZ_ADC_REG_ADCMD_YNGRU
		       | JZ_ADC_REG_ADCMD_VREFNYN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_XNADC,
		       adc->base + JZ_ADC_REG_ADCMD);
	}

	if (mask & 0x30) {
		/* Sixth channel (INGENIC_ADC_TOUCH_YD): sample YP vs. YN */
		writel(JZ_ADC_REG_ADCMD_VREFNYN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_YPADC,
		       adc->base + JZ_ADC_REG_ADCMD);

		/* Fifth channel (INGENIC_ADC_TOUCH_XD): sample XP vs. XN */
		writel(JZ_ADC_REG_ADCMD_VREFNXN | JZ_ADC_REG_ADCMD_VREFPVDD33
		       | JZ_ADC_REG_ADCMD_XPADC,
		       adc->base + JZ_ADC_REG_ADCMD);
	}

	/* We're done */
	writel(0, adc->base + JZ_ADC_REG_ADCMD);

	mutex_unlock(&adc->lock);
}

static void ingenic_adc_set_config(struct ingenic_adc *adc,
				   uint32_t mask,
				   uint32_t val)
{
	uint32_t cfg;

	mutex_lock(&adc->lock);

	cfg = readl(adc->base + JZ_ADC_REG_CFG) & ~mask;
	cfg |= val;
	writel(cfg, adc->base + JZ_ADC_REG_CFG);

	mutex_unlock(&adc->lock);
}

static void ingenic_adc_enable_unlocked(struct ingenic_adc *adc,
					int engine,
					bool enabled)
{
	u8 val;

	val = readb(adc->base + JZ_ADC_REG_ENABLE);

	if (enabled)
		val |= BIT(engine);
	else
		val &= ~BIT(engine);

	writeb(val, adc->base + JZ_ADC_REG_ENABLE);
}

static void ingenic_adc_enable(struct ingenic_adc *adc,
			       int engine,
			       bool enabled)
{
	mutex_lock(&adc->lock);
	ingenic_adc_enable_unlocked(adc, engine, enabled);
	mutex_unlock(&adc->lock);
}

static int ingenic_adc_capture(struct ingenic_adc *adc,
			       int engine)
{
	u32 cfg;
	u8 val;
	int ret;

	/*
	 * Disable CMD_SEL temporarily, because it causes wrong VBAT readings,
	 * probably due to the switch of VREF. We must keep the lock here to
	 * avoid races with the buffer enable/disable functions.
	 */
	mutex_lock(&adc->lock);
	cfg = readl(adc->base + JZ_ADC_REG_CFG);
	writel(cfg & ~JZ_ADC_REG_CFG_CMD_SEL, adc->base + JZ_ADC_REG_CFG);

	ingenic_adc_enable_unlocked(adc, engine, true);
	ret = readb_poll_timeout(adc->base + JZ_ADC_REG_ENABLE, val,
				 !(val & BIT(engine)), 250, 1000);
	if (ret)
		ingenic_adc_enable_unlocked(adc, engine, false);

	writel(cfg, adc->base + JZ_ADC_REG_CFG);
	mutex_unlock(&adc->lock);

	return ret;
}

static int ingenic_adc_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 int val,
				 int val2,
				 long m)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);
	struct device *dev = iio_dev->dev.parent;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel) {
		case INGENIC_ADC_BATTERY:
			if (!adc->soc_data->battery_vref_mode)
				return -EINVAL;

			ret = clk_enable(adc->clk);
			if (ret) {
				dev_err(dev, "Failed to enable clock: %d\n",
					ret);
				return ret;
			}

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

			clk_disable(adc->clk);

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

static const int jz4760_adc_battery_scale_avail[] = {
	JZ4760_ADC_BATTERY_VREF, JZ4770_ADC_BATTERY_VREF_BITS,
};

static const int jz4770_adc_battery_raw_avail[] = {
	0, 1, (1 << JZ4770_ADC_BATTERY_VREF_BITS) - 1,
};

static const int jz4770_adc_battery_scale_avail[] = {
	JZ4770_ADC_BATTERY_VREF, JZ4770_ADC_BATTERY_VREF_BITS,
};

static int jz4725b_adc_init_clk_div(struct device *dev, struct ingenic_adc *adc)
{
	struct clk *parent_clk;
	unsigned long parent_rate, rate;
	unsigned int div_main, div_10us;

	parent_clk = clk_get_parent(adc->clk);
	if (!parent_clk) {
		dev_err(dev, "ADC clock has no parent\n");
		return -ENODEV;
	}
	parent_rate = clk_get_rate(parent_clk);

	/*
	 * The JZ4725B ADC works at 500 kHz to 8 MHz.
	 * We pick the highest rate possible.
	 * In practice we typically get 6 MHz, half of the 12 MHz EXT clock.
	 */
	div_main = DIV_ROUND_UP(parent_rate, 8000000);
	div_main = clamp(div_main, 1u, 64u);
	rate = parent_rate / div_main;
	if (rate < 500000 || rate > 8000000) {
		dev_err(dev, "No valid divider for ADC main clock\n");
		return -EINVAL;
	}

	/* We also need a divider that produces a 10us clock. */
	div_10us = DIV_ROUND_UP(rate, 100000);

	writel(((div_10us - 1) << JZ4725B_ADC_REG_ADCLK_CLKDIV10US_LSB) |
	       (div_main - 1) << JZ_ADC_REG_ADCLK_CLKDIV_LSB,
	       adc->base + JZ_ADC_REG_ADCLK);

	return 0;
}

static int jz4770_adc_init_clk_div(struct device *dev, struct ingenic_adc *adc)
{
	struct clk *parent_clk;
	unsigned long parent_rate, rate;
	unsigned int div_main, div_ms, div_10us;

	parent_clk = clk_get_parent(adc->clk);
	if (!parent_clk) {
		dev_err(dev, "ADC clock has no parent\n");
		return -ENODEV;
	}
	parent_rate = clk_get_rate(parent_clk);

	/*
	 * The JZ4770 ADC works at 20 kHz to 200 kHz.
	 * We pick the highest rate possible.
	 */
	div_main = DIV_ROUND_UP(parent_rate, 200000);
	div_main = clamp(div_main, 1u, 256u);
	rate = parent_rate / div_main;
	if (rate < 20000 || rate > 200000) {
		dev_err(dev, "No valid divider for ADC main clock\n");
		return -EINVAL;
	}

	/* We also need a divider that produces a 10us clock. */
	div_10us = DIV_ROUND_UP(rate, 10000);
	/* And another, which produces a 1ms clock. */
	div_ms = DIV_ROUND_UP(rate, 1000);

	writel(((div_ms - 1) << JZ4770_ADC_REG_ADCLK_CLKDIVMS_LSB) |
	       ((div_10us - 1) << JZ4770_ADC_REG_ADCLK_CLKDIV10US_LSB) |
	       (div_main - 1) << JZ_ADC_REG_ADCLK_CLKDIV_LSB,
	       adc->base + JZ_ADC_REG_ADCLK);

	return 0;
}

static const struct iio_chan_spec jz4740_channels[] = {
	{
		.extend_name = "aux",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX,
		.scan_index = -1,
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
		.scan_index = -1,
	},
};

static const struct iio_chan_spec jz4760_channels[] = {
	{
		.extend_name = "aux",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX0,
		.scan_index = -1,
	},
	{
		.extend_name = "aux1",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX,
		.scan_index = -1,
	},
	{
		.extend_name = "aux2",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX2,
		.scan_index = -1,
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
		.scan_index = -1,
	},
};

static const struct iio_chan_spec jz4770_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_XP,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_YP,
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_XN,
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_YN,
		.scan_index = 3,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_XD,
		.scan_index = 4,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = INGENIC_ADC_TOUCH_YD,
		.scan_index = 5,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
	},
	{
		.extend_name = "aux",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX,
		.scan_index = -1,
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
		.scan_index = -1,
	},
	{
		.extend_name = "aux2",
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = INGENIC_ADC_AUX2,
		.scan_index = -1,
	},
};

static const struct ingenic_adc_soc_data jz4725b_adc_soc_data = {
	.battery_high_vref = JZ4725B_ADC_BATTERY_HIGH_VREF,
	.battery_high_vref_bits = JZ4725B_ADC_BATTERY_HIGH_VREF_BITS,
	.battery_raw_avail = jz4725b_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4725b_adc_battery_raw_avail),
	.battery_scale_avail = jz4725b_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4725b_adc_battery_scale_avail),
	.battery_vref_mode = true,
	.has_aux_md = false,
	.channels = jz4740_channels,
	.num_channels = ARRAY_SIZE(jz4740_channels),
	.init_clk_div = jz4725b_adc_init_clk_div,
};

static const struct ingenic_adc_soc_data jz4740_adc_soc_data = {
	.battery_high_vref = JZ4740_ADC_BATTERY_HIGH_VREF,
	.battery_high_vref_bits = JZ4740_ADC_BATTERY_HIGH_VREF_BITS,
	.battery_raw_avail = jz4740_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4740_adc_battery_raw_avail),
	.battery_scale_avail = jz4740_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4740_adc_battery_scale_avail),
	.battery_vref_mode = true,
	.has_aux_md = false,
	.channels = jz4740_channels,
	.num_channels = ARRAY_SIZE(jz4740_channels),
	.init_clk_div = NULL, /* no ADCLK register on JZ4740 */
};

static const struct ingenic_adc_soc_data jz4760_adc_soc_data = {
	.battery_high_vref = JZ4760_ADC_BATTERY_VREF,
	.battery_high_vref_bits = JZ4770_ADC_BATTERY_VREF_BITS,
	.battery_raw_avail = jz4770_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4770_adc_battery_raw_avail),
	.battery_scale_avail = jz4760_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4760_adc_battery_scale_avail),
	.battery_vref_mode = false,
	.has_aux_md = true,
	.channels = jz4760_channels,
	.num_channels = ARRAY_SIZE(jz4760_channels),
	.init_clk_div = jz4770_adc_init_clk_div,
};

static const struct ingenic_adc_soc_data jz4770_adc_soc_data = {
	.battery_high_vref = JZ4770_ADC_BATTERY_VREF,
	.battery_high_vref_bits = JZ4770_ADC_BATTERY_VREF_BITS,
	.battery_raw_avail = jz4770_adc_battery_raw_avail,
	.battery_raw_avail_size = ARRAY_SIZE(jz4770_adc_battery_raw_avail),
	.battery_scale_avail = jz4770_adc_battery_scale_avail,
	.battery_scale_avail_size = ARRAY_SIZE(jz4770_adc_battery_scale_avail),
	.battery_vref_mode = false,
	.has_aux_md = true,
	.channels = jz4770_channels,
	.num_channels = ARRAY_SIZE(jz4770_channels),
	.init_clk_div = jz4770_adc_init_clk_div,
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
	}
}

static int ingenic_adc_read_chan_info_raw(struct iio_dev *iio_dev,
					  struct iio_chan_spec const *chan,
					  int *val)
{
	int cmd, ret, engine = (chan->channel == INGENIC_ADC_BATTERY);
	struct ingenic_adc *adc = iio_priv(iio_dev);

	ret = clk_enable(adc->clk);
	if (ret) {
		dev_err(iio_dev->dev.parent, "Failed to enable clock: %d\n",
			ret);
		return ret;
	}

	/* We cannot sample the aux channels in parallel. */
	mutex_lock(&adc->aux_lock);
	if (adc->soc_data->has_aux_md && engine == 0) {
		switch (chan->channel) {
		case INGENIC_ADC_AUX0:
			cmd = 0;
			break;
		case INGENIC_ADC_AUX:
			cmd = 1;
			break;
		case INGENIC_ADC_AUX2:
			cmd = 2;
			break;
		}

		ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_AUX_MD, cmd);
	}

	ret = ingenic_adc_capture(adc, engine);
	if (ret)
		goto out;

	switch (chan->channel) {
	case INGENIC_ADC_AUX0:
	case INGENIC_ADC_AUX:
	case INGENIC_ADC_AUX2:
		*val = readw(adc->base + JZ_ADC_REG_ADSDAT);
		break;
	case INGENIC_ADC_BATTERY:
		*val = readw(adc->base + JZ_ADC_REG_ADBDAT);
		break;
	}

	ret = IIO_VAL_INT;
out:
	mutex_unlock(&adc->aux_lock);
	clk_disable(adc->clk);

	return ret;
}

static int ingenic_adc_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long m)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		return ingenic_adc_read_chan_info_raw(iio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel) {
		case INGENIC_ADC_AUX0:
		case INGENIC_ADC_AUX:
		case INGENIC_ADC_AUX2:
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

static int ingenic_adc_fwnode_xlate(struct iio_dev *iio_dev,
				    const struct fwnode_reference_args *iiospec)
{
	int i;

	if (!iiospec->nargs)
		return -EINVAL;

	for (i = 0; i < iio_dev->num_channels; ++i)
		if (iio_dev->channels[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static void ingenic_adc_clk_cleanup(void *data)
{
	clk_unprepare(data);
}

static const struct iio_info ingenic_adc_info = {
	.write_raw = ingenic_adc_write_raw,
	.read_raw = ingenic_adc_read_raw,
	.read_avail = ingenic_adc_read_avail,
	.fwnode_xlate = ingenic_adc_fwnode_xlate,
};

static int ingenic_adc_buffer_enable(struct iio_dev *iio_dev)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);
	int ret;

	ret = clk_enable(adc->clk);
	if (ret) {
		dev_err(iio_dev->dev.parent, "Failed to enable clock: %d\n",
			ret);
		return ret;
	}

	/* It takes significant time for the touchscreen hw to stabilize. */
	msleep(50);
	ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_TOUCH_OPS_MASK,
			       JZ_ADC_REG_CFG_SAMPLE_NUM(4) |
			       JZ_ADC_REG_CFG_PULL_UP(4));

	writew(80, adc->base + JZ_ADC_REG_ADWAIT);
	writew(2, adc->base + JZ_ADC_REG_ADSAME);
	writeb((u8)~JZ_ADC_IRQ_TOUCH, adc->base + JZ_ADC_REG_CTRL);
	writel(0, adc->base + JZ_ADC_REG_ADTCH);

	ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_CMD_SEL,
			       JZ_ADC_REG_CFG_CMD_SEL);
	ingenic_adc_set_adcmd(iio_dev, iio_dev->active_scan_mask[0]);

	ingenic_adc_enable(adc, 2, true);

	return 0;
}

static int ingenic_adc_buffer_disable(struct iio_dev *iio_dev)
{
	struct ingenic_adc *adc = iio_priv(iio_dev);

	ingenic_adc_enable(adc, 2, false);

	ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_CMD_SEL, 0);

	writeb(0xff, adc->base + JZ_ADC_REG_CTRL);
	writeb(0xff, adc->base + JZ_ADC_REG_STATUS);
	ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_TOUCH_OPS_MASK, 0);
	writew(0, adc->base + JZ_ADC_REG_ADSAME);
	writew(0, adc->base + JZ_ADC_REG_ADWAIT);
	clk_disable(adc->clk);

	return 0;
}

static const struct iio_buffer_setup_ops ingenic_buffer_setup_ops = {
	.postenable = &ingenic_adc_buffer_enable,
	.predisable = &ingenic_adc_buffer_disable
};

static irqreturn_t ingenic_adc_irq(int irq, void *data)
{
	struct iio_dev *iio_dev = data;
	struct ingenic_adc *adc = iio_priv(iio_dev);
	unsigned long mask = iio_dev->active_scan_mask[0];
	unsigned int i;
	u32 tdat[3];

	for (i = 0; i < ARRAY_SIZE(tdat); mask >>= 2, i++) {
		if (mask & 0x3)
			tdat[i] = readl(adc->base + JZ_ADC_REG_ADTCH);
		else
			tdat[i] = 0;
	}

	iio_push_to_buffers(iio_dev, tdat);
	writeb(JZ_ADC_IRQ_TOUCH, adc->base + JZ_ADC_REG_STATUS);

	return IRQ_HANDLED;
}

static int ingenic_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *iio_dev;
	struct ingenic_adc *adc;
	const struct ingenic_adc_soc_data *soc_data;
	int irq, ret;

	soc_data = device_get_match_data(dev);
	if (!soc_data)
		return -EINVAL;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!iio_dev)
		return -ENOMEM;

	adc = iio_priv(iio_dev);
	mutex_init(&adc->lock);
	mutex_init(&adc->aux_lock);
	adc->soc_data = soc_data;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, ingenic_adc_irq, 0,
			       dev_name(dev), iio_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq: %d\n", ret);
		return ret;
	}

	adc->base = devm_platform_ioremap_resource(pdev, 0);
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

	/* Set clock dividers. */
	if (soc_data->init_clk_div) {
		ret = soc_data->init_clk_div(dev, adc);
		if (ret) {
			clk_disable_unprepare(adc->clk);
			return ret;
		}
	}

	/* Put hardware in a known passive state. */
	writeb(0x00, adc->base + JZ_ADC_REG_ENABLE);
	writeb(0xff, adc->base + JZ_ADC_REG_CTRL);

	/* JZ4760B specific */
	if (device_property_present(dev, "ingenic,use-internal-divider"))
		ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_VBAT_SEL,
					    JZ_ADC_REG_CFG_VBAT_SEL);
	else
		ingenic_adc_set_config(adc, JZ_ADC_REG_CFG_VBAT_SEL, 0);

	usleep_range(2000, 3000); /* Must wait at least 2ms. */
	clk_disable(adc->clk);

	ret = devm_add_action_or_reset(dev, ingenic_adc_clk_cleanup, adc->clk);
	if (ret) {
		dev_err(dev, "Unable to add action\n");
		return ret;
	}

	iio_dev->name = "jz-adc";
	iio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;
	iio_dev->setup_ops = &ingenic_buffer_setup_ops;
	iio_dev->channels = soc_data->channels;
	iio_dev->num_channels = soc_data->num_channels;
	iio_dev->info = &ingenic_adc_info;

	ret = devm_iio_device_register(dev, iio_dev);
	if (ret)
		dev_err(dev, "Unable to register IIO device\n");

	return ret;
}

static const struct of_device_id ingenic_adc_of_match[] = {
	{ .compatible = "ingenic,jz4725b-adc", .data = &jz4725b_adc_soc_data, },
	{ .compatible = "ingenic,jz4740-adc", .data = &jz4740_adc_soc_data, },
	{ .compatible = "ingenic,jz4760-adc", .data = &jz4760_adc_soc_data, },
	{ .compatible = "ingenic,jz4760b-adc", .data = &jz4760_adc_soc_data, },
	{ .compatible = "ingenic,jz4770-adc", .data = &jz4770_adc_soc_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, ingenic_adc_of_match);

static struct platform_driver ingenic_adc_driver = {
	.driver = {
		.name = "ingenic-adc",
		.of_match_table = ingenic_adc_of_match,
	},
	.probe = ingenic_adc_probe,
};
module_platform_driver(ingenic_adc_driver);
MODULE_LICENSE("GPL v2");
