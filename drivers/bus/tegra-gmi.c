// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NVIDIA Generic Memory Interface
 *
 * Copyright (C) 2016 Host Mobility AB. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <soc/tegra/common.h>

#define TEGRA_GMI_CONFIG		0x00
#define TEGRA_GMI_CONFIG_GO		BIT(31)
#define TEGRA_GMI_BUS_WIDTH_32BIT	BIT(30)
#define TEGRA_GMI_MUX_MODE		BIT(28)
#define TEGRA_GMI_RDY_BEFORE_DATA	BIT(24)
#define TEGRA_GMI_RDY_ACTIVE_HIGH	BIT(23)
#define TEGRA_GMI_ADV_ACTIVE_HIGH	BIT(22)
#define TEGRA_GMI_OE_ACTIVE_HIGH	BIT(21)
#define TEGRA_GMI_CS_ACTIVE_HIGH	BIT(20)
#define TEGRA_GMI_CS_SELECT(x)		((x & 0x7) << 4)

#define TEGRA_GMI_TIMING0		0x10
#define TEGRA_GMI_MUXED_WIDTH(x)	((x & 0xf) << 12)
#define TEGRA_GMI_HOLD_WIDTH(x)		((x & 0xf) << 8)
#define TEGRA_GMI_ADV_WIDTH(x)		((x & 0xf) << 4)
#define TEGRA_GMI_CE_WIDTH(x)		(x & 0xf)

#define TEGRA_GMI_TIMING1		0x14
#define TEGRA_GMI_WE_WIDTH(x)		((x & 0xff) << 16)
#define TEGRA_GMI_OE_WIDTH(x)		((x & 0xff) << 8)
#define TEGRA_GMI_WAIT_WIDTH(x)		(x & 0xff)

#define TEGRA_GMI_MAX_CHIP_SELECT	8

struct tegra_gmi {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;

	u32 snor_config;
	u32 snor_timing0;
	u32 snor_timing1;
};

static int tegra_gmi_enable(struct tegra_gmi *gmi)
{
	int err;

	pm_runtime_enable(gmi->dev);
	err = pm_runtime_resume_and_get(gmi->dev);
	if (err) {
		pm_runtime_disable(gmi->dev);
		return err;
	}

	reset_control_assert(gmi->rst);
	usleep_range(2000, 4000);
	reset_control_deassert(gmi->rst);

	writel(gmi->snor_timing0, gmi->base + TEGRA_GMI_TIMING0);
	writel(gmi->snor_timing1, gmi->base + TEGRA_GMI_TIMING1);

	gmi->snor_config |= TEGRA_GMI_CONFIG_GO;
	writel(gmi->snor_config, gmi->base + TEGRA_GMI_CONFIG);

	return 0;
}

static void tegra_gmi_disable(struct tegra_gmi *gmi)
{
	u32 config;

	/* stop GMI operation */
	config = readl(gmi->base + TEGRA_GMI_CONFIG);
	config &= ~TEGRA_GMI_CONFIG_GO;
	writel(config, gmi->base + TEGRA_GMI_CONFIG);

	reset_control_assert(gmi->rst);

	pm_runtime_put_sync_suspend(gmi->dev);
	pm_runtime_force_suspend(gmi->dev);
}

