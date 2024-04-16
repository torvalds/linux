// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2014,2017 The Linux Foundation. All rights reserved.
 * Copyright (c) 2018-2020, Linaro Limited
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define PHY_CTRL0			0x6C
#define PHY_CTRL1			0x70
#define PHY_CTRL2			0x74
#define PHY_CTRL4			0x7C

/* PHY_CTRL bits */
#define REF_PHY_EN			BIT(0)
#define LANE0_PWR_ON			BIT(2)
#define SWI_PCS_CLK_SEL			BIT(4)
#define TST_PWR_DOWN			BIT(4)
#define PHY_RESET			BIT(7)

#define NUM_BULK_CLKS			3
#define NUM_BULK_REGS			2

struct ssphy_priv {
	void __iomem *base;
	struct device *dev;
	struct reset_control *reset_com;
	struct reset_control *reset_phy;
	struct regulator_bulk_data regs[NUM_BULK_REGS];
	struct clk_bulk_data clks[NUM_BULK_CLKS];
	enum phy_mode mode;
};

static inline void qcom_ssphy_updatel(void __iomem *addr, u32 mask, u32 val)
{
	writel((readl(addr) & ~mask) | val, addr);
}

static int qcom_ssphy_do_reset(struct ssphy_priv *priv)
{
	int ret;

	if (!priv->reset_com) {
		qcom_ssphy_updatel(priv->base + PHY_CTRL1, PHY_RESET,
				   PHY_RESET);
		usleep_range(10, 20);
		qcom_ssphy_updatel(priv->base + PHY_CTRL1, PHY_RESET, 0);
	} else {
		ret = reset_control_assert(priv->reset_com);
		if (ret) {
			dev_err(priv->dev, "Failed to assert reset com\n");
			return ret;
		}

		ret = reset_control_assert(priv->reset_phy);
		if (ret) {
			dev_err(priv->dev, "Failed to assert reset phy\n");
			return ret;
		}

		usleep_range(10, 20);

		ret = reset_control_deassert(priv->reset_com);
		if (ret) {
			dev_err(priv->dev, "Failed to deassert reset com\n");
			return ret;
		}

		ret = reset_control_deassert(priv->reset_phy);
		if (ret) {
			dev_err(priv->dev, "Failed to deassert reset phy\n");
			return ret;
		}
	}

	return 0;
}

static int qcom_ssphy_power_on(struct phy *phy)
{
	struct ssphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = regulator_bulk_enable(NUM_BULK_REGS, priv->regs);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(NUM_BULK_CLKS, priv->clks);
	if (ret)
		goto err_disable_regulator;

	ret = qcom_ssphy_do_reset(priv);
	if (ret)
		goto err_disable_clock;

	writeb(SWI_PCS_CLK_SEL, priv->base + PHY_CTRL0);
	qcom_ssphy_updatel(priv->base + PHY_CTRL4, LANE0_PWR_ON, LANE0_PWR_ON);
	qcom_ssphy_updatel(priv->base + PHY_CTRL2, REF_PHY_EN, REF_PHY_EN);
	qcom_ssphy_updatel(priv->base + PHY_CTRL4, TST_PWR_DOWN, 0);

	return 0;
err_disable_clock:
	clk_bulk_disable_unprepare(NUM_BULK_CLKS, priv->clks);
err_disable_regulator:
	regulator_bulk_disable(NUM_BULK_REGS, priv->regs);

	return ret;
}

static int qcom_ssphy_power_off(struct phy *phy)
{
	struct ssphy_priv *priv = phy_get_drvdata(phy);

	qcom_ssphy_updatel(priv->base + PHY_CTRL4, LANE0_PWR_ON, 0);
	qcom_ssphy_updatel(priv->base + PHY_CTRL2, REF_PHY_EN, 0);
	qcom_ssphy_updatel(priv->base + PHY_CTRL4, TST_PWR_DOWN, TST_PWR_DOWN);

	clk_bulk_disable_unprepare(NUM_BULK_CLKS, priv->clks);
	regulator_bulk_disable(NUM_BULK_REGS, priv->regs);

	return 0;
}

static int qcom_ssphy_init_clock(struct ssphy_priv *priv)
{
	priv->clks[0].id = "ref";
	priv->clks[1].id = "ahb";
	priv->clks[2].id = "pipe";

	return devm_clk_bulk_get(priv->dev, NUM_BULK_CLKS, priv->clks);
}

static int qcom_ssphy_init_regulator(struct ssphy_priv *priv)
{
	int ret;

	priv->regs[0].supply = "vdd";
	priv->regs[1].supply = "vdda1p8";
	ret = devm_regulator_bulk_get(priv->dev, NUM_BULK_REGS, priv->regs);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(priv->dev, "Failed to get regulators\n");
		return ret;
	}

	return ret;
}

static int qcom_ssphy_init_reset(struct ssphy_priv *priv)
{
	priv->reset_com = devm_reset_control_get_optional_exclusive(priv->dev, "com");
	if (IS_ERR(priv->reset_com)) {
		dev_err(priv->dev, "Failed to get reset control com\n");
		return PTR_ERR(priv->reset_com);
	}

	if (priv->reset_com) {
		/* if reset_com is present, reset_phy is no longer optional */
		priv->reset_phy = devm_reset_control_get_exclusive(priv->dev, "phy");
		if (IS_ERR(priv->reset_phy)) {
			dev_err(priv->dev, "Failed to get reset control phy\n");
			return PTR_ERR(priv->reset_phy);
		}
	}

	return 0;
}

static const struct phy_ops qcom_ssphy_ops = {
	.power_off = qcom_ssphy_power_off,
	.power_on = qcom_ssphy_power_on,
	.owner = THIS_MODULE,
};

static int qcom_ssphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct ssphy_priv *priv;
	struct phy *phy;
	int ret;

	priv = devm_kzalloc(dev, sizeof(struct ssphy_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->mode = PHY_MODE_INVALID;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = qcom_ssphy_init_clock(priv);
	if (ret)
		return ret;

	ret = qcom_ssphy_init_reset(priv);
	if (ret)
		return ret;

	ret = qcom_ssphy_init_regulator(priv);
	if (ret)
		return ret;

	phy = devm_phy_create(dev, dev->of_node, &qcom_ssphy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "Failed to create the SS phy\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, priv);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id qcom_ssphy_match[] = {
	{ .compatible = "qcom,usb-ss-28nm-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_ssphy_match);

static struct platform_driver qcom_ssphy_driver = {
	.probe		= qcom_ssphy_probe,
	.driver = {
		.name	= "qcom-usb-ssphy",
		.of_match_table = qcom_ssphy_match,
	},
};
module_platform_driver(qcom_ssphy_driver);

MODULE_DESCRIPTION("Qualcomm SuperSpeed USB PHY driver");
MODULE_LICENSE("GPL v2");
