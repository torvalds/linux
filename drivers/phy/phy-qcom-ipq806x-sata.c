/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>

struct qcom_ipq806x_sata_phy {
	void __iomem *mmio;
	struct clk *cfg_clk;
	struct device *dev;
};

#define __set(v, a, b)	(((v) << (b)) & GENMASK(a, b))

#define SATA_PHY_P0_PARAM0		0x200
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN3(x)	__set(x, 17, 12)
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN3_MASK	GENMASK(17, 12)
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN2(x)	__set(x, 11, 6)
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN2_MASK	GENMASK(11, 6)
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN1(x)	__set(x, 5, 0)
#define SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN1_MASK	GENMASK(5, 0)

#define SATA_PHY_P0_PARAM1		0x204
#define SATA_PHY_P0_PARAM1_RESERVED_BITS31_21(x)	__set(x, 31, 21)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN3(x)	__set(x, 20, 14)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN3_MASK	GENMASK(20, 14)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN2(x)	__set(x, 13, 7)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN2_MASK	GENMASK(13, 7)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN1(x)	__set(x, 6, 0)
#define SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN1_MASK	GENMASK(6, 0)

#define SATA_PHY_P0_PARAM2		0x208
#define SATA_PHY_P0_PARAM2_RX_EQ(x)	__set(x, 20, 18)
#define SATA_PHY_P0_PARAM2_RX_EQ_MASK	GENMASK(20, 18)

#define SATA_PHY_P0_PARAM3		0x20C
#define SATA_PHY_SSC_EN			0x8
#define SATA_PHY_P0_PARAM4		0x210
#define SATA_PHY_REF_SSP_EN		0x2
#define SATA_PHY_RESET			0x1

static int qcom_ipq806x_sata_phy_init(struct phy *generic_phy)
{
	struct qcom_ipq806x_sata_phy *phy = phy_get_drvdata(generic_phy);
	u32 reg;

	/* Setting SSC_EN to 1 */
	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM3);
	reg = reg | SATA_PHY_SSC_EN;
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM3);

	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM0) &
			~(SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN3_MASK |
			  SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN2_MASK |
			  SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN1_MASK);
	reg |= SATA_PHY_P0_PARAM0_P0_TX_PREEMPH_GEN3(0xf);
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM0);

	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM1) &
			~(SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN3_MASK |
			  SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN2_MASK |
			  SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN1_MASK);
	reg |= SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN3(0x55) |
		SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN2(0x55) |
		SATA_PHY_P0_PARAM1_P0_TX_AMPLITUDE_GEN1(0x55);
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM1);

	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM2) &
		~SATA_PHY_P0_PARAM2_RX_EQ_MASK;
	reg |= SATA_PHY_P0_PARAM2_RX_EQ(0x3);
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM2);

	/* Setting PHY_RESET to 1 */
	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM4);
	reg = reg | SATA_PHY_RESET;
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM4);

	/* Setting REF_SSP_EN to 1 */
	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM4);
	reg = reg | SATA_PHY_REF_SSP_EN | SATA_PHY_RESET;
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM4);

	/* make sure all changes complete before we let the PHY out of reset */
	mb();

	/* sleep for max. 50us more to combine processor wakeups */
	usleep_range(20, 20 + 50);

	/* Clearing PHY_RESET to 0 */
	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM4);
	reg = reg & ~SATA_PHY_RESET;
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM4);

	return 0;
}

static int qcom_ipq806x_sata_phy_exit(struct phy *generic_phy)
{
	struct qcom_ipq806x_sata_phy *phy = phy_get_drvdata(generic_phy);
	u32 reg;

	/* Setting PHY_RESET to 1 */
	reg = readl_relaxed(phy->mmio + SATA_PHY_P0_PARAM4);
	reg = reg | SATA_PHY_RESET;
	writel_relaxed(reg, phy->mmio + SATA_PHY_P0_PARAM4);

	return 0;
}

static struct phy_ops qcom_ipq806x_sata_phy_ops = {
	.init		= qcom_ipq806x_sata_phy_init,
	.exit		= qcom_ipq806x_sata_phy_exit,
	.owner		= THIS_MODULE,
};

static int qcom_ipq806x_sata_phy_probe(struct platform_device *pdev)
{
	struct qcom_ipq806x_sata_phy *phy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct phy_provider *phy_provider;
	struct phy *generic_phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy->mmio))
		return PTR_ERR(phy->mmio);

	generic_phy = devm_phy_create(dev, NULL, &qcom_ipq806x_sata_phy_ops,
				      NULL);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "%s: failed to create phy\n", __func__);
		return PTR_ERR(generic_phy);
	}

	phy->dev = dev;
	phy_set_drvdata(generic_phy, phy);
	platform_set_drvdata(pdev, phy);

	phy->cfg_clk = devm_clk_get(dev, "cfg");
	if (IS_ERR(phy->cfg_clk)) {
		dev_err(dev, "Failed to get sata cfg clock\n");
		return PTR_ERR(phy->cfg_clk);
	}

	ret = clk_prepare_enable(phy->cfg_clk);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		clk_disable_unprepare(phy->cfg_clk);
		dev_err(dev, "%s: failed to register phy\n", __func__);
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static int qcom_ipq806x_sata_phy_remove(struct platform_device *pdev)
{
	struct qcom_ipq806x_sata_phy *phy = platform_get_drvdata(pdev);

	clk_disable_unprepare(phy->cfg_clk);

	return 0;
}

static const struct of_device_id qcom_ipq806x_sata_phy_of_match[] = {
	{ .compatible = "qcom,ipq806x-sata-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_ipq806x_sata_phy_of_match);

static struct platform_driver qcom_ipq806x_sata_phy_driver = {
	.probe	= qcom_ipq806x_sata_phy_probe,
	.remove	= qcom_ipq806x_sata_phy_remove,
	.driver = {
		.name	= "qcom-ipq806x-sata-phy",
		.owner	= THIS_MODULE,
		.of_match_table	= qcom_ipq806x_sata_phy_of_match,
	}
};
module_platform_driver(qcom_ipq806x_sata_phy_driver);

MODULE_DESCRIPTION("QCOM IPQ806x SATA PHY driver");
MODULE_LICENSE("GPL v2");
