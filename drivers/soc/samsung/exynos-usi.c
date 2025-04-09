// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Linaro Ltd.
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 *
 * Samsung Exynos USI driver (Universal Serial Interface).
 */

#include <linux/array_size.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/soc/samsung,exynos-usi.h>

/* USIv1: System Register: SW_CONF register bits */
#define USI_V1_SW_CONF_NONE		0x0
#define USI_V1_SW_CONF_I2C0		0x1
#define USI_V1_SW_CONF_I2C1		0x2
#define USI_V1_SW_CONF_I2C0_1		0x3
#define USI_V1_SW_CONF_SPI		0x4
#define USI_V1_SW_CONF_UART		0x8
#define USI_V1_SW_CONF_UART_I2C1	0xa
#define USI_V1_SW_CONF_MASK		(USI_V1_SW_CONF_I2C0 | USI_V1_SW_CONF_I2C1 | \
					 USI_V1_SW_CONF_I2C0_1 | USI_V1_SW_CONF_SPI | \
					 USI_V1_SW_CONF_UART | USI_V1_SW_CONF_UART_I2C1)

/* USIv2: System Register: SW_CONF register bits */
#define USI_V2_SW_CONF_NONE	0x0
#define USI_V2_SW_CONF_UART	BIT(0)
#define USI_V2_SW_CONF_SPI	BIT(1)
#define USI_V2_SW_CONF_I2C	BIT(2)
#define USI_V2_SW_CONF_MASK	(USI_V2_SW_CONF_UART | USI_V2_SW_CONF_SPI | \
				 USI_V2_SW_CONF_I2C)

/* USIv2: USI register offsets */
#define USI_CON			0x04
#define USI_OPTION		0x08

/* USIv2: USI register bits */
#define USI_CON_RESET		BIT(0)
#define USI_OPTION_CLKREQ_ON	BIT(1)
#define USI_OPTION_CLKSTOP_ON	BIT(2)

enum exynos_usi_ver {
	USI_VER1 = 0,
	USI_VER2,
};

struct exynos_usi_variant {
	enum exynos_usi_ver ver;	/* USI IP-core version */
	unsigned int sw_conf_mask;	/* SW_CONF mask for all protocols */
	size_t min_mode;		/* first index in exynos_usi_modes[] */
	size_t max_mode;		/* last index in exynos_usi_modes[] */
	size_t num_clks;		/* number of clocks to assert */
	const char * const *clk_names;	/* clock names to assert */
};

struct exynos_usi {
	struct device *dev;
	void __iomem *regs;		/* USI register map */
	struct clk_bulk_data *clks;	/* USI clocks */

	size_t mode;			/* current USI SW_CONF mode index */
	bool clkreq_on;			/* always provide clock to IP */

	/* System Register */
	struct regmap *sysreg;		/* System Register map */
	unsigned int sw_conf;		/* SW_CONF register offset in sysreg */

	const struct exynos_usi_variant *data;
};

struct exynos_usi_mode {
	const char *name;		/* mode name */
	unsigned int val;		/* mode register value */
};

#define USI_MODES_MAX (USI_MODE_UART_I2C1 + 1)
static const struct exynos_usi_mode exynos_usi_modes[][USI_MODES_MAX] = {
	[USI_VER1] = {
		[USI_MODE_NONE] =	{ .name = "none", .val = USI_V1_SW_CONF_NONE },
		[USI_MODE_UART] =	{ .name = "uart", .val = USI_V1_SW_CONF_UART },
		[USI_MODE_SPI] =	{ .name = "spi",  .val = USI_V1_SW_CONF_SPI },
		[USI_MODE_I2C] =	{ .name = "i2c",  .val = USI_V1_SW_CONF_I2C0 },
		[USI_MODE_I2C1] =	{ .name = "i2c1", .val = USI_V1_SW_CONF_I2C1 },
		[USI_MODE_I2C0_1] =	{ .name = "i2c0_1", .val = USI_V1_SW_CONF_I2C0_1 },
		[USI_MODE_UART_I2C1] =	{ .name = "uart_i2c1", .val = USI_V1_SW_CONF_UART_I2C1 },
	}, [USI_VER2] = {
		[USI_MODE_NONE] =	{ .name = "none", .val = USI_V2_SW_CONF_NONE },
		[USI_MODE_UART] =	{ .name = "uart", .val = USI_V2_SW_CONF_UART },
		[USI_MODE_SPI] =	{ .name = "spi",  .val = USI_V2_SW_CONF_SPI },
		[USI_MODE_I2C] =	{ .name = "i2c",  .val = USI_V2_SW_CONF_I2C },
	},
};

