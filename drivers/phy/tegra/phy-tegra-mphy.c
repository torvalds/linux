// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// NVIDIA Tegra264 MPHY driver.

#include <dt-bindings/phy/nvidia,tegra264-mphy.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define MPHY_GO_BIT				BIT(0)

#define MPHY_RX_APB_CAPABILITY_88_8B		0x88
#define RX_HS_G1_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 24)

#define MPHY_RX_APB_CAPABILITY_94_97		0x94
#define RX_HS_G3_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 8)
#define RX_HS_G2_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 0)

#define MPHY_TX_APB_VENDOR0			0x100
#define MPHY_TX_APB_VENDOR2			0x108
#define TX_CAL_DONE				BIT(19)
#define TX_CAL_EN				BIT(15)

#define MPHY_RX_APB_VENDOR2			0x184
#define RX_CAL_DONE				BIT(19)
#define RX_CAL_EN				BIT(15)

#define MPHY_RX_APB_VENDOR3			0x188
#define RX_MPHY2UPHY_IF_OVR_CTRL		BIT(26)

#define MPHY_RX_APB_VENDOR3B			0x220
#define MPHY_RX_APB_VENDOR49			0x254

#define MPHY_EQ_TIMEOUT				0xffffffff
#define MPHY_PWR_CHANGE_CLK_BOOST		0x0017

#define MPHY_TX_OFFSET				0x1000
#define MPHY_RX_OFFSET				0x2000

struct tegra_mphy_lane {
	void __iomem *regs;
	void __iomem *tx_regs;
	void __iomem *rx_regs;

	struct reset_control *rst_rx;
	struct reset_control *rst_tx;

	struct phy *tx;
	struct phy *rx;
};

struct tegra_mphy {
	struct device *dev;

	struct tegra_mphy_lane l0;
	struct tegra_mphy_lane l1;

	unsigned int num_clks;
	struct clk_bulk_data *clks;

	struct reset_control *rst_clk_ctl;

	unsigned int power_count;
};

static int tegra_mphy_get_clocks(struct tegra_mphy *mphy)
{
	struct device *dev = mphy->dev;
	int num_clks;

	num_clks = devm_clk_bulk_get_all(dev, &mphy->clks);
	if (num_clks < 0)
		return dev_err_probe(dev, num_clks, "failed to get clocks\n");

	mphy->num_clks = num_clks;

	return 0;
}

static int tegra_mphy_get_resets(struct tegra_mphy *mphy)
{
	struct device *dev = mphy->dev;

	mphy->rst_clk_ctl = devm_reset_control_get_exclusive(dev, "clk-ctl");
	if (IS_ERR(mphy->rst_clk_ctl))
		return dev_err_probe(dev, PTR_ERR(mphy->rst_clk_ctl),
				     "failed to get clk-ctl reset\n");

	mphy->l0.rst_rx = devm_reset_control_get_exclusive(dev, "l0-rx");
	if (IS_ERR(mphy->l0.rst_rx))
		return dev_err_probe(dev, PTR_ERR(mphy->l0.rst_rx), "failed to get l0-rx reset\n");

	mphy->l0.rst_tx = devm_reset_control_get_exclusive(dev, "l0-tx");
	if (IS_ERR(mphy->l0.rst_tx))
		return dev_err_probe(dev, PTR_ERR(mphy->l0.rst_tx), "failed to get l0-tx reset\n");

	mphy->l1.rst_rx = devm_reset_control_get_exclusive(dev, "l1-rx");
	if (IS_ERR(mphy->l1.rst_rx))
		return dev_err_probe(dev, PTR_ERR(mphy->l1.rst_rx), "failed to get l1-rx reset\n");

	mphy->l1.rst_tx = devm_reset_control_get_exclusive(dev, "l1-tx");
	if (IS_ERR(mphy->l1.rst_tx))
		return dev_err_probe(dev, PTR_ERR(mphy->l1.rst_tx), "failed to get l1-tx reset\n");

	return 0;
}

