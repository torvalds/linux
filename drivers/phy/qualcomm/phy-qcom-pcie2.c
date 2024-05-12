// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019, Linaro Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <dt-bindings/phy/phy.h>

#define PCIE20_PARF_PHY_STTS         0x3c
#define PCIE2_PHY_RESET_CTRL         0x44
#define PCIE20_PARF_PHY_REFCLK_CTRL2 0xa0
#define PCIE20_PARF_PHY_REFCLK_CTRL3 0xa4
#define PCIE20_PARF_PCS_SWING_CTRL1  0x88
#define PCIE20_PARF_PCS_SWING_CTRL2  0x8c
#define PCIE20_PARF_PCS_DEEMPH1      0x74
#define PCIE20_PARF_PCS_DEEMPH2      0x78
#define PCIE20_PARF_PCS_DEEMPH3      0x7c
#define PCIE20_PARF_CONFIGBITS       0x84
#define PCIE20_PARF_PHY_CTRL3        0x94
#define PCIE20_PARF_PCS_CTRL         0x80

#define TX_AMP_VAL                   120
#define PHY_RX0_EQ_GEN1_VAL          0
#define PHY_RX0_EQ_GEN2_VAL          4
#define TX_DEEMPH_GEN1_VAL           24
#define TX_DEEMPH_GEN2_3_5DB_VAL     26
#define TX_DEEMPH_GEN2_6DB_VAL       36
#define PHY_TX0_TERM_OFFST_VAL       0

struct qcom_phy {
	struct device *dev;
	void __iomem *base;

	struct regulator_bulk_data vregs[2];

	struct reset_control *phy_reset;
	struct reset_control *pipe_reset;
	struct clk *pipe_clk;
};

