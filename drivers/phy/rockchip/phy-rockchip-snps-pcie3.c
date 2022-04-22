// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip PCIE3.0 phy driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/pcie.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <dt-bindings/phy/phy-snps-pcie3.h>

/* Register for RK3568 */
#define GRF_PCIE30PHY_CON1 0x4
#define GRF_PCIE30PHY_CON6 0x18
#define GRF_PCIE30PHY_CON9 0x24
#define GRF_PCIE30PHY_STATUS0 0x80
#define SRAM_INIT_DONE(reg) (reg & BIT(14))

/* Register for RK3588 */
#define PHP_GRF_PCIESEL_CON 0x100
#define RK3588_PCIE3PHY_GRF_CMN_CON0 0x0
#define RK3588_PCIE3PHY_GRF_PHY0_STATUS1 0x904
#define RK3588_PCIE3PHY_GRF_PHY1_STATUS1 0xa04
#define RK3588_SRAM_INIT_DONE(reg) (reg & BIT(0))

struct rockchip_p3phy_ops;

struct rockchip_p3phy_priv {
	const struct rockchip_p3phy_ops *ops;
	void __iomem *mmio;
	/* mode: RC, EP */
	int mode;
	/* pcie30_phymode: Aggregation, Bifurcation */
	int pcie30_phymode;
	struct regmap *phy_grf;
	struct regmap *pipe_grf;
	struct reset_control *p30phy;
	struct phy *phy;
	struct clk_bulk_data *clks;
	int num_clks;
	bool is_bifurcation;
};

struct rockchip_p3phy_ops {
	int (*phy_init)(struct rockchip_p3phy_priv *priv);
};

static int rockchip_p3phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct rockchip_p3phy_priv *priv = phy_get_drvdata(phy);

	/* Acutally We don't care EP/RC mode, but just record it */
	switch (submode) {
	case PHY_MODE_PCIE_RC:
		priv->mode = PHY_MODE_PCIE_RC;
		break;
	case PHY_MODE_PCIE_EP:
		priv->mode = PHY_MODE_PCIE_EP;
		break;
	case PHY_MODE_PCIE_BIFURCATION:
		priv->is_bifurcation = true;
		break;
	default:
		pr_info("%s, invalid mode\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int rockchip_p3phy_rk3568_init(struct rockchip_p3phy_priv *priv)
{
	int ret = 0;
	u32 reg;

	/* Deassert PCIe PMA output clamp mode */
	regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON9,
		     (0x1 << 15) | (0x1 << 31));
	/* Set bifurcation if needed, and it doesn't care RC/EP */
	if (priv->is_bifurcation) {
		regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON6,
			     0x1 | (0xf << 16));
		regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON1,
			     (0x1 << 15) | (0x1 << 31));
	}

	reset_control_deassert(priv->p30phy);

	udelay(10);
	/* Updata RX VCO calibration controls */
	writel(0x2800, priv->mmio + (0x104a << 2));
	writel(0x2800, priv->mmio + (0x114a << 2));
	udelay(10);

	ret = regmap_read_poll_timeout(priv->phy_grf,
				       GRF_PCIE30PHY_STATUS0,
				       reg, SRAM_INIT_DONE(reg),
				       0, 500);
	if (ret)
		pr_err("%s: lock failed 0x%x, check input refclk and power supply\n",
		       __func__, reg);
	return ret;
}

static const struct rockchip_p3phy_ops rk3568_ops = {
	.phy_init = rockchip_p3phy_rk3568_init,
};

static int rockchip_p3phy_rk3588_init(struct rockchip_p3phy_priv *priv)
{
	int ret = 0;
	u32 reg;

	/* Deassert PCIe PMA output clamp mode */
	regmap_write(priv->phy_grf, RK3588_PCIE3PHY_GRF_CMN_CON0,
		     (0x1 << 8) | (0x1 << 24));

	reset_control_deassert(priv->p30phy);

	ret = regmap_read_poll_timeout(priv->phy_grf,
				       RK3588_PCIE3PHY_GRF_PHY0_STATUS1,
				       reg, RK3588_SRAM_INIT_DONE(reg),
				       0, 500);
	ret |= regmap_read_poll_timeout(priv->phy_grf,
					RK3588_PCIE3PHY_GRF_PHY1_STATUS1,
					reg, RK3588_SRAM_INIT_DONE(reg),
					0, 500);
	if (ret)
		pr_err("%s: lock failed 0x%x, check input refclk and power supply\n",
		       __func__, reg);
	return ret;
}