static const char * const exynos850_usi_clk_names[] = { "pclk", "ipclk" };
static const struct exynos_usi_variant exynos850_usi_data = {
	.ver		= USI_VER2,
	.sw_conf_mask	= USI_V2_SW_CONF_MASK,
	.min_mode	= USI_MODE_NONE,
	.max_mode	= USI_MODE_I2C,
	.num_clks	= ARRAY_SIZE(exynos850_usi_clk_names),
	.clk_names	= exynos850_usi_clk_names,
};

static const struct exynos_usi_variant exynos8895_usi_data = {
	.ver		= USI_VER1,
	.sw_conf_mask	= USI_V1_SW_CONF_MASK,
	.min_mode	= USI_MODE_NONE,
	.max_mode	= USI_MODE_UART_I2C1,
	.num_clks	= ARRAY_SIZE(exynos850_usi_clk_names),
	.clk_names	= exynos850_usi_clk_names,
};

static const struct of_device_id exynos_usi_dt_match[] = {
	{
		.compatible = "samsung,exynos850-usi",
		.data = &exynos850_usi_data,
	}, {
		.compatible = "samsung,exynos8895-usi",
		.data = &exynos8895_usi_data,
	},
	{ } /* sentinel */
};
MODULE_DEVICE_TABLE(of, exynos_usi_dt_match);

/**
 * exynos_usi_set_sw_conf - Set USI block configuration mode
 * @usi: USI driver object
 * @mode: Mode index
 *
 * Select underlying serial protocol (UART/SPI/I2C) in USI IP-core.
 *
 * Return: 0 on success, or negative error code on failure.
 */
static int exynos_usi_set_sw_conf(struct exynos_usi *usi, size_t mode)
{
	unsigned int val;
	int ret;

	if (mode < usi->data->min_mode || mode > usi->data->max_mode)
		return -EINVAL;

	val = exynos_usi_modes[usi->data->ver][mode].val;
	ret = regmap_update_bits(usi->sysreg, usi->sw_conf,
				 usi->data->sw_conf_mask, val);
	if (ret)
		return ret;

	usi->mode = mode;
	dev_dbg(usi->dev, "protocol: %s\n",
		exynos_usi_modes[usi->data->ver][usi->mode].name);

	return 0;
}

/**
 * exynos_usi_enable - Initialize USI block
 * @usi: USI driver object
 *
 * USI IP-core start state is "reset" (on startup and after CPU resume). This
 * routine enables the USI block by clearing the reset flag. It also configures
 * HWACG behavior (needed e.g. for UART Rx). It should be performed before
 * underlying protocol becomes functional.
 *
 * Return: 0 on success, or negative error code on failure.
 */
static int exynos_usi_enable(const struct exynos_usi *usi)
{
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(usi->data->num_clks, usi->clks);
	if (ret)
		return ret;

	/* Enable USI block */
	val = readl(usi->regs + USI_CON);
	val &= ~USI_CON_RESET;
	writel(val, usi->regs + USI_CON);
	udelay(1);

	/* Continuously provide the clock to USI IP w/o gating */
	if (usi->clkreq_on) {
		val = readl(usi->regs + USI_OPTION);
		val &= ~USI_OPTION_CLKSTOP_ON;
		val |= USI_OPTION_CLKREQ_ON;
		writel(val, usi->regs + USI_OPTION);
	}

	clk_bulk_disable_unprepare(usi->data->num_clks, usi->clks);

	return ret;
}

static int exynos_usi_configure(struct exynos_usi *usi)
{
	int ret;

	ret = exynos_usi_set_sw_conf(usi, usi->mode);
	if (ret)
		return ret;

	if (usi->data->ver == USI_VER1)
		ret = clk_bulk_prepare_enable(usi->data->num_clks,
					      usi->clks);
	else if (usi->data->ver == USI_VER2)
		ret = exynos_usi_enable(usi);

	return ret;
}