static int tegra_mphy_rx_write_kick_go(struct tegra_mphy_lane *lane, u32 offset, u32 value)
{
	u32 v;

	writel(value, lane->rx_regs + offset);

	v = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	v |= MPHY_GO_BIT;
	writel(v, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	return readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, v,
				  (v & MPHY_GO_BIT) == 0, 25, 5000);
}

static int tegra_mphy_rx_power_on(struct phy *phy)
{
	struct tegra_mphy *mphy = dev_get_drvdata(phy->dev.parent);
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);
	u32 value;
	int err;

	if (mphy->power_count++ == 0) {
		err = clk_bulk_prepare_enable(mphy->num_clks, mphy->clks);
		if (err) {
			mphy->power_count--;
			dev_err(&phy->dev, "failed to enable clocks: %d\n", err);
			return err;
		}
		reset_control_deassert(mphy->rst_clk_ctl);
	}

	reset_control_deassert(lane->rst_rx);
	reset_control_deassert(lane->rst_tx);

	err = tegra_mphy_rx_write_kick_go(lane, MPHY_RX_APB_VENDOR3B, MPHY_EQ_TIMEOUT);
	if (err) {
		dev_err(&phy->dev, "eq_timeout programming failed: %d\n", err);
		goto err_reset;
	}

	err = tegra_mphy_rx_write_kick_go(lane, MPHY_RX_APB_VENDOR49, MPHY_PWR_CHANGE_CLK_BOOST);
	if (err) {
		dev_err(&phy->dev, "pwr_change_clk_boost programming failed: %d\n", err);
		goto err_reset;
	}

	value = readl(lane->regs + MPHY_RX_APB_CAPABILITY_88_8B);
	value &= ~RX_HS_G1_SYNC_LENGTH_CAPABILITY(~0);
	value |= RX_HS_G1_SYNC_LENGTH_CAPABILITY(0xf);
	writel(value, lane->regs + MPHY_RX_APB_CAPABILITY_88_8B);

	value = readl(lane->regs + MPHY_RX_APB_CAPABILITY_94_97);
	value &= ~RX_HS_G3_SYNC_LENGTH_CAPABILITY(~0);
	value |= RX_HS_G3_SYNC_LENGTH_CAPABILITY(0xf);
	value &= ~RX_HS_G2_SYNC_LENGTH_CAPABILITY(~0);
	value |= RX_HS_G2_SYNC_LENGTH_CAPABILITY(0xf);
	writel(value, lane->regs + MPHY_RX_APB_CAPABILITY_94_97);

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR3);
	value |= RX_MPHY2UPHY_IF_OVR_CTRL;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR3);

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	value |= MPHY_GO_BIT;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	err = readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, value,
				 (value & MPHY_GO_BIT) == 0, 25, 5000);
	if (err) {
		dev_err(&phy->dev, "RX cap update failed: %d\n", err);
		goto err_reset;
	}

	return 0;

err_reset:
	reset_control_assert(lane->rst_rx);
	reset_control_assert(lane->rst_tx);

	if (--mphy->power_count == 0) {
		reset_control_assert(mphy->rst_clk_ctl);
		clk_bulk_disable_unprepare(mphy->num_clks, mphy->clks);
	}

	return err;
}

static int tegra_mphy_rx_power_off(struct phy *phy)
{
	struct tegra_mphy *mphy = dev_get_drvdata(phy->dev.parent);
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);

	if (WARN_ON(mphy->power_count == 0))
		return -EINVAL;

	reset_control_assert(lane->rst_rx);
	reset_control_assert(lane->rst_tx);

	if (--mphy->power_count == 0)
		clk_bulk_disable_unprepare(mphy->num_clks, mphy->clks);

	return 0;
}

static int tegra_mphy_rx_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);
	u32 value;
	int err;

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	value |= RX_CAL_EN;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	value |= MPHY_GO_BIT;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	err = readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, value,
				 (value & MPHY_GO_BIT) == 0, 25, 5000);
	if (err)
		dev_err(&phy->dev, "failed to arm RX calibration: %d\n", err);

	return err;
}