static const struct rockchip_p3phy_ops rk3588_ops = {
	.phy_init = rockchip_p3phy_rk3588_init,
};

static int rochchip_p3phy_init(struct phy *phy)
{
	struct rockchip_p3phy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		pr_err("failed to enable PCIe bulk clks %d\n", ret);
		return ret;
	}

	reset_control_assert(priv->p30phy);
	udelay(1);

	if (priv->ops->phy_init) {
		ret = priv->ops->phy_init(priv);
		if (ret)
			clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	};

	return ret;
}

static int rochchip_p3phy_exit(struct phy *phy)
{
	struct rockchip_p3phy_priv *priv = phy_get_drvdata(phy);
	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	reset_control_assert(priv->p30phy);
	return 0;
}

static const struct phy_ops rochchip_p3phy_ops = {
	.init = rochchip_p3phy_init,
	.exit = rochchip_p3phy_exit,
	.set_mode = rockchip_p3phy_set_mode,
	.owner = THIS_MODULE,
};

static int rockchip_p3phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct rockchip_p3phy_priv *priv;
	struct device_node *np = dev->of_node;
	struct resource *res;
	int ret;
	u32 val, reg;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->mmio)) {
		ret = PTR_ERR(priv->mmio);
		return ret;
	}

	priv->ops = of_device_get_match_data(&pdev->dev);
	if (!priv->ops) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	priv->phy_grf = syscon_regmap_lookup_by_phandle(np, "rockchip,phy-grf");
	if (IS_ERR(priv->phy_grf)) {
		dev_err(dev, "failed to find rockchip,phy_grf regmap\n");
		return PTR_ERR(priv->phy_grf);
	}

	priv->pipe_grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							 "rockchip,pipe-grf");
	if (IS_ERR(priv->pipe_grf))
		dev_info(dev, "failed to find rockchip,pipe_grf regmap\n");

	ret = device_property_read_u32(dev, "rockchip,pcie30-phymode", &val);
	if (!ret)
		priv->pcie30_phymode = val;
	else
		priv->pcie30_phymode = PHY_MODE_PCIE_AGGREGATION;

	/* Select correct pcie30_phymode */
	if (priv->pcie30_phymode > 4)
		priv->pcie30_phymode = PHY_MODE_PCIE_AGGREGATION;

	regmap_write(priv->phy_grf, RK3588_PCIE3PHY_GRF_CMN_CON0,
		     (0x7<<16) | priv->pcie30_phymode);

	/* Set pcie1ln_sel in PHP_GRF_PCIESEL_CON */
	if (!IS_ERR(priv->pipe_grf)) {
		reg = priv->pcie30_phymode & 3;
		if (reg)
			regmap_write(priv->pipe_grf, PHP_GRF_PCIESEL_CON,
				     (reg << 16) | reg);
	};

	priv->phy = devm_phy_create(dev, NULL, &rochchip_p3phy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	priv->p30phy = devm_reset_control_get(dev, "phy");
	if (IS_ERR(priv->p30phy)) {
		dev_warn(dev, "no phy reset control specified\n");
		priv->p30phy = NULL;
	}

	priv->num_clks = devm_clk_bulk_get_all(dev, &priv->clks);
	if (priv->num_clks < 1)
		return -ENODEV;

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_p3phy_of_match[] = {
	{ .compatible = "rockchip,rk3568-pcie3-phy", .data = &rk3568_ops },
	{ .compatible = "rockchip,rk3588-pcie3-phy", .data = &rk3588_ops },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_p3phy_of_match);

static struct platform_driver rockchip_p3phy_driver = {
	.probe	= rockchip_p3phy_probe,
	.driver = {
		.name = "rockchip-snps-pcie3-phy",
		.of_match_table = rockchip_p3phy_of_match,
	},
};
module_platform_driver(rockchip_p3phy_driver);
MODULE_DESCRIPTION("Rockchip Synopsys PCIe 3.0 PHY driver");
MODULE_LICENSE("GPL v2");