static int qcom_pcie2_phy_init(struct phy *phy)
{
	struct qcom_phy *qphy = phy_get_drvdata(phy);
	int ret;

	ret = reset_control_deassert(qphy->phy_reset);
	if (ret) {
		dev_err(qphy->dev, "cannot deassert pipe reset\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(qphy->vregs), qphy->vregs);
	if (ret)
		reset_control_assert(qphy->phy_reset);

	return ret;
}

static int qcom_pcie2_phy_power_on(struct phy *phy)
{
	struct qcom_phy *qphy = phy_get_drvdata(phy);
	int ret;
	u32 val;

	/* Program REF_CLK source */
	val = readl(qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL2);
	val &= ~BIT(1);
	writel(val, qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL2);

	usleep_range(1000, 2000);

	/* Don't use PAD for refclock */
	val = readl(qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL2);
	val &= ~BIT(0);
	writel(val, qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL2);

	/* Program SSP ENABLE */
	val = readl(qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL3);
	val |= BIT(0);
	writel(val, qphy->base + PCIE20_PARF_PHY_REFCLK_CTRL3);

	usleep_range(1000, 2000);

	/* Assert Phy SW Reset */
	val = readl(qphy->base + PCIE2_PHY_RESET_CTRL);
	val |= BIT(0);
	writel(val, qphy->base + PCIE2_PHY_RESET_CTRL);

	/* Program Tx Amplitude */
	val = readl(qphy->base + PCIE20_PARF_PCS_SWING_CTRL1);
	val &= ~0x7f;
	val |= TX_AMP_VAL;
	writel(val, qphy->base + PCIE20_PARF_PCS_SWING_CTRL1);

	val = readl(qphy->base + PCIE20_PARF_PCS_SWING_CTRL2);
	val &= ~0x7f;
	val |= TX_AMP_VAL;
	writel(val, qphy->base + PCIE20_PARF_PCS_SWING_CTRL2);

	/* Program De-Emphasis */
	val = readl(qphy->base + PCIE20_PARF_PCS_DEEMPH1);
	val &= ~0x3f;
	val |= TX_DEEMPH_GEN2_6DB_VAL;
	writel(val, qphy->base + PCIE20_PARF_PCS_DEEMPH1);

	val = readl(qphy->base + PCIE20_PARF_PCS_DEEMPH2);
	val &= ~0x3f;
	val |= TX_DEEMPH_GEN2_3_5DB_VAL;
	writel(val, qphy->base + PCIE20_PARF_PCS_DEEMPH2);

	val = readl(qphy->base + PCIE20_PARF_PCS_DEEMPH3);
	val &= ~0x3f;
	val |= TX_DEEMPH_GEN1_VAL;
	writel(val, qphy->base + PCIE20_PARF_PCS_DEEMPH3);

	/* Program Rx_Eq */
	val = readl(qphy->base + PCIE20_PARF_CONFIGBITS);
	val &= ~0x7;
	val |= PHY_RX0_EQ_GEN2_VAL;
	writel(val, qphy->base + PCIE20_PARF_CONFIGBITS);

	/* Program Tx0_term_offset */
	val = readl(qphy->base + PCIE20_PARF_PHY_CTRL3);
	val &= ~0x1f;
	val |= PHY_TX0_TERM_OFFST_VAL;
	writel(val, qphy->base + PCIE20_PARF_PHY_CTRL3);

	/* disable Tx2Rx Loopback */
	val = readl(qphy->base + PCIE20_PARF_PCS_CTRL);
	val &= ~BIT(1);
	writel(val, qphy->base + PCIE20_PARF_PCS_CTRL);

	/* De-assert Phy SW Reset */
	val = readl(qphy->base + PCIE2_PHY_RESET_CTRL);
	val &= ~BIT(0);
	writel(val, qphy->base + PCIE2_PHY_RESET_CTRL);

	usleep_range(1000, 2000);

	ret = reset_control_deassert(qphy->pipe_reset);
	if (ret) {
		dev_err(qphy->dev, "cannot deassert pipe reset\n");
		goto out;
	}

	clk_set_rate(qphy->pipe_clk, 250000000);

	ret = clk_prepare_enable(qphy->pipe_clk);
	if (ret) {
		dev_err(qphy->dev, "failed to enable pipe clock\n");
		goto out;
	}

	ret = readl_poll_timeout(qphy->base + PCIE20_PARF_PHY_STTS, val,
				 !(val & BIT(0)), 1000, 10);
	if (ret)
		dev_err(qphy->dev, "phy initialization failed\n");

out:
	return ret;
}

static int qcom_pcie2_phy_power_off(struct phy *phy)
{
	struct qcom_phy *qphy = phy_get_drvdata(phy);
	u32 val;

	val = readl(qphy->base + PCIE2_PHY_RESET_CTRL);
	val |= BIT(0);
	writel(val, qphy->base + PCIE2_PHY_RESET_CTRL);

	clk_disable_unprepare(qphy->pipe_clk);
	reset_control_assert(qphy->pipe_reset);

	return 0;
}

static int qcom_pcie2_phy_exit(struct phy *phy)
{
	struct qcom_phy *qphy = phy_get_drvdata(phy);

	regulator_bulk_disable(ARRAY_SIZE(qphy->vregs), qphy->vregs);
	reset_control_assert(qphy->phy_reset);

	return 0;
}

static const struct phy_ops qcom_pcie2_ops = {
	.init = qcom_pcie2_phy_init,
	.power_on = qcom_pcie2_phy_power_on,
	.power_off = qcom_pcie2_phy_power_off,
	.exit = qcom_pcie2_phy_exit,
	.owner = THIS_MODULE,
};

/*
 * Register a fixed rate pipe clock.
 *
 * The <s>_pipe_clksrc generated by PHY goes to the GCC that gate
 * controls it. The <s>_pipe_clk coming out of the GCC is requested
 * by the PHY driver for its operations.
 * We register the <s>_pipe_clksrc here. The gcc driver takes care
 * of assigning this <s>_pipe_clksrc as parent to <s>_pipe_clk.
 * Below picture shows this relationship.
 *
 *         +---------------+
 *         |   PHY block   |<<---------------------------------------+
 *         |               |                                         |
 *         |   +-------+   |                   +-----+               |
 *   I/P---^-->|  PLL  |---^--->pipe_clksrc--->| GCC |--->pipe_clk---+
 *    clk  |   +-------+   |                   +-----+
 *         +---------------+
 */
static int phy_pipe_clksrc_register(struct qcom_phy *qphy)
{
	struct device_node *np = qphy->dev->of_node;
	struct clk_fixed_rate *fixed;
	struct clk_init_data init = { };
	int ret;

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret) {
		dev_err(qphy->dev, "%s: No clock-output-names\n", np->name);
		return ret;
	}

	fixed = devm_kzalloc(qphy->dev, sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return -ENOMEM;

	init.ops = &clk_fixed_rate_ops;

	/* controllers using QMP phys use 250MHz pipe clock interface */
	fixed->fixed_rate = 250000000;
	fixed->hw.init = &init;

	ret = devm_clk_hw_register(qphy->dev, &fixed->hw);
	if (ret < 0)
		return ret;

	return devm_of_clk_add_hw_provider(qphy->dev, of_clk_hw_simple_get, &fixed->hw);
}

static int qcom_pcie2_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct qcom_phy *qphy;
	struct device *dev = &pdev->dev;
	struct phy *phy;
	int ret;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->dev = dev;
	qphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	ret = phy_pipe_clksrc_register(qphy);
	if (ret) {
		dev_err(dev, "failed to register pipe_clk\n");
		return ret;
	}

	qphy->vregs[0].supply = "vdda-vp";
	qphy->vregs[1].supply = "vdda-vph";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(qphy->vregs), qphy->vregs);
	if (ret < 0)
		return ret;

	qphy->pipe_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(qphy->pipe_clk)) {
		dev_err(dev, "failed to acquire pipe clock\n");
		return PTR_ERR(qphy->pipe_clk);
	}

	qphy->phy_reset = devm_reset_control_get_exclusive(dev, "phy");
	if (IS_ERR(qphy->phy_reset)) {
		dev_err(dev, "failed to acquire phy reset\n");
		return PTR_ERR(qphy->phy_reset);
	}

	qphy->pipe_reset = devm_reset_control_get_exclusive(dev, "pipe");
	if (IS_ERR(qphy->pipe_reset)) {
		dev_err(dev, "failed to acquire pipe reset\n");
		return PTR_ERR(qphy->pipe_reset);
	}

	phy = devm_phy_create(dev, dev->of_node, &qcom_pcie2_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create phy\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, qphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		dev_err(dev, "failed to register phy provider\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id qcom_pcie2_phy_match_table[] = {
	{ .compatible = "qcom,pcie2-phy" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_pcie2_phy_match_table);

static struct platform_driver qcom_pcie2_phy_driver = {
	.probe = qcom_pcie2_phy_probe,
	.driver = {
		.name = "phy-qcom-pcie2",
		.of_match_table = qcom_pcie2_phy_match_table,
	},
};

module_platform_driver(qcom_pcie2_phy_driver);

MODULE_DESCRIPTION("Qualcomm PCIe PHY driver");
MODULE_LICENSE("GPL v2");