static int tegra_gmi_parse_dt(struct tegra_gmi *gmi)
{
	struct device_node *child;
	u32 property, ranges[4];
	int err;

	child = of_get_next_available_child(gmi->dev->of_node, NULL);
	if (!child) {
		dev_err(gmi->dev, "no child nodes found\n");
		return -ENODEV;
	}

	/*
	 * We currently only support one child device due to lack of
	 * chip-select address decoding. Which means that we only have one
	 * chip-select line from the GMI controller.
	 */
	if (of_get_child_count(gmi->dev->of_node) > 1)
		dev_warn(gmi->dev, "only one child device is supported.");

	if (of_property_read_bool(child, "nvidia,snor-data-width-32bit"))
		gmi->snor_config |= TEGRA_GMI_BUS_WIDTH_32BIT;

	if (of_property_read_bool(child, "nvidia,snor-mux-mode"))
		gmi->snor_config |= TEGRA_GMI_MUX_MODE;

	if (of_property_read_bool(child, "nvidia,snor-rdy-active-before-data"))
		gmi->snor_config |= TEGRA_GMI_RDY_BEFORE_DATA;

	if (of_property_read_bool(child, "nvidia,snor-rdy-active-high"))
		gmi->snor_config |= TEGRA_GMI_RDY_ACTIVE_HIGH;

	if (of_property_read_bool(child, "nvidia,snor-adv-active-high"))
		gmi->snor_config |= TEGRA_GMI_ADV_ACTIVE_HIGH;

	if (of_property_read_bool(child, "nvidia,snor-oe-active-high"))
		gmi->snor_config |= TEGRA_GMI_OE_ACTIVE_HIGH;

	if (of_property_read_bool(child, "nvidia,snor-cs-active-high"))
		gmi->snor_config |= TEGRA_GMI_CS_ACTIVE_HIGH;

	/* Decode the CS# */
	err = of_property_read_u32_array(child, "ranges", ranges, 4);
	if (err < 0) {
		/* Invalid binding */
		if (err == -EOVERFLOW) {
			dev_err(gmi->dev,
				"failed to decode CS: invalid ranges length\n");
			goto error_cs;
		}

		/*
		 * If we reach here it means that the child node has an empty
		 * ranges or it does not exist at all. Attempt to decode the
		 * CS# from the reg property instead.
		 */
		err = of_property_read_u32(child, "reg", &property);
		if (err < 0) {
			dev_err(gmi->dev,
				"failed to decode CS: no reg property found\n");
			goto error_cs;
		}
	} else {
		property = ranges[1];
	}

	/* Valid chip selects are CS0-CS7 */
	if (property >= TEGRA_GMI_MAX_CHIP_SELECT) {
		dev_err(gmi->dev, "invalid chip select: %d", property);
		err = -EINVAL;
		goto error_cs;
	}

	gmi->snor_config |= TEGRA_GMI_CS_SELECT(property);

	/* The default values that are provided below are reset values */
	if (!of_property_read_u32(child, "nvidia,snor-muxed-width", &property))
		gmi->snor_timing0 |= TEGRA_GMI_MUXED_WIDTH(property);
	else
		gmi->snor_timing0 |= TEGRA_GMI_MUXED_WIDTH(1);

	if (!of_property_read_u32(child, "nvidia,snor-hold-width", &property))
		gmi->snor_timing0 |= TEGRA_GMI_HOLD_WIDTH(property);
	else
		gmi->snor_timing0 |= TEGRA_GMI_HOLD_WIDTH(1);

	if (!of_property_read_u32(child, "nvidia,snor-adv-width", &property))
		gmi->snor_timing0 |= TEGRA_GMI_ADV_WIDTH(property);
	else
		gmi->snor_timing0 |= TEGRA_GMI_ADV_WIDTH(1);

	if (!of_property_read_u32(child, "nvidia,snor-ce-width", &property))
		gmi->snor_timing0 |= TEGRA_GMI_CE_WIDTH(property);
	else
		gmi->snor_timing0 |= TEGRA_GMI_CE_WIDTH(4);

	if (!of_property_read_u32(child, "nvidia,snor-we-width", &property))
		gmi->snor_timing1 |= TEGRA_GMI_WE_WIDTH(property);
	else
		gmi->snor_timing1 |= TEGRA_GMI_WE_WIDTH(1);

	if (!of_property_read_u32(child, "nvidia,snor-oe-width", &property))
		gmi->snor_timing1 |= TEGRA_GMI_OE_WIDTH(property);
	else
		gmi->snor_timing1 |= TEGRA_GMI_OE_WIDTH(1);

	if (!of_property_read_u32(child, "nvidia,snor-wait-width", &property))
		gmi->snor_timing1 |= TEGRA_GMI_WAIT_WIDTH(property);
	else
		gmi->snor_timing1 |= TEGRA_GMI_WAIT_WIDTH(3);

error_cs:
	of_node_put(child);
	return err;
}

static int tegra_gmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_gmi *gmi;
	int err;

	gmi = devm_kzalloc(dev, sizeof(*gmi), GFP_KERNEL);
	if (!gmi)
		return -ENOMEM;

	platform_set_drvdata(pdev, gmi);
	gmi->dev = dev;

	gmi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gmi->base))
		return PTR_ERR(gmi->base);

	gmi->clk = devm_clk_get(dev, "gmi");
	if (IS_ERR(gmi->clk)) {
		dev_err(dev, "can not get clock\n");
		return PTR_ERR(gmi->clk);
	}

	gmi->rst = devm_reset_control_get(dev, "gmi");
	if (IS_ERR(gmi->rst)) {
		dev_err(dev, "can not get reset\n");
		return PTR_ERR(gmi->rst);
	}

	err = devm_tegra_core_dev_init_opp_table_common(&pdev->dev);
	if (err)
		return err;

	err = tegra_gmi_parse_dt(gmi);
	if (err)
		return err;

	err = tegra_gmi_enable(gmi);
	if (err < 0)
		return err;

	err = of_platform_default_populate(dev->of_node, NULL, dev);
	if (err < 0) {
		dev_err(dev, "fail to create devices.\n");
		tegra_gmi_disable(gmi);
		return err;
	}

	return 0;
}

static void tegra_gmi_remove(struct platform_device *pdev)
{
	struct tegra_gmi *gmi = platform_get_drvdata(pdev);

	of_platform_depopulate(gmi->dev);
	tegra_gmi_disable(gmi);
}

static int __maybe_unused tegra_gmi_runtime_resume(struct device *dev)
{
	struct tegra_gmi *gmi = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(gmi->clk);
	if (err < 0) {
		dev_err(gmi->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra_gmi_runtime_suspend(struct device *dev)
{
	struct tegra_gmi *gmi = dev_get_drvdata(dev);

	clk_disable_unprepare(gmi->clk);

	return 0;
}

static const struct dev_pm_ops tegra_gmi_pm = {
	SET_RUNTIME_PM_OPS(tegra_gmi_runtime_suspend, tegra_gmi_runtime_resume,
			   NULL)
};

static const struct of_device_id tegra_gmi_id_table[] = {
	{ .compatible = "nvidia,tegra20-gmi", },
	{ .compatible = "nvidia,tegra30-gmi", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_gmi_id_table);

static struct platform_driver tegra_gmi_driver = {
	.probe = tegra_gmi_probe,
	.remove = tegra_gmi_remove,
	.driver = {
		.name		= "tegra-gmi",
		.of_match_table	= tegra_gmi_id_table,
		.pm = &tegra_gmi_pm,
	},
};
module_platform_driver(tegra_gmi_driver);

MODULE_AUTHOR("Mirza Krak <mirza.krak@gmail.com");
MODULE_DESCRIPTION("NVIDIA Tegra GMI Bus Driver");
MODULE_LICENSE("GPL v2");
