// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip PIPE USB3.0 PCIE SATA combphy driver
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
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <dt-bindings/phy/phy.h>

#define BIT_WRITEABLE_SHIFT		16
#define COMBPHY_MAX_MODE		4
#define COMBPHY_MODE_PCIE		0
#define COMBPHY_MODE_USB3		1
#define COMBPHY_MODE_SATA		2
#define COMBPHY_MODE_QSGMII		3
#define COMBPHY_MODE_INVALID		4
#define COMBPHY_DISABLED_REG_OFFSET	0xFFFF

struct combphy_reg {
	u16 offset;
	u16 bitend;
	u16 bitstart;
	u16 data[COMBPHY_MAX_MODE];
};

struct rockchip_combphy_grfcfg {
	struct combphy_reg mode_set;
	struct combphy_reg data_width_set;
};

struct rockchip_combphy_priv {
	u8 mode;
	void __iomem *mmio;
	struct regmap *pipe_grf;
	struct regmap *phy_grf;
	struct phy *phy;
	const struct rockchip_combphy_grfcfg *cfg;
};

static inline bool param_read(struct regmap *base, const struct combphy_reg *reg, u8 mode)
{
	int ret;
	u32 mask, orig, tmp, val;

	if (reg->offset == COMBPHY_DISABLED_REG_OFFSET ||
	    mode >= COMBPHY_MODE_INVALID)
		return true;

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	val = reg->data[mode];

	mask = GENMASK(reg->bitend, reg->bitstart);
	tmp = (orig & mask) >> reg->bitstart;

	return tmp == val;
}

static int param_write(struct regmap *base, const struct combphy_reg *reg, u8 mode)
{
	u32 val, mask, tmp;

	if (reg->offset == COMBPHY_DISABLED_REG_OFFSET ||
	    mode >= COMBPHY_MODE_INVALID)
		return 0;

	tmp = reg->data[mode];
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static int rockchip_combphy_set_mode(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg;
	if (priv->mode >= COMBPHY_MODE_INVALID)
		return -EINVAL;

	param_write(priv->phy_grf, &cfg->mode_set, priv->mode);
	param_write(priv->phy_grf, &cfg->data_width_set, priv->mode);

	return 0;
}

static int rochchip_combphy_init(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = rockchip_combphy_set_mode(priv);
	if (ret)
		return ret;

	return 0;
}

static int rochchip_combphy_exit(struct phy *phy)
{
	return 0;
}

static const struct phy_ops rochchip_combphy_ops = {
	.init = rochchip_combphy_init,
	.exit = rochchip_combphy_exit,
	.owner = THIS_MODULE,
};

static struct phy *rockchip_combphy_xlate(struct device *dev,
					  struct of_phandle_args *args)
{
	struct rockchip_combphy_priv *priv = dev_get_drvdata(dev);
	int mode;

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	mode = args->args[0];

	switch (mode) {
	case PHY_TYPE_USB2:
	case PHY_TYPE_USB3:
		priv->mode = COMBPHY_MODE_USB3;
		break;

	case PHY_TYPE_PCIE:
		priv->mode = COMBPHY_MODE_PCIE;
		break;

	case PHY_TYPE_SATA:
		priv->mode = COMBPHY_MODE_SATA;
		break;

	default:
		dev_err(dev, "unsupported device type: %d\n", mode);
		return ERR_PTR(-EINVAL);
	}

	return priv->phy;
}

static int rockchip_combphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct rockchip_combphy_priv *priv;
	struct device_node *np = dev->of_node;
	const struct rockchip_combphy_grfcfg *phy_cfg;
	struct resource *res;
	int ret;

	phy_cfg = of_device_get_match_data(dev);
	if (!phy_cfg) {
		dev_err(dev, "No OF match data provided\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->mmio)) {
		ret = PTR_ERR(priv->mmio);
		return ret;
	}

	priv->pipe_grf = syscon_regmap_lookup_by_phandle(np, "rockchip,pipe-grf");
	if (IS_ERR(priv->pipe_grf)) {
		dev_err(dev, "failed to find peri_ctrl pipe-grf regmap\n");
		return PTR_ERR(priv->pipe_grf);
	}

	priv->phy_grf = syscon_regmap_lookup_by_phandle(np, "rockchip,pipe-phy-grf");
	if (IS_ERR(priv->phy_grf)) {
		dev_err(dev, "failed to find peri_ctrl pipe-phy-grf regmap\n");
		return PTR_ERR(priv->phy_grf);
	}

	priv->cfg = phy_cfg;
	priv->phy = devm_phy_create(dev, NULL, &rochchip_combphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, rockchip_combphy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct rockchip_combphy_grfcfg rk3568_combphy_cfgs = {
			/* offset end start pcie  usb  sata  qsgmii */
	.mode_set	= { 0x0000, 3, 2, {0x00, 0x01, 0x02, 0x03} },
	.data_width_set	= { 0x0000, 0, 0, {0x01, 0x00, 0x01, 0x01} },
};

static const struct of_device_id rockchip_combphy_of_match[] = {
	{
		.compatible = "rockchip,rk3568-naneng-combphy",
		.data = &rk3568_combphy_cfgs,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_combphy_of_match);

static struct platform_driver rockchip_combphy_driver = {
	.probe	= rockchip_combphy_probe,
	.driver = {
		.name = "naneng-combphy",
		.of_match_table = rockchip_combphy_of_match,
	},
};
module_platform_driver(rockchip_combphy_driver);

MODULE_DESCRIPTION("Rockchip NANENG COMBPHY driver");
MODULE_LICENSE("GPL v2");