static int tegra_mphy_rx_calibrate(struct phy *phy)
{
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);
	u32 value;
	int err;

	err = readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, value,
				 (value & RX_CAL_DONE) != 0, 25, 100000);
	if (err) {
		dev_err(&phy->dev, "RX calibration timed out: %d\n", err);
		return err;
	}

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	value &= ~RX_CAL_EN;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	value = readl(lane->rx_regs + MPHY_RX_APB_VENDOR2);
	value |= MPHY_GO_BIT;
	writel(value, lane->rx_regs + MPHY_RX_APB_VENDOR2);

	err = readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, value,
				 (value & MPHY_GO_BIT) == 0, 25, 5000);
	if (err) {
		dev_err(&phy->dev, "failed to clear RX calibration: %d\n", err);
		return err;
	}

	err = readl_poll_timeout(lane->rx_regs + MPHY_RX_APB_VENDOR2, value,
				 (value & RX_CAL_DONE) == 0, 25, 100000);
	if (err)
		dev_err(&phy->dev, "RX calibration failed to clear: %d\n", err);

	return err;
}

static const struct phy_ops tegra_mphy_rx_ops = {
	.power_on = tegra_mphy_rx_power_on,
	.power_off = tegra_mphy_rx_power_off,
	.configure = tegra_mphy_rx_configure,
	.calibrate = tegra_mphy_rx_calibrate,
};

static int tegra_mphy_tx_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);
	u32 value;
	int err;

	value = readl(lane->tx_regs + MPHY_TX_APB_VENDOR2);
	value |= TX_CAL_EN;
	writel(value, lane->tx_regs + MPHY_TX_APB_VENDOR2);

	value = readl(lane->tx_regs + MPHY_TX_APB_VENDOR0);
	value |= MPHY_GO_BIT;
	writel(value, lane->tx_regs + MPHY_TX_APB_VENDOR0);

	err = readl_poll_timeout(lane->tx_regs + MPHY_TX_APB_VENDOR0, value,
				 (value & MPHY_GO_BIT) == 0, 25, 5000);
	if (err)
		dev_err(&phy->dev, "failed to arm TX calibration: %d\n", err);

	return err;
}

static int tegra_mphy_tx_calibrate(struct phy *phy)
{
	struct tegra_mphy_lane *lane = phy_get_drvdata(phy);
	u32 value;
	int err;

	err = readl_poll_timeout(lane->tx_regs + MPHY_TX_APB_VENDOR2, value,
				 (value & TX_CAL_DONE) != 0, 25, 50000);
	if (err) {
		dev_err(&phy->dev, "TX calibration timed out: %d\n", err);
		return err;
	}

	value = readl(lane->tx_regs + MPHY_TX_APB_VENDOR2);
	value &= ~TX_CAL_EN;
	writel(value, lane->tx_regs + MPHY_TX_APB_VENDOR2);

	value = readl(lane->tx_regs + MPHY_TX_APB_VENDOR0);
	value |= MPHY_GO_BIT;
	writel(value, lane->tx_regs + MPHY_TX_APB_VENDOR0);

	err = readl_poll_timeout(lane->tx_regs + MPHY_TX_APB_VENDOR0, value,
				 (value & MPHY_GO_BIT) == 0, 25, 5000);
	if (err) {
		dev_err(&phy->dev, "GO bit failed to clear: %d\n", err);
		return err;
	}

	err = readl_poll_timeout(lane->tx_regs + MPHY_TX_APB_VENDOR2, value,
				 (value & TX_CAL_DONE) == 0, 25, 50000);
	if (err)
		dev_err(&phy->dev, "TX calibration failed: %d\n", err);

	return err;
}

static const struct phy_ops tegra_mphy_tx_ops = {
	.configure = tegra_mphy_tx_configure,
	.calibrate = tegra_mphy_tx_calibrate,
};

