// SPDX-License-Identifier: GPL-2.0
/*
 * phy-uniphier-pcie.c - PHY driver for UniPhier PCIe controller
 * Copyright 2018, Socionext Inc.
 * Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/resource.h>

/* PHY */
#define PCL_PHY_CLKCTRL		0x0000
#define PORT_SEL_MASK		GENMASK(11, 9)
#define PORT_SEL_1		FIELD_PREP(PORT_SEL_MASK, 1)

#define PCL_PHY_TEST_I		0x2000
#define TESTI_DAT_MASK		GENMASK(13, 6)
#define TESTI_ADR_MASK		GENMASK(5, 1)
#define TESTI_WR_EN		BIT(0)
#define TESTIO_PHY_SHIFT	16

#define PCL_PHY_TEST_O		0x2004
#define TESTO_DAT_MASK		GENMASK(7, 0)

#define PCL_PHY_RESET		0x200c
#define PCL_PHY_RESET_N_MNMODE	BIT(8)	/* =1:manual */
#define PCL_PHY_RESET_N		BIT(0)	/* =1:deasssert */

/* SG */
#define SG_USBPCIESEL		0x590
#define SG_USBPCIESEL_PCIE	BIT(0)

/* SC */
#define SC_US3SRCSEL		0x2244
#define SC_US3SRCSEL_2LANE	GENMASK(9, 8)

#define PCL_PHY_R00		0
#define   RX_EQ_ADJ_EN		BIT(3)		/* enable for EQ adjustment */
#define PCL_PHY_R06		6
#define   RX_EQ_ADJ		GENMASK(5, 0)	/* EQ adjustment value */
#define   RX_EQ_ADJ_VAL		0
#define PCL_PHY_R26		26
#define   VCO_CTRL		GENMASK(7, 4)	/* Tx VCO adjustment value */
#define   VCO_CTRL_INIT_VAL	5
#define PCL_PHY_R28		28
#define   VCOPLL_CLMP		GENMASK(3, 2)	/* Tx VCOPLL clamp mode */
#define   VCOPLL_CLMP_VAL	0

struct uniphier_pciephy_priv {
	void __iomem *base;
	struct device *dev;
	struct clk *clk, *clk_gio;
	struct reset_control *rst, *rst_gio;
	const struct uniphier_pciephy_soc_data *data;
};

struct uniphier_pciephy_soc_data {
	bool is_legacy;
	bool is_dual_phy;
	void (*set_phymode)(struct regmap *regmap);
};

static void uniphier_pciephy_testio_write(struct uniphier_pciephy_priv *priv,
					  int id, u32 data)
{
	if (id)
		data <<= TESTIO_PHY_SHIFT;

	/* need to read TESTO twice after accessing TESTI */
	writel(data, priv->base + PCL_PHY_TEST_I);
	readl(priv->base + PCL_PHY_TEST_O);
	readl(priv->base + PCL_PHY_TEST_O);
}

static u32 uniphier_pciephy_testio_read(struct uniphier_pciephy_priv *priv, int id)
{
	u32 val = readl(priv->base + PCL_PHY_TEST_O);

	if (id)
		val >>= TESTIO_PHY_SHIFT;

	return val & TESTO_DAT_MASK;
}

static void uniphier_pciephy_set_param(struct uniphier_pciephy_priv *priv,
				       int id, u32 reg, u32 mask, u32 param)
{
	u32 val;

	/* read previous data */
	val  = FIELD_PREP(TESTI_DAT_MASK, 1);
	val |= FIELD_PREP(TESTI_ADR_MASK, reg);
	uniphier_pciephy_testio_write(priv, id, val);
	val = uniphier_pciephy_testio_read(priv, id);

	/* update value */
	val &= ~mask;
	val |= mask & param;
	val = FIELD_PREP(TESTI_DAT_MASK, val);
	val |= FIELD_PREP(TESTI_ADR_MASK, reg);
	uniphier_pciephy_testio_write(priv, id, val);
	uniphier_pciephy_testio_write(priv, id, val | TESTI_WR_EN);
	uniphier_pciephy_testio_write(priv, id, val);

	/* read current data as dummy */
	val  = FIELD_PREP(TESTI_DAT_MASK, 1);
	val |= FIELD_PREP(TESTI_ADR_MASK, reg);
	uniphier_pciephy_testio_write(priv, id, val);
	uniphier_pciephy_testio_read(priv, id);
}

static void uniphier_pciephy_assert(struct uniphier_pciephy_priv *priv)
{
	u32 val;

	val = readl(priv->base + PCL_PHY_RESET);
	val &= ~PCL_PHY_RESET_N;
	val |= PCL_PHY_RESET_N_MNMODE;
	writel(val, priv->base + PCL_PHY_RESET);
}

static void uniphier_pciephy_deassert(struct uniphier_pciephy_priv *priv)
{
	u32 val;

	val = readl(priv->base + PCL_PHY_RESET);
	val |= PCL_PHY_RESET_N_MNMODE | PCL_PHY_RESET_N;
	writel(val, priv->base + PCL_PHY_RESET);
}