static void exynos_usi_unconfigure(void *data)
{
	struct exynos_usi *usi = data;
	u32 val;
	int ret;

	if (usi->data->ver == USI_VER1) {
		clk_bulk_disable_unprepare(usi->data->num_clks, usi->clks);
		return;
	}

	ret = clk_bulk_prepare_enable(usi->data->num_clks, usi->clks);
	if (ret)
		return;

	/* Make sure that we've stopped providing the clock to USI IP */
	val = readl(usi->regs + USI_OPTION);
	val &= ~USI_OPTION_CLKREQ_ON;
	val |= ~USI_OPTION_CLKSTOP_ON;
	writel(val, usi->regs + USI_OPTION);

	/* Set USI block state to reset */
	val = readl(usi->regs + USI_CON);
	val |= USI_CON_RESET;
	writel(val, usi->regs + USI_CON);

	clk_bulk_disable_unprepare(usi->data->num_clks, usi->clks);
}

static int exynos_usi_parse_dt(struct device_node *np, struct exynos_usi *usi)
{
	int ret;
	u32 mode;

	ret = of_property_read_u32(np, "samsung,mode", &mode);
	if (ret)
		return ret;
	if (mode < usi->data->min_mode || mode > usi->data->max_mode)
		return -EINVAL;
	usi->mode = mode;

	usi->sysreg = syscon_regmap_lookup_by_phandle_args(np, "samsung,sysreg",
							   1, &usi->sw_conf);
	if (IS_ERR(usi->sysreg))
		return PTR_ERR(usi->sysreg);

	usi->clkreq_on = of_property_read_bool(np, "samsung,clkreq-on");

	return 0;
}

static int exynos_usi_get_clocks(struct exynos_usi *usi)
{
	const size_t num = usi->data->num_clks;
	struct device *dev = usi->dev;
	size_t i;

	if (num == 0)
		return 0;

	usi->clks = devm_kcalloc(dev, num, sizeof(*usi->clks), GFP_KERNEL);
	if (!usi->clks)
		return -ENOMEM;

	for (i = 0; i < num; ++i)
		usi->clks[i].id = usi->data->clk_names[i];

	return devm_clk_bulk_get(dev, num, usi->clks);
}

static int exynos_usi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct exynos_usi *usi;
	int ret;

	usi = devm_kzalloc(dev, sizeof(*usi), GFP_KERNEL);
	if (!usi)
		return -ENOMEM;

	usi->dev = dev;
	platform_set_drvdata(pdev, usi);

	usi->data = of_device_get_match_data(dev);
	if (!usi->data)
		return -EINVAL;

	ret = exynos_usi_parse_dt(np, usi);
	if (ret)
		return ret;

	ret = exynos_usi_get_clocks(usi);
	if (ret)
		return ret;

	if (usi->data->ver == USI_VER2) {
		usi->regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(usi->regs))
			return PTR_ERR(usi->regs);
	}

	ret = exynos_usi_configure(usi);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev, exynos_usi_unconfigure, usi);
	if (ret)
		return ret;

	/* Make it possible to embed protocol nodes into USI np */
	return of_platform_populate(np, NULL, NULL, dev);
}

static int __maybe_unused exynos_usi_resume_noirq(struct device *dev)
{
	struct exynos_usi *usi = dev_get_drvdata(dev);

	return exynos_usi_configure(usi);
}

static const struct dev_pm_ops exynos_usi_pm = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL, exynos_usi_resume_noirq)
};

static struct platform_driver exynos_usi_driver = {
	.driver = {
		.name		= "exynos-usi",
		.pm		= &exynos_usi_pm,
		.of_match_table	= exynos_usi_dt_match,
	},
	.probe = exynos_usi_probe,
};
module_platform_driver(exynos_usi_driver);

MODULE_DESCRIPTION("Samsung USI driver");
MODULE_AUTHOR("Sam Protsenko <semen.protsenko@linaro.org>");
MODULE_LICENSE("GPL");