static struct phy *tegra_mphy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct tegra_mphy *mphy = dev_get_drvdata(dev);

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	switch (args->args[0]) {
	case TEGRA_MPHY_L0_TX:
		return mphy->l0.tx;
	case TEGRA_MPHY_L0_RX:
		return mphy->l0.rx;
	case TEGRA_MPHY_L1_TX:
		return mphy->l1.tx;
	case TEGRA_MPHY_L1_RX:
		return mphy->l1.rx;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int tegra_mphy_create_phys(struct tegra_mphy *mphy)
{
	struct device *dev = mphy->dev;

	mphy->l0.tx = devm_phy_create(dev, dev->of_node, &tegra_mphy_tx_ops);
	if (IS_ERR(mphy->l0.tx))
		return dev_err_probe(dev, PTR_ERR(mphy->l0.tx), "failed to create l0-tx PHY\n");
	phy_set_drvdata(mphy->l0.tx, &mphy->l0);

	mphy->l0.rx = devm_phy_create(dev, dev->of_node, &tegra_mphy_rx_ops);
	if (IS_ERR(mphy->l0.rx))
		return dev_err_probe(dev, PTR_ERR(mphy->l0.rx), "failed to create l0-rx PHY\n");
	phy_set_drvdata(mphy->l0.rx, &mphy->l0);

	mphy->l1.tx = devm_phy_create(dev, dev->of_node, &tegra_mphy_tx_ops);
	if (IS_ERR(mphy->l1.tx))
		return dev_err_probe(dev, PTR_ERR(mphy->l1.tx), "failed to create l1-tx PHY\n");
	phy_set_drvdata(mphy->l1.tx, &mphy->l1);

	mphy->l1.rx = devm_phy_create(dev, dev->of_node, &tegra_mphy_rx_ops);
	if (IS_ERR(mphy->l1.rx))
		return dev_err_probe(dev, PTR_ERR(mphy->l1.rx), "failed to create l1-rx PHY\n");
	phy_set_drvdata(mphy->l1.rx, &mphy->l1);

	return 0;
}

static int tegra_mphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct tegra_mphy *mphy;
	int err;

	mphy = devm_kzalloc(dev, sizeof(*mphy), GFP_KERNEL);
	if (!mphy)
		return -ENOMEM;

	mphy->dev = dev;

	mphy->l0.regs = devm_platform_ioremap_resource_byname(pdev, "l0");
	if (IS_ERR(mphy->l0.regs))
		return PTR_ERR(mphy->l0.regs);

	mphy->l1.regs = devm_platform_ioremap_resource_byname(pdev, "l1");
	if (IS_ERR(mphy->l1.regs))
		return PTR_ERR(mphy->l1.regs);

	mphy->l0.tx_regs = mphy->l0.regs + MPHY_TX_OFFSET;
	mphy->l0.rx_regs = mphy->l0.regs + MPHY_RX_OFFSET;

	mphy->l1.tx_regs = mphy->l1.regs + MPHY_TX_OFFSET;
	mphy->l1.rx_regs = mphy->l1.regs + MPHY_RX_OFFSET;

	err = tegra_mphy_get_clocks(mphy);
	if (err)
		return err;

	err = tegra_mphy_get_resets(mphy);
	if (err)
		return err;

	err = tegra_mphy_create_phys(mphy);
	if (err)
		return err;

	platform_set_drvdata(pdev, mphy);

	provider = devm_of_phy_provider_register(dev, tegra_mphy_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider),
				     "failed to register PHY\n");

	return 0;
}

static const struct of_device_id tegra_mphy_of_table[] = {
	{ .compatible = "nvidia,tegra264-mphy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra_mphy_of_table);

static struct platform_driver tegra_mphy_driver = {
	.driver = {
		.name = "tegra-mphy",
		.of_match_table = tegra_mphy_of_table,
	},
	.probe = tegra_mphy_probe,
};
module_platform_driver(tegra_mphy_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_AUTHOR("Kartik Rajput <kkartik@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra MPHY driver");
MODULE_LICENSE("GPL");