static int uniphier_pciephy_init(struct phy *phy)
{
	struct uniphier_pciephy_priv *priv = phy_get_drvdata(phy);
	u32 val;
	int ret, id;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->clk_gio);
	if (ret)
		goto out_clk_disable;

	ret = reset_control_deassert(priv->rst);
	if (ret)
		goto out_clk_gio_disable;

	ret = reset_control_deassert(priv->rst_gio);
	if (ret)
		goto out_rst_assert;

	/* support only 1 port */
	val = readl(priv->base + PCL_PHY_CLKCTRL);
	val &= ~PORT_SEL_MASK;
	val |= PORT_SEL_1;
	writel(val, priv->base + PCL_PHY_CLKCTRL);

	/* legacy controller doesn't have phy_reset and parameters */
	if (priv->data->is_legacy)
		return 0;

	for (id = 0; id < (priv->data->is_dual_phy ? 2 : 1); id++) {
		uniphier_pciephy_set_param(priv, id, PCL_PHY_R00,
				   RX_EQ_ADJ_EN, RX_EQ_ADJ_EN);
		uniphier_pciephy_set_param(priv, id, PCL_PHY_R06, RX_EQ_ADJ,
				   FIELD_PREP(RX_EQ_ADJ, RX_EQ_ADJ_VAL));
		uniphier_pciephy_set_param(priv, id, PCL_PHY_R26, VCO_CTRL,
				   FIELD_PREP(VCO_CTRL, VCO_CTRL_INIT_VAL));
		uniphier_pciephy_set_param(priv, id, PCL_PHY_R28, VCOPLL_CLMP,
				   FIELD_PREP(VCOPLL_CLMP, VCOPLL_CLMP_VAL));
	}
	usleep_range(1, 10);

	uniphier_pciephy_deassert(priv);
	usleep_range(1, 10);

	return 0;

out_rst_assert:
	reset_control_assert(priv->rst);
out_clk_gio_disable:
	clk_disable_unprepare(priv->clk_gio);
out_clk_disable:
	clk_disable_unprepare(priv->clk);

	return ret;
}

static int uniphier_pciephy_exit(struct phy *phy)
{
	struct uniphier_pciephy_priv *priv = phy_get_drvdata(phy);

	if (!priv->data->is_legacy)
		uniphier_pciephy_assert(priv);
	reset_control_assert(priv->rst_gio);
	reset_control_assert(priv->rst);
	clk_disable_unprepare(priv->clk_gio);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct phy_ops uniphier_pciephy_ops = {
	.init  = uniphier_pciephy_init,
	.exit  = uniphier_pciephy_exit,
	.owner = THIS_MODULE,
};

static int uniphier_pciephy_probe(struct platform_device *pdev)
{
	struct uniphier_pciephy_priv *priv;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(dev);
	if (WARN_ON(!priv->data))
		return -EINVAL;

	priv->dev = dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (priv->data->is_legacy) {
		priv->clk_gio = devm_clk_get(dev, "gio");
		if (IS_ERR(priv->clk_gio))
			return PTR_ERR(priv->clk_gio);

		priv->rst_gio =
			devm_reset_control_get_shared(dev, "gio");
		if (IS_ERR(priv->rst_gio))
			return PTR_ERR(priv->rst_gio);

		priv->clk = devm_clk_get(dev, "link");
		if (IS_ERR(priv->clk))
			return PTR_ERR(priv->clk);

		priv->rst = devm_reset_control_get_shared(dev, "link");
		if (IS_ERR(priv->rst))
			return PTR_ERR(priv->rst);
	} else {
		priv->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(priv->clk))
			return PTR_ERR(priv->clk);

		priv->rst = devm_reset_control_get_shared(dev, NULL);
		if (IS_ERR(priv->rst))
			return PTR_ERR(priv->rst);
	}

	phy = devm_phy_create(dev, dev->of_node, &uniphier_pciephy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "socionext,syscon");
	if (!IS_ERR(regmap) && priv->data->set_phymode)
		priv->data->set_phymode(regmap);

	phy_set_drvdata(phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static void uniphier_pciephy_ld20_setmode(struct regmap *regmap)
{
	regmap_update_bits(regmap, SG_USBPCIESEL,
			   SG_USBPCIESEL_PCIE, SG_USBPCIESEL_PCIE);
}

static void uniphier_pciephy_nx1_setmode(struct regmap *regmap)
{
	regmap_update_bits(regmap, SC_US3SRCSEL,
			   SC_US3SRCSEL_2LANE, SC_US3SRCSEL_2LANE);
}

static const struct uniphier_pciephy_soc_data uniphier_pro5_data = {
	.is_legacy = true,
};

static const struct uniphier_pciephy_soc_data uniphier_ld20_data = {
	.is_legacy = false,
	.is_dual_phy = false,
	.set_phymode = uniphier_pciephy_ld20_setmode,
};

static const struct uniphier_pciephy_soc_data uniphier_pxs3_data = {
	.is_legacy = false,
	.is_dual_phy = false,
};

static const struct uniphier_pciephy_soc_data uniphier_nx1_data = {
	.is_legacy = false,
	.is_dual_phy = true,
	.set_phymode = uniphier_pciephy_nx1_setmode,
};

static const struct of_device_id uniphier_pciephy_match[] = {
	{
		.compatible = "socionext,uniphier-pro5-pcie-phy",
		.data = &uniphier_pro5_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-pcie-phy",
		.data = &uniphier_ld20_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-pcie-phy",
		.data = &uniphier_pxs3_data,
	},
	{
		.compatible = "socionext,uniphier-nx1-pcie-phy",
		.data = &uniphier_nx1_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, uniphier_pciephy_match);

static struct platform_driver uniphier_pciephy_driver = {
	.probe = uniphier_pciephy_probe,
	.driver = {
		.name = "uniphier-pcie-phy",
		.of_match_table = uniphier_pciephy_match,
	},
};
module_platform_driver(uniphier_pciephy_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier PHY driver for PCIe controller");
MODULE_LICENSE("GPL v2");
